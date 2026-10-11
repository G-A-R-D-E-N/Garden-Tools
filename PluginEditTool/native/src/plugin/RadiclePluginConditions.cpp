#include "RadiclePluginSupport.hpp"

#include <exception>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/schema/condition_scopes.hpp>
#include <fo4/schema/ctda.hpp>

namespace fo4ide::plugin {
using namespace support;

namespace {

Error rootConditionError(fo4::schema::ScopedConditionStatus status, const std::string& recordId) {
    if (status == fo4::schema::ScopedConditionStatus::alias_structure_invalid) {
        return Error{"ConditionAliasMalformed", "Record '" + recordId + "' has a malformed alias array."};
    }
    if (status == fo4::schema::ScopedConditionStatus::unrepresentable) {
        return Error{"ConditionPayloadUnsupported", "Condition bytes cannot round-trip on record '" + recordId + "'."};
    }
    return Error{"ConditionScopeMultipleLists", "Record '" + recordId + "' has more than one condition list."};
}

}

Result<nlohmann::json> RadiclePluginBackend::getConditions(
    const nlohmann::json& arguments) const {
    const auto pluginName = stringArgument(arguments, 0);
    const auto recordId = stringArgument(arguments, 1);
    if (recordId.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record ID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const LoadedPlugin* loaded = nullptr;
    if (!pluginName.empty()) {
        loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{
                "PluginNotLoaded",
                "Plugin is not loaded: " + pluginName
            });
        }
    }

    const auto resolveRecord = [&](const LoadedPlugin& candidate)
        -> const fo4::Record* {
        const auto& plugin = candidate.plugin;
        try {
            if (const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(recordId))) {
                return record;
            }
        } catch (const std::exception&) {
        }
        for (const auto* record : fo4::collect_records(plugin)) {
            if (editorId(*record) == recordId) return record;
        }
        return nullptr;
    };

    const fo4::Record* record = nullptr;
    const LoadedPlugin* owner = loaded;
    if (loaded != nullptr) {
        record = resolveRecord(*loaded);
    } else {
        for (const auto& candidate : plugins_) {
            record = resolveRecord(candidate);
            if (record != nullptr) {
                owner = &candidate;
                break;
            }
        }
    }
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record '" + recordId + "' not found."
        });
    }

    const auto root = fo4::schema::read_root_conditions(*record);
    if (!root.ok) return std::unexpected(rootConditionError(root.error, recordId));
    nlohmann::json outp = nlohmann::json::array();
    for (const auto& condition : root.conditions) {
        outp.push_back(describeCondition(condition, owner->plugin));
    }
    return nlohmann::json(outp.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getConditionsAtPath(
    const nlohmann::json& arguments) const {
    const auto pluginName = stringArgument(arguments, 0);
    const auto recordId = stringArgument(arguments, 1);
    const auto path = stringArgument(arguments, 2);
    if (recordId.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record ID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const LoadedPlugin* loaded = nullptr;
    if (!pluginName.empty()) {
        loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{
                "PluginNotLoaded",
                "Plugin is not loaded: " + pluginName
            });
        }
    }

    const auto resolveRecord = [&](const LoadedPlugin& candidate)
        -> const fo4::Record* {
        const auto& plugin = candidate.plugin;
        try {
            if (const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(recordId))) {
                return record;
            }
        } catch (const std::exception&) {
        }
        for (const auto* record : fo4::collect_records(plugin)) {
            if (editorId(*record) == recordId) return record;
        }
        return nullptr;
    };

    const fo4::Record* record = nullptr;
    const LoadedPlugin* owner = loaded;
    if (loaded != nullptr) {
        record = resolveRecord(*loaded);
    } else {
        for (const auto& candidate : plugins_) {
            record = resolveRecord(candidate);
            if (record != nullptr) {
                owner = &candidate;
                break;
            }
        }
    }
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record '" + recordId + "' not found."
        });
    }

    const auto scoped =
        fo4::schema::scoped_condition_path(record->header.signature, path);
    if (!scoped.has_value()) {
        return std::unexpected(Error{
            "ConditionPathUnsupported",
            "Native condition editing does not represent path '" + path +
                "' on " + record->header.signature.to_string() + " records"
        });
    }

    const auto conditions = fo4::schema::read_scoped_conditions(
        *record, scoped->descriptor, scoped->container_index);
    if (!conditions.ok) {
        if (conditions.error == fo4::schema::ScopedConditionStatus::unrepresentable) {
            return std::unexpected(Error{"ConditionPayloadUnsupported", "Condition bytes cannot round-trip in this scope"});
        }
        if (conditions.error == fo4::schema::ScopedConditionStatus::multiple_lists) {
            return std::unexpected(Error{
                "ConditionScopeMultipleLists",
                "Scope '" + path + "' has more than one condition list on record '" + recordId + "'."
            });
        }
        if (conditions.error == fo4::schema::ScopedConditionStatus::element_not_conditioned) {
            return std::unexpected(Error{
                "ConditionScopeNotConditioned",
                "'" + path + "' addresses an element that carries no conditions on record '" +
                    recordId + "'"
            });
        }
        if (conditions.error == fo4::schema::ScopedConditionStatus::alias_structure_invalid) {
            return std::unexpected(Error{
                "ConditionAliasMalformed",
                "Record '" + recordId + "' has a malformed alias array for path '" + path + "'"
            });
        }
        return std::unexpected(Error{
            "ConditionScopeOutOfRange",
            "Container index for '" + path + "' is out of range on record '" +
                recordId + "'"
        });
    }

    const auto* context = owner != nullptr ? owner : &plugins_.front();
    nlohmann::json outp = nlohmann::json::array();
    for (const auto& condition : conditions.conditions) {
        outp.push_back(describeCondition(condition, context->plugin));
    }
    return nlohmann::json(outp.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getConditionFunctions() const {
    return nlohmann::json(fo4ide::plugin::ConditionFunctions::asJson());
}

Result<nlohmann::json> RadiclePluginBackend::getConditionRunOnTypes() const {
    return nlohmann::json(fo4ide::plugin::conditionRunOnNames());
}

Result<nlohmann::json> RadiclePluginBackend::setConditions(
    const nlohmann::json& arguments) {
    const auto pluginName = stringArgument(arguments, 0);
    const auto recordId = stringArgument(arguments, 1);
    const auto conditionsJson = stringArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordId.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record FormKey is empty"});
    }
    if (conditionsJson.empty()) {
        return std::unexpected(Error{
            "InvalidConditionsJson",
            "conditions JSON is empty; pass [] to clear"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    const auto resolveEditorId = [&](std::string_view text)
        -> std::optional<fo4::RawFormId> {
        for (const auto& candidate : plugins_) {
            for (const auto* record : fo4::collect_records(candidate.plugin)) {
                const auto id = editorId(*record);
                if (id.empty()) continue;
                std::string lowerId = id;
                std::string lowerText(text);
                std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if (lowerId == lowerText) {
                    try {
                        const auto raw = fo4::encode_plugin_form_id(
                            record->form_key, fo4::plugin_form_id_context(candidate.plugin));
                        return fo4::RawFormId{raw.value()};
                    } catch (const std::exception&) {
                        return std::nullopt;
                    }
                }
            }
        }
        return std::nullopt;
    };

    nlohmann::json document;
    std::size_t writtenCount = 0;
    try {
        document = nlohmann::json::parse(conditionsJson);
        if (!document.is_array()) {
            return std::unexpected(Error{"InvalidConditionsJson", "Could not parse conditions JSON: expected an array."});
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidConditionsJson", "Could not parse conditions JSON: " + std::string(error.what())});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(recordId);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto* record = fo4::find_record(mutated, key);
        if (record == nullptr) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        const auto root = fo4::schema::read_root_conditions(*record);
        if (!root.ok) return std::unexpected(rootConditionError(root.error, recordId));
        std::string failure;
        const auto built = buildConditions(document, loaded->plugin, root.conditions, resolveEditorId, failure);
        if (!built) return std::unexpected(Error{"InvalidConditionsJson", failure});
        writtenCount = built->size();
        const auto status = fo4::set_record_conditions(
            mutated, key, std::span<const fo4::CtdaCondition>{*built});
        if (status == fo4::ConditionMutationStatus::multiple_lists) {
            return std::unexpected(Error{
                "ConditionScopeMultipleLists",
                "Record '" + recordId + "' has more than one condition list."
            });
        }
        if (status == fo4::ConditionMutationStatus::alias_structure_invalid) {
            return std::unexpected(rootConditionError(
                fo4::schema::ScopedConditionStatus::alias_structure_invalid, recordId));
        }
        if (status == fo4::ConditionMutationStatus::no_condition_list) {
            return std::unexpected(Error{
                "NoConditionList",
                "Record '" + recordId + "' has no condition list to replace."
            });
        }
        if (status == fo4::ConditionMutationStatus::record_missing) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        if (status == fo4::ConditionMutationStatus::updated) {
            stagePluginMutationLocked(*loaded, std::move(mutated));
        }
    } catch (const fo4::CtdaMultipleListsError&) {
        return std::unexpected(Error{"ConditionScopeMultipleLists", "Record '" + recordId + "' has more than one condition list."});
    } catch (const fo4::CtdaNotLosslessError& error) {
        return std::unexpected(Error{"ConditionPayloadUnsupported", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetConditionsFailed", error.what()});
    }

    auto message = "Set " + std::to_string(writtenCount) +
                   " condition(s) on " + recordId + " in " + pluginName + ".";
    message += " SavePlugin to persist.";
    return nlohmann::json(std::move(message));
}

}
