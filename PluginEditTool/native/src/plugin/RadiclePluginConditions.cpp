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
    if (loaded != nullptr) {
        record = resolveRecord(*loaded);
    } else {
        for (const auto& candidate : plugins_) {
            record = resolveRecord(candidate);
            if (record != nullptr) break;
        }
    }
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record '" + recordId + "' not found."
        });
    }

    const auto conditions = fo4::read_ctda_conditions(*record);
    const auto* owner = loaded != nullptr ? loaded : &plugins_.front();
    nlohmann::json outp = nlohmann::json::array();
    for (const auto& condition : conditions) {
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

    std::vector<fo4::CtdaCondition> built;
    std::vector<std::string> failures;
    try {
        nlohmann::json document = nlohmann::json::parse(
            conditionsJson.empty() ? "[]" : conditionsJson);
        if (!document.is_array()) {
            return std::unexpected(Error{
                "InvalidConditionsJson",
                "Could not parse conditions JSON: expected an array."
            });
        }
        std::size_t index = 0;
        for (const auto& element : document) {
            index++;
            std::string failure;
            auto condition = buildCondition(element, loaded->plugin, resolveEditorId, failure);
            if (!condition.has_value()) {
                failures.push_back("#" + std::to_string(index) + ": " + failure);
                continue;
            }
            built.push_back(std::move(*condition));
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{
            "InvalidConditionsJson",
            "Could not parse conditions JSON: " + std::string(error.what())
        });
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
        if (fo4::find_record(mutated, key) == nullptr) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        const auto status = fo4::set_record_conditions(
            mutated, key, std::span<const fo4::CtdaCondition>{built});
        if (status == fo4::ConditionMutationStatus::multiple_lists) {
            return std::unexpected(Error{
                "ConditionScopeMultipleLists",
                "Record '" + recordId + "' has more than one condition list."
            });
        }
        if (status == fo4::ConditionMutationStatus::record_missing) {
            return std::unexpected(Error{
                "NoConditionList",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetConditionsFailed", error.what()});
    }

    auto message = "Set " + std::to_string(built.size()) +
                   " condition(s) on " + recordId + " in " + pluginName + ".";
    if (!failures.empty()) {
        message += " Skipped " + std::to_string(failures.size()) + ": ";
        for (std::size_t i = 0; i < failures.size(); ++i) {
            if (i > 0) message += "; ";
            message += failures[i];
        }
        message += ".";
    }
    message += " SavePlugin to persist.";
    return nlohmann::json(std::move(message));
}

}
