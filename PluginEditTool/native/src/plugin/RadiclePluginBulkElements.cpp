#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "RadiclePluginSupport.hpp"

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/subrecord_copy.hpp>
#include <fo4/plugin/visit.hpp>

namespace fo4ide::plugin {
namespace {

struct BulkTarget {
    std::string plugin;
    std::string formKeyText;
    fo4::FormKey key;
};

struct BulkElement {
    fo4::Signature signature{};
    std::size_t occurrence{0};
};

struct BulkWorking {
    std::string name;
    fo4::Plugin plugin;
    bool changed{false};
};

std::optional<BulkElement> bulkAuthoringElement(std::string_view path) {
    const auto parsed = support::parseAuthoringPath(path);
    if (!parsed) return std::nullopt;
    return BulkElement{parsed->signature, parsed->occurrence.value_or(0)};
}

bool parseBulkTargets(std::string_view text, std::vector<BulkTarget>& targets, nlohmann::json& failures) {
    nlohmann::json raw;
    try {
        raw = nlohmann::json::parse(text);
    } catch (const std::exception&) {
        return false;
    }
    if (!raw.is_array()) return false;
    for (const auto& entry : raw) {
        std::string plugin;
        std::string formKeyText;
        if (entry.is_object() && entry.contains("plugin") && entry.at("plugin").is_string()) {
            plugin = entry.at("plugin").get<std::string>();
        }
        if (entry.is_object() && entry.contains("formKey") && entry.at("formKey").is_string()) {
            formKeyText = entry.at("formKey").get<std::string>();
        }
        if (plugin.empty() || formKeyText.empty()) {
            failures.push_back({{"plugin", plugin}, {"formKey", formKeyText},
                                {"reason", "plugin and formKey are required"}});
            continue;
        }
        try {
            targets.push_back({plugin, formKeyText, fo4::FormKey::parse(formKeyText)});
        } catch (const std::exception& error) {
            failures.push_back({{"plugin", plugin}, {"formKey", formKeyText}, {"reason", error.what()}});
        }
    }
    return true;
}

nlohmann::json bulkFailure(const BulkTarget& target, std::string reason) {
    return {{"plugin", target.plugin}, {"formKey", target.formKeyText}, {"reason", std::move(reason)}};
}

BulkWorking& bulkWorkingFor(std::vector<BulkWorking>& working, std::string_view name, const fo4::Plugin& original) {
    for (auto& entry : working) {
        if (entry.name == name) return entry;
    }
    working.push_back({std::string(name), original, false});
    return working.back();
}

std::string copyReason(fo4::SubrecordCopyStatus status) {
    switch (status) {
        case fo4::SubrecordCopyStatus::copied: return "";
        case fo4::SubrecordCopyStatus::record_missing: return "RecordNotFound: Record is not present";
        case fo4::SubrecordCopyStatus::source_missing: return "RecordNotFound: Source record is not present";
        case fo4::SubrecordCopyStatus::source_occurrence_missing:
            return "ElementIndexOutOfRange: Source element is not present";
        case fo4::SubrecordCopyStatus::occurrence_unreachable:
            return "ElementIndexOutOfRange: Element cannot be placed at this occurrence";
        case fo4::SubrecordCopyStatus::owner_not_master:
            return "ElementLinkNeedsMaster: a record this element links to is in a plugin that is not a master of this plugin";
        case fo4::SubrecordCopyStatus::not_authorable:
            return "ElementNotAuthorable: Subrecord is coupled or has no independent authoring rule";
        case fo4::SubrecordCopyStatus::unknown_order:
            return "ElementUnknownOrder: A valid insertion order cannot be established";
        case fo4::SubrecordCopyStatus::no_default:
            return "ElementNoDefault: No usable default is proven for this subrecord";
        case fo4::SubrecordCopyStatus::localized_mismatch:
            return "ElementLocalized: Localized text IDs copy only within one localized plugin or between inline plugins";
    }
    return "ElementCopyFailed: Unknown copy status";
}

std::string removeReason(fo4::SubrecordRemoveStatus status) {
    switch (status) {
        case fo4::SubrecordRemoveStatus::removed: return "";
        case fo4::SubrecordRemoveStatus::record_missing: return "RecordNotFound: Record is not present";
        case fo4::SubrecordRemoveStatus::absent: return "";
        case fo4::SubrecordRemoveStatus::below_minimum:
            return "ElementBelowMinimum: Removal would violate the required subrecord count";
        case fo4::SubrecordRemoveStatus::not_authorable:
            return "ElementNotAuthorable: Subrecord is coupled or has no independent authoring rule";
        case fo4::SubrecordRemoveStatus::unknown_order:
            return "ElementUnknownOrder: Removal cannot be proven safe: the record has subrecords the authoring order does not describe";
    }
    return "ElementRemoveFailed: Unknown removal status";
}

std::size_t occurrencesIn(const fo4::Record& record, fo4::Signature signature) {
    std::size_t count = 0;
    for (const auto& sub : record.subrecords) {
        if (sub.signature == signature) ++count;
    }
    return count;
}

void restoreRecord(fo4::Plugin& plugin, const fo4::Record& snapshot) {
    fo4::Record* found = nullptr;
    fo4::visit_records(plugin, [&](fo4::Record& record) {
        if (found == nullptr && record.header.signature != fo4::sig_tes4 && record.form_key == snapshot.form_key) {
            found = &record;
        }
    });
    if (found != nullptr) *found = snapshot;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::copyElementToSelected(const nlohmann::json& arguments) {
    const auto sourceName = support::stringArgument(arguments, 0);
    const auto sourceText = support::stringArgument(arguments, 1);
    const auto path = support::stringArgument(arguments, 2);
    const auto targetsText = support::stringArgument(arguments, 3);

    const auto element = bulkAuthoringElement(path);
    if (!element) {
        return std::unexpected(Error{"ElementPathUnsupported", "Bulk copy requires an Authoring\\SIG or Authoring\\SIG[n] path"});
    }
    nlohmann::json failures = nlohmann::json::array();
    std::vector<BulkTarget> targets;
    if (!parseBulkTargets(targetsText, targets, failures)) {
        return std::unexpected(Error{"InvalidBulkTargets", "Targets must be a JSON array of {plugin, formKey}"});
    }
    fo4::FormKey sourceKey;
    try {
        sourceKey = fo4::FormKey::parse(sourceText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::vector<std::string_view> owners{sourceName};
    for (const auto& target : targets) owners.push_back(target.plugin);
    OpLock lock(*this, OpLock::Mode::Write, owners);
    const auto* source = findPlugin(sourceName);
    if (source == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + sourceName});
    const auto* sourceRecord = fo4::find_record(source->plugin, sourceKey);
    if (sourceRecord == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + sourceText});
    if (occurrencesIn(*sourceRecord, element->signature) <= element->occurrence) {
        return std::unexpected(Error{"ElementIndexOutOfRange", "Source record has no " + path});
    }

    std::vector<BulkWorking> working;
    std::size_t applied = 0;
    for (const auto& target : targets) {
        try {
            auto* loaded = findPlugin(target.plugin);
            if (loaded == nullptr) {
                failures.push_back(bulkFailure(target, "PluginNotLoaded: Plugin is not loaded: " + target.plugin));
                continue;
            }
            if (const auto guard = writeGuard(*loaded)) {
                failures.push_back(bulkFailure(target, guard->type + ": " + guard->message));
                continue;
            }
            auto& entry = bulkWorkingFor(working, loaded->name, loaded->plugin);
            const auto* current = fo4::find_record(entry.plugin, target.key);
            if (current == nullptr) {
                failures.push_back(bulkFailure(target, copyReason(fo4::SubrecordCopyStatus::record_missing)));
                continue;
            }
            const fo4::Record snapshot = *current;
            fo4::SubrecordCopyStatus status = fo4::SubrecordCopyStatus::record_missing;
            try {
                status = fo4::copy_record_subrecord(
                    entry.plugin, target.key, source->plugin, sourceKey, element->signature, element->occurrence);
            } catch (...) {
                restoreRecord(entry.plugin, snapshot);
                throw;
            }
            if (status == fo4::SubrecordCopyStatus::copied) {
                entry.changed = true;
                ++applied;
                continue;
            }
            restoreRecord(entry.plugin, snapshot);
            failures.push_back(bulkFailure(target, copyReason(status)));
        } catch (const std::exception& error) {
            failures.push_back(bulkFailure(target, std::string("ElementCopyFailed: ") + error.what()));
        }
    }

    for (auto& entry : working) {
        if (!entry.changed) continue;
        try {
            stagePluginMutationLocked(*findPlugin(entry.name), std::move(entry.plugin));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"BulkCopyWriteFailed", error.what()});
        }
    }

    return support::dumpPayload({
        {"applied", applied}, {"skipped", nlohmann::json::array()}, {"failures", std::move(failures)}
    });
}

Result<nlohmann::json> RadiclePluginBackend::removeElementFromSelected(const nlohmann::json& arguments) {
    const auto path = support::stringArgument(arguments, 0);
    const auto targetsText = support::stringArgument(arguments, 1);

    const auto element = bulkAuthoringElement(path);
    if (!element) {
        return std::unexpected(Error{"ElementPathUnsupported", "Bulk removal requires an Authoring\\SIG or Authoring\\SIG[n] path"});
    }
    nlohmann::json failures = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    std::vector<BulkTarget> targets;
    if (!parseBulkTargets(targetsText, targets, failures)) {
        return std::unexpected(Error{"InvalidBulkTargets", "Targets must be a JSON array of {plugin, formKey}"});
    }

    std::vector<std::string_view> owners;
    for (const auto& target : targets) owners.push_back(target.plugin);
    OpLock lock(*this, OpLock::Mode::Write, owners);

    std::vector<BulkWorking> working;
    std::size_t applied = 0;
    for (const auto& target : targets) {
        try {
            auto* loaded = findPlugin(target.plugin);
            if (loaded == nullptr) {
                failures.push_back(bulkFailure(target, "PluginNotLoaded: Plugin is not loaded: " + target.plugin));
                continue;
            }
            if (const auto guard = writeGuard(*loaded)) {
                failures.push_back(bulkFailure(target, guard->type + ": " + guard->message));
                continue;
            }
            auto& entry = bulkWorkingFor(working, loaded->name, loaded->plugin);
            if (fo4::find_record(entry.plugin, target.key) == nullptr) {
                failures.push_back(bulkFailure(target, "RecordNotFound: Record is not present"));
                continue;
            }
            const auto status = fo4::remove_record_subrecord(
                entry.plugin, target.key, element->signature, element->occurrence);
            if (status == fo4::SubrecordRemoveStatus::removed) {
                entry.changed = true;
                ++applied;
            } else if (status == fo4::SubrecordRemoveStatus::absent) {
                skipped.push_back(bulkFailure(target, "Element is not present"));
            } else {
                failures.push_back(bulkFailure(target, removeReason(status)));
            }
        } catch (const std::exception& error) {
            failures.push_back(bulkFailure(target, std::string("ElementRemoveFailed: ") + error.what()));
        }
    }

    for (auto& entry : working) {
        if (!entry.changed) continue;
        try {
            stagePluginMutationLocked(*findPlugin(entry.name), std::move(entry.plugin));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"BulkRemoveWriteFailed", error.what()});
        }
    }

    return support::dumpPayload({
        {"applied", applied}, {"skipped", std::move(skipped)}, {"failures", std::move(failures)}
    });
}

}  // namespace fo4ide::plugin
