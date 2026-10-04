#include "RadiclePluginSupport.hpp"

#include <exception>
#include <string>

#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>
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

}
