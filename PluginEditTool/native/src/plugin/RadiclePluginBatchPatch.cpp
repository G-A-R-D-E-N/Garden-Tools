#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <fo4/plugin/batch_patch.hpp>

namespace fo4ide::plugin {

using support::asciiLower;

namespace {

constexpr std::size_t kPreviewRows = 25;
constexpr std::size_t kShownErrors = 5;

std::string trimmed(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

Result<std::vector<fo4::BatchOperationSpec>> parseOperations(const nlohmann::json& rawOps) {
    std::vector<fo4::BatchOperationSpec> specs;
    for (const auto& raw : rawOps) {
        if (!raw.is_object()) return std::unexpected(Error{"InvalidOperations", "every operation must be an object"});
        fo4::BatchOperationSpec spec;
        for (const auto& [key, target] : {std::pair<const char*, std::string*>{"op", &spec.op},
                                          std::pair<const char*, std::string*>{"field", &spec.field},
                                          std::pair<const char*, std::string*>{"value", &spec.value}}) {
            if (!raw.contains(key)) continue;
            if (!raw.at(key).is_string()) {
                return std::unexpected(Error{"InvalidOperations", std::string(key) + " must be a string"});
            }
            *target = raw.at(key).get<std::string>();
        }
        specs.push_back(std::move(spec));
    }
    return specs;
}

}

Result<nlohmann::json> RadiclePluginBackend::batchPatchRecords(const nlohmann::json& arguments) {
    const auto patchName = trimmed(support::stringArgument(arguments, 0));
    const auto sourceName = trimmed(support::stringArgument(arguments, 1));
    const auto typeText = trimmed(support::stringArgument(arguments, 2));
    const auto operationsText = support::stringArgument(arguments, 3);
    const auto filterField = trimmed(support::stringArgument(arguments, 4));
    const auto filterValue = support::stringArgument(arguments, 5);

    if (sourceName.empty()) return std::unexpected(Error{"SourcePluginMissing", "Provide source_plugin."});
    if (patchName.empty()) return std::unexpected(Error{"PatchPluginMissing", "Provide patch_plugin."});
    if (asciiLower(patchName) == asciiLower(sourceName)) {
        return std::unexpected(Error{"PatchPluginIsSource", "patch_plugin and source_plugin must differ"});
    }

    bool dryRun = true;
    if (arguments.is_array() && arguments.size() > 6 && !arguments[6].is_null() &&
        !support::tryBoolArgument(arguments, 6, dryRun)) {
        return std::unexpected(Error{"InvalidDryRunFlag", "dry_run must be boolean"});
    }
    std::size_t limit = fo4::k_batch_default_limit;
    if (arguments.is_array() && arguments.size() > 7 && !arguments[7].is_null()) {
        const auto parsed = core::jsonIntSaturated(arguments[7]);
        if (!parsed || *parsed <= 0) {
            return std::unexpected(Error{"InvalidLimit", "limit must be a positive integer"});
        }
        limit = static_cast<std::size_t>(*parsed);
    }

    nlohmann::json rawOps;
    try {
        rawOps = nlohmann::json::parse(operationsText.empty() ? "[]" : operationsText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidOperations", error.what()});
    }
    if (!rawOps.is_array()) {
        return std::unexpected(Error{"InvalidOperations", "operations must be a JSON array of {op, field, value}"});
    }
    if (rawOps.empty() && !dryRun) {
        return std::unexpected(Error{"OperationsMissing",
            "Provide at least one operation in 'operations' (or use dry_run=true to preview)."});
    }

    const std::vector<std::string_view> owners{patchName, sourceName};
    OpLock lock(*this, dryRun ? OpLock::Mode::Read : OpLock::Mode::Write, owners);

    auto* source = findPlugin(sourceName);
    if (source == nullptr) {
        return std::unexpected(Error{"SourcePluginNotLoaded", "Source plugin is not loaded: " + sourceName});
    }

    const auto specs = parseOperations(rawOps);
    if (!specs) return std::unexpected(specs.error());

    const fo4::BatchPatchHost host{
        [this](std::string_view value) { return resolveListValue(value); },
        [this](const fo4::FormKey& key) -> std::optional<fo4::Signature> {
            const auto* winner = (*graph_).winner(key);
            if (winner == nullptr) return std::nullopt;
            return winner->signature;
        },
        [this](fo4::Plugin& plugin, const fo4::PluginKey& owner) {
            support::ensureOwnerMaster(plugin, owner, plugins_);
        },
    };
    const auto plan = fo4::plan_batch_patch(typeText, *specs, filterField, filterValue, host);
    if (!plan) return std::unexpected(Error{plan.error().code, plan.error().message});

    const auto selection = fo4::select_batch_records(source->plugin, *plan, limit);
    const auto& matched = selection.matched;
    const auto upperType = plan->type_text;

    if (selection.total == 0) {
        return nlohmann::json("No records of type '" + upperType + "' found in '" + sourceName + "'.");
    }
    const auto filterText = plan->filtering ? " (" + filterField + " contains " + filterValue + ")" : std::string(" (no filter)");
    if (matched.empty()) {
        return nlohmann::json("No " + upperType + " records in '" + sourceName + "' match the filter" + filterText +
                              ". Adjust the filter or check field names.");
    }

    if (dryRun) {
        std::string text = "DRY RUN -- " + std::to_string(matched.size()) + " of " + std::to_string(selection.total) + " " +
                           upperType + " records in '" + sourceName + "' match" + filterText + ":\n";
        for (std::size_t i = 0; i < std::min(matched.size(), kPreviewRows); ++i) {
            const auto eid = fo4::record_editor_id(*matched[i]);
            text += "  " + (eid.empty() ? std::string("(no-eid)") : eid) + " [" + matched[i]->form_key.to_string() + "]\n";
        }
        if (matched.size() > kPreviewRows) text += "  ...and " + std::to_string(matched.size() - kPreviewRows) + " more.\n";
        text += "\nOperations to apply to each:\n";
        for (const auto& op : plan->operations) text += "  " + op.op + " " + op.field + " = \"" + op.value + "\"\n";
        text += "\nRun again with dry_run=false to write " + std::to_string(matched.size()) + " records into '" +
                patchName + "'.";
        return nlohmann::json(std::move(text));
    }

    auto* target = findPlugin(patchName);
    if (target == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Target plugin is not loaded yet: " + patchName});
    }
    if (target == source) return std::unexpected(Error{"PatchPluginIsSource", "patch_plugin and source_plugin must differ"});
    if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

    try {
        auto applied = fo4::apply_batch_patch(target->plugin, source->plugin, matched, *plan, host);

        if (applied.failed != 0) {
            std::string message = std::to_string(applied.failed) + " of " + std::to_string(matched.size()) +
                                  " record(s) failed; nothing was staged.";
            for (std::size_t i = 0; i < std::min(applied.errors.size(), kShownErrors); ++i) message += "\n  " + applied.errors[i];
            return std::unexpected(Error{"BatchPatchFailed", std::move(message)});
        }
        if (!applied.plugin) {
            return nlohmann::json("No change: every matched record in '" + patchName + "' already has these values.");
        }

        stagePluginMutationLocked(*target, std::move(*applied.plugin));
        return nlohmann::json("Batch patched " + std::to_string(applied.done) + " of " + std::to_string(matched.size()) +
                              " records into '" + patchName + "' (" + std::to_string(applied.operations_applied) +
                              " field op(s) applied, " + std::to_string(applied.inserted) +
                              " new override(s)). SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"BatchPatchFailed", error.what()});
    }
}

}
