#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string renumberStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool topLevelContains(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr && record->form_key == key) return true;
        }
    }
    return false;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::renumberPluginFormIds(
    const nlohmann::json& arguments) {
    const auto pluginName = renumberStringArgument(arguments, 0);
    auto startText = renumberStringArgument(arguments, 1);
    const bool apply = support::boolArgument(arguments, 2);

    if (pluginName.empty() || startText.empty()) {
        return std::unexpected(Error{
            "RenumberPluginArgumentsMissing",
            "RenumberPluginFormIds requires plugin name and starting hexadecimal FormID"
        });
    }
    if (startText.size() >= 2 && startText[0] == '0' &&
        (startText[1] == 'x' || startText[1] == 'X')) {
        startText.erase(0, 2);
    }

    std::uint32_t start = 0;
    const auto [ptr, parseError] = std::from_chars(
        startText.data(), startText.data() + startText.size(), start, 16);
    if (parseError != std::errc{} || ptr != startText.data() + startText.size() ||
        start == 0 || start > 0xFFFFFFu) {
        return std::unexpected(Error{
            "InvalidFormId",
            "Starting FormID must be a non-zero 24-bit hexadecimal object id"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    std::vector<fo4::FormKey> nativeKeys;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4 ||
            record->form_key.owner() != loaded->plugin.key) {
            continue;
        }
        nativeKeys.push_back(record->form_key);
    }
    std::sort(nativeKeys.begin(), nativeKeys.end(), [](const auto& left, const auto& right) {
        return left.local().value() < right.local().value();
    });

    if (nativeKeys.empty()) {
        return nlohmann::json("'" + pluginName + "' has no records of its own to renumber.");
    }

    const auto [lowest, highest] = fo4::local_form_id_range(loaded->plugin);
    const auto end64 = static_cast<std::uint64_t>(start) + nativeKeys.size() - 1u;
    if (start < lowest || end64 > highest) {
        return std::unexpected(Error{
            "RenumberRangeInvalid",
            "Requested range " + support::localHex(start) + "-" +
                support::localHex(static_cast<std::uint32_t>(std::min<std::uint64_t>(end64, 0xFFFFFFu))) +
                " is outside this plugin's allowed local FormID range"
        });
    }

    struct Rekey {
        fo4::FormKey oldKey;
        fo4::FormKey newKey;
        fo4::LocalFormId newLocal;
    };
    std::vector<Rekey> mappings;
    mappings.reserve(nativeKeys.size());
    for (std::size_t index = 0; index < nativeKeys.size(); ++index) {
        const auto local = start + static_cast<std::uint32_t>(index);
        const fo4::FormKey newKey{loaded->plugin.key, fo4::LocalFormId{local}};
        if (newKey == nativeKeys[index]) continue;
        mappings.push_back({nativeKeys[index], newKey, fo4::LocalFormId{local}});
    }

    if (mappings.empty()) {
        return nlohmann::json(
            "'" + pluginName + "' is already numbered consecutively from " +
            support::localHex(start) + "; nothing to do.");
    }

    std::map<fo4::FormKey, std::size_t> movingByOldKey;
    for (std::size_t index = 0; index < mappings.size(); ++index) {
        movingByOldKey.emplace(mappings[index].oldKey, index);
        if (!topLevelContains(loaded->plugin, mappings[index].oldKey)) {
            return nlohmann::json(
                "Renumber is blocked by nested record " + mappings[index].oldKey.to_string() +
                ": parent GRUP label rewriting is not proven for plugin-wide rekeying. "
                "Renumber this plugin in xEdit instead.");
        }
    }

    for (const auto& mapping : mappings) {
        if (fo4::find_record(loaded->plugin, mapping.newKey) == nullptr) continue;
        if (movingByOldKey.contains(mapping.newKey)) continue;
        return std::unexpected(Error{
            "RenumberTargetOccupied",
            "Target FormID is occupied by a record that is not being moved: " +
                mapping.newKey.to_string()
        });
    }

    for (const auto& mapping : mappings) {
        if (const auto refusal = support::parentGroupRelabelRefusal(loaded->plugin, mapping.oldKey)) {
            return std::unexpected(*refusal);
        }
    }

    if (!apply) {
        return nlohmann::json(
            "DRY RUN: would renumber " + std::to_string(mappings.size()) + " of " +
            std::to_string(nativeKeys.size()) + " record(s) in " + pluginName + " into " +
            support::localHex(start) + "-" + support::localHex(static_cast<std::uint32_t>(end64)) +
            ", repointing every supported in-plugin reference. References from OTHER plugins "
            "into this one will still point at the old ids. Re-run with apply=true.");
    }

    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    // A mapping whose destination is another old key depends on that record
    // moving first. Resolve those chains before mutation so each FormLink key
    // is rewritten exactly once and every destination is vacant when used.
    std::vector<std::size_t> order;
    order.reserve(mappings.size());
    std::vector<bool> remaining(mappings.size(), true);
    std::size_t left = mappings.size();
    while (left != 0) {
        bool progressed = false;
        for (std::size_t index = 0; index < mappings.size(); ++index) {
            if (!remaining[index]) continue;
            const auto dependency = movingByOldKey.find(mappings[index].newKey);
            if (dependency != movingByOldKey.end() && remaining[dependency->second]) continue;
            order.push_back(index);
            remaining[index] = false;
            --left;
            progressed = true;
        }
        if (!progressed) {
            return std::unexpected(Error{
                "RenumberCycleUnsupported",
                "Requested FormID mapping contains a cycle and cannot be applied without temporary IDs"
            });
        }
    }

    try {
        auto mutated = loaded->plugin;
        std::size_t changed = 0;
        for (const auto index : order) {
            const auto& mapping = mappings[index];
            const auto status = fo4::renumber_local_record(
                mutated, mapping.oldKey, mapping.newLocal);
            if (status != fo4::RecordRenumberStatus::renumbered) {
                return std::unexpected(Error{
                    "RenumberPluginFailed",
                    "Could not rekey " + mapping.oldKey.to_string() + " to " +
                        mapping.newKey.to_string()
                });
            }
            ++changed;
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Renumbered " + std::to_string(changed) + " record(s) in " + pluginName +
            " into " + support::localHex(start) + "-" +
            support::localHex(static_cast<std::uint32_t>(end64)) +
            " and repointed every supported in-plugin reference. References from other plugins "
            "into this one now dangle until patched. SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RenumberPluginFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
