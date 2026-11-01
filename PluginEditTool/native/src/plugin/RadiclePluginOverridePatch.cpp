#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/forms/components.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

// RevertOverrides and CreateMergedPatch: two bulk override -> patch drivers,
// faithful ports of C# WriteService.RevertOverridesFrom / CreateMergedPatch.
// Both compose over fo4::copy_record_as_override, forwarding many records into
// one patch plugin, then writing the patch a single time (rather than the
// per-record write the single-record CopyAsOverride path performs).
namespace fo4ide::plugin {

using support::editorId;

namespace {

int intArgument(const nlohmann::json& args, std::size_t index, int fallback) {
    if (!args.is_array() || index >= args.size()) return fallback;
    return core::jsonIntSaturated(args[index]).value_or(fallback);
}

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string normalizePluginName(std::string name) {
    if (name.empty()) return name;
    const auto lowered = asciiLower(name);
    if (!lowered.ends_with(".esp") && !lowered.ends_with(".esm") && !lowered.ends_with(".esl")) {
        name += ".esp";
    }
    return name;
}

std::vector<std::string> splitCsv(std::string_view value) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto comma = value.find(',', start);
        if (comma == std::string_view::npos) {
            out.emplace_back(value.substr(start));
            break;
        }
        out.emplace_back(value.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

std::string copyStatusText(fo4::RecordCopyStatus status) {
    switch (status) {
        case fo4::RecordCopyStatus::source_missing:
            return "record not present in the source plugin";
        case fo4::RecordCopyStatus::source_containment_unsupported:
            return "record has an unsupported nested containment";
        case fo4::RecordCopyStatus::already_exists:
            return "already present in the patch";
        case fo4::RecordCopyStatus::inserted:
        case fo4::RecordCopyStatus::replaced:
            return "ok";
    }
    return "unknown copy status";
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::createMergedPatch(const nlohmann::json& arguments) {
    const auto pluginsCsv = support::stringArgument(arguments, 0);
    const auto patchArg = trim(support::stringArgument(arguments, 1));
    bool apply = false;
    support::tryBoolArgument(arguments, 2, apply);
    constexpr int limit = 5000;

    if (patchArg.empty()) {
        return std::unexpected(Error{
            "PatchPluginMissing", "Choose a patch plugin to write the merged records into."});
    }
    const auto patchName = normalizePluginName(patchArg);
    const auto patchLower = asciiLower(patchName);

    // Empty selection means "every source plugin"; the patch is always excluded.
    std::set<std::string> selected;
    for (const auto& raw : splitCsv(pluginsCsv)) {
        const auto name = trim(raw);
        if (!name.empty()) selected.insert(asciiLower(name));
    }
    const auto isSelected = [&](std::string_view name) {
        const auto lowered = asciiLower(name);
        if (lowered == patchLower) return false;
        return selected.empty() || selected.count(lowered) != 0;
    };

    OpLock lock(*this, OpLock::Mode::Write, {patchName});

    std::set<std::string> selectedPresent;
    for (const auto& plugin : plugins_) {
        if (isSelected(plugin.name)) selectedPresent.insert(asciiLower(plugin.name));
    }
    if (selectedPresent.size() < 2) {
        return std::unexpected(Error{
            "MergedPatchNeedsSources",
            "A merged patch needs at least two source plugins; matched " +
                std::to_string(selectedPresent.size()) + "."});
    }

    struct Contest {
        fo4::FormKey key;
        std::string winner;
    };
    std::vector<Contest> contested;
    for (const auto& key : (*graph_).conflicts()) {
        int selectedCount = 0;
        std::string winner;
        for (const auto& version : (*graph_).versions(key)) {
            const auto name = std::string(version.plugin.filename());
            if (!isSelected(name)) continue;
            ++selectedCount;
            winner = name;
        }
        if (selectedCount >= 2) contested.push_back({key, std::move(winner)});
    }

    if (contested.empty()) {
        return nlohmann::json(
            "No records are touched by two or more of the " +
            std::to_string(selectedPresent.size()) + " selected plugin(s); nothing to merge.");
    }

    const bool capped = static_cast<int>(contested.size()) > limit;
    const auto workCount = std::min<std::size_t>(contested.size(), static_cast<std::size_t>(limit));
    std::vector<Contest> work(contested.begin(), contested.begin() + workCount);

    if (!apply) {
        std::map<std::string, int> byWinner;
        for (const auto& contest : work) ++byWinner[contest.winner];
        std::vector<std::pair<std::string, int>> ordered(byWinner.begin(), byWinner.end());
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        std::string message = "DRY RUN: " + std::to_string(work.size()) + " conflicting record(s)";
        if (capped) {
            message += " (capped at " + std::to_string(limit) + " of " +
                       std::to_string(contested.size()) + ")";
        }
        message += " across " + std::to_string(selectedPresent.size()) +
                   " plugin(s) would be forwarded into " + patchName + ".\n";
        bool first = true;
        for (const auto& [winner, count] : ordered) {
            if (!first) message += "\n";
            first = false;
            message += "  " + winner + ": " + std::to_string(count) + " record(s) win";
        }
        message += "\nRe-run with apply=true to write them.";
        return nlohmann::json(message);
    }

    auto* target = findPlugin(patchName);
    if (target == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded", "Target plugin is not loaded yet: " + patchName});
    }
    if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

    try {
        auto mutated = target->plugin;
        int forwarded = 0;
        std::vector<std::string> failures;
        for (const auto& contest : work) {
            const auto* winner = findPlugin(contest.winner);
            if (winner == nullptr) {
                failures.push_back(contest.key.to_string() + ": winning plugin not loaded");
                continue;
            }
            const auto status =
                fo4::copy_record_as_override(mutated, winner->plugin, contest.key, /*overwrite=*/false);
            if (status == fo4::RecordCopyStatus::inserted ||
                status == fo4::RecordCopyStatus::replaced) {
                ++forwarded;
            } else {
                failures.push_back(contest.key.to_string() + ": " + copyStatusText(status));
            }
        }

        stagePluginMutationLocked(*target, std::move(mutated));

        std::string result = "Merged patch " + patchName + ": forwarded " +
                             std::to_string(forwarded) + " of " + std::to_string(work.size()) +
                             " conflicting record(s). Ensure " + patchName +
                             " loads last. SavePlugin to persist.";
        if (!failures.empty()) {
            result += " " + std::to_string(failures.size()) + " failed: ";
            for (std::size_t i = 0; i < failures.size() && i < std::size_t{5}; ++i) {
                if (i != 0) result += "; ";
                result += failures[i];
            }
            result += ".";
        }
        return nlohmann::json(result);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MergedPatchFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::revertOverrides(const nlohmann::json& arguments) {
    const auto badPlugin = trim(support::stringArgument(arguments, 0));
    const auto patchArg = trim(support::stringArgument(arguments, 1));
    const auto signature = trim(support::stringArgument(arguments, 2));
    const auto containsComponent = trim(support::stringArgument(arguments, 3));
    bool apply = false;
    support::tryBoolArgument(arguments, 4, apply);
    int limit = intArgument(arguments, 5, 50);
    if (limit <= 0) limit = 50;

    if (badPlugin.empty()) {
        return std::unexpected(Error{
            "BadPluginMissing", "Provide the plugin whose overrides to revert."});
    }
    const auto badLower = asciiLower(badPlugin);
    const auto signatureLower = asciiLower(signature);
    const auto patchName = normalizePluginName(patchArg);

    OpLock lock(*this, OpLock::Mode::Write, {patchName});

    const auto* bad = findPlugin(badPlugin);
    if (bad == nullptr) {
        return std::unexpected(Error{"BadPluginNotLoaded", "'" + badPlugin + "' is not loaded."});
    }

    std::optional<fo4::FormKey> componentFk;
    if (!containsComponent.empty()) {
        componentFk = resolveListValue(containsComponent);
        if (!componentFk) {
            return std::unexpected(Error{
                "ComponentNotResolved",
                "'" + containsComponent + "' is not a FormKey/EditorID of a loaded record."});
        }
    }

    // Each override of badPlugin that also wins (is the top context) is a
    // revert candidate; the record is restored from the version immediately
    // below badPlugin in load order.
    struct Revert {
        fo4::FormKey key;
        std::string prior;
        std::string editor;
        std::string signature;
    };
    std::vector<Revert> reverts;
    int overridesFound = 0;
    int skipped = 0;
    for (const auto& key : (*graph_).conflicts()) {
        const auto& versions = (*graph_).versions(key);
        if (versions.size() < 2) continue;

        std::optional<std::size_t> badIndex;
        for (std::size_t i = 0; i < versions.size(); ++i) {
            if (asciiLower(std::string(versions[i].plugin.filename())) == badLower) {
                badIndex = i;
                break;
            }
        }
        if (!badIndex || *badIndex == 0) continue;  // absent, or the origin (not an override)

        const auto recordSig = versions[*badIndex].signature.to_string();
        if (!signatureLower.empty() && asciiLower(recordSig) != signatureLower) continue;
        ++overridesFound;

        if (*badIndex != versions.size() - 1) {  // a later plugin already wins
            ++skipped;
            continue;
        }
        if (componentFk) {
            bool uses = false;
            if (fo4::forms::record_supports_components(versions[*badIndex].signature)) {
                try {
                    for (const auto& entry : fo4::forms::decode_components(bad->plugin, key)) {
                        if (entry.component == *componentFk) { uses = true; break; }
                    }
                } catch (const std::exception&) {
                    uses = false;
                }
            }
            if (!uses) { ++skipped; continue; }
        }
        const auto& prior = versions[versions.size() - 2];
        std::string editor;
        if (const auto* record = fo4::find_record(bad->plugin, key)) editor = editorId(*record);
        reverts.push_back({key, std::string(prior.plugin.filename()), std::move(editor), recordSig});
    }

    if (overridesFound == 0) {
        return nlohmann::json(
            "'" + badPlugin + "' carries no override records" +
            (signature.empty() ? std::string{} : (" of type " + signature)) +
            " (or it isn't loaded).");
    }

    if (!apply) {
        std::string message = "DRY RUN -- " + std::to_string(reverts.size()) +
                             " record(s) where " + badPlugin +
                             " is the winner would be reverted to the prior version" +
                             (componentFk ? (" (filtered to ones using " + containsComponent + ")")
                                          : std::string{}) +
                             ".\n";
        if (skipped > 0) {
            message += "(" + std::to_string(skipped) + " of " + badPlugin +
                       "'s overrides skipped -- a later plugin already wins, or filtered out.)\n";
        }
        const auto previewCount = std::min<std::size_t>(reverts.size(), static_cast<std::size_t>(limit));
        for (std::size_t i = 0; i < previewCount; ++i) {
            const auto& revert = reverts[i];
            message += "  " + revert.editor + " [" + revert.key.to_string() + "] (" +
                       revert.signature + ") -> restore " + revert.prior + "'s version\n";
        }
        if (reverts.size() > previewCount) {
            message += "  ...and " + std::to_string(reverts.size() - previewCount) + " more.\n";
        }
        message += "Run revert_overrides again with apply=true to write these into '" + patchName + "'.";
        return nlohmann::json(message);
    }

    if (patchArg.empty()) {
        return std::unexpected(Error{
            "PatchPluginMissing", "Provide a patch plugin to write the reverts into."});
    }
    auto* target = findPlugin(patchName);
    if (target == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded", "Target plugin is not loaded yet: " + patchName});
    }
    if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

    try {
        auto mutated = target->plugin;
        int reverted = 0;
        for (const auto& revert : reverts) {
            const auto* prior = findPlugin(revert.prior);
            if (prior == nullptr) continue;
            const auto status =
                fo4::copy_record_as_override(mutated, prior->plugin, revert.key, /*overwrite=*/false);
            if (status == fo4::RecordCopyStatus::inserted ||
                status == fo4::RecordCopyStatus::already_exists) {
                ++reverted;
            } else {
                ++skipped;
            }
        }

        stagePluginMutationLocked(*target, std::move(mutated));

        std::string result = "Reverted " + std::to_string(reverted) + " record(s) that " +
                             badPlugin + " was overriding, into " + patchName + ". ";
        if (skipped > 0) {
            result += std::to_string(skipped) + " skipped (not the winner / filtered out). ";
        }
        result += "SavePlugin to persist " + patchName + ". Load " + patchName +
                  " AFTER " + badPlugin + " so the reverts win.";
        return nlohmann::json(result);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RevertOverridesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
