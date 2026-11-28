#include "RadiclePluginScriptSupport.hpp"

#include <fo4/plugin/vmad_writer.hpp>
#include <fo4/schema/scalar.hpp>

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace fo4ide::plugin {

using namespace script_support;

namespace {

constexpr const char* k_invalid_alias_id = "Alias ID must be an integer from -32768 to 32767";
constexpr const char* k_value_not_authorable =
    "Quest alias script values are not authorable: the record's VMAD is not a QUST quest";

std::optional<std::int16_t> aliasIdArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    if (value.is_null() || value.is_discarded()) return std::nullopt;
    const auto parsed = support::integerValue<std::int32_t>(value);
    if (!parsed || *parsed < std::numeric_limits<std::int16_t>::min() ||
        *parsed > std::numeric_limits<std::int16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(*parsed);
}

std::optional<fo4::QuestVmadAliasScripts> questAliasFor(const fo4::Record& record, std::int16_t aliasId) {
    if (record.header.signature.to_string() != "QUST") return std::nullopt;
    const auto* vmad = vmadSubrecord(record);
    if (vmad == nullptr) return std::nullopt;
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    const auto extras = fo4::read_quest_vmad_extras(
        std::span<const std::byte>(vmad->data).subspan(vmad->data.size() - scripts.trailing_bytes),
        scripts.object_format);
    for (const auto& alias : extras.aliases) {
        if (alias.alias_id == aliasId) return alias;
    }
    return std::nullopt;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::addAliasScript(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto aliasId = aliasIdArgument(arguments, 2);
    const auto scriptName = support::stringArgument(arguments, 3);
    const auto flags = byteArgument(arguments, 4).value_or(0);
    const auto requestedIndex = sizeArgument(arguments, 5);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!aliasId) return std::unexpected(Error{"AliasIdInvalid", k_invalid_alias_id});
    if (invalidName(scriptName)) return std::unexpected(Error{"InvalidScriptName", k_invalid_script_name});
    if (invalidArgument<std::uint8_t>(arguments, 4)) {
        return std::unexpected(Error{"ScriptFlagsInvalid", "Script flags must be an integer from 0 to 255"});
    }
    if (invalidArgument<std::uint32_t>(arguments, 5)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly",
                                     "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    std::size_t added = 0;
    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto* record = fo4::find_record(mutated, key);
        if (record == nullptr) {
            return std::unexpected(Error{"RecordNotFound",
                                         "Record is not present in " + pluginName + ": " + formKeyText});
        }
        const auto alias = questAliasFor(*record, *aliasId);
        added = requestedIndex.value_or(alias ? alias->scripts.size() : 0);
        const auto status = fo4::add_quest_alias_script(mutated, key, *aliasId, added, scriptName, flags);
        if (const auto failure = vmadFailure(status, "Quest alias script"); !failure.empty()) {
            return std::unexpected(Error{"ScriptWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptWriteFailed", error.what()});
    }

    return nlohmann::json("Added alias " + std::to_string(*aliasId) + " script '" + scriptName +
                          "' at index " + std::to_string(added) + " to " + formKeyText + " in " +
                          pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeAliasScript(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto aliasId = aliasIdArgument(arguments, 2);
    const auto scriptIndex = sizeArgument(arguments, 3);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!aliasId) return std::unexpected(Error{"AliasIdInvalid", k_invalid_alias_id});
    if (invalidArgument<std::uint32_t>(arguments, 3)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly",
                                     "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::remove_quest_alias_script(mutated, key, *aliasId, *scriptIndex);
        if (const auto failure = vmadFailure(status, "Quest alias script"); !failure.empty()) {
            return std::unexpected(Error{"ScriptWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptWriteFailed", error.what()});
    }

    return nlohmann::json("Removed script index " + std::to_string(*scriptIndex) + " from alias " +
                          std::to_string(*aliasId) + " of " + formKeyText + " in " + pluginName +
                          ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::addAliasScriptProperty(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto aliasId = aliasIdArgument(arguments, 2);
    const auto scriptIndex = sizeArgument(arguments, 3);
    const auto propertyName = support::stringArgument(arguments, 4);
    const auto type = byteArgument(arguments, 5);
    const auto flags = byteArgument(arguments, 6).value_or(0);
    const auto requestedIndex = sizeArgument(arguments, 7);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!aliasId) return std::unexpected(Error{"AliasIdInvalid", k_invalid_alias_id});
    if (invalidArgument<std::uint32_t>(arguments, 3)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});
    if (invalidName(propertyName)) {
        return std::unexpected(Error{"InvalidPropertyName", k_invalid_property_name});
    }
    if (invalidArgument<std::uint8_t>(arguments, 5)) {
        return std::unexpected(Error{"PropertyTypeInvalid", "Property type must be an integer from 0 to 255"});
    }
    if (!type) return std::unexpected(Error{"PropertyTypeMissing", "Property type is required"});
    if (invalidArgument<std::uint8_t>(arguments, 6)) {
        return std::unexpected(Error{"PropertyFlagsInvalid", "Property flags must be an integer from 0 to 255"});
    }
    if (invalidArgument<std::uint32_t>(arguments, 7)) {
        return std::unexpected(Error{"PropertyIndexInvalid", "Property index must be an integer from 0 to 4294967295"});
    }
    const auto property = propertyForType(propertyName, *type, flags);
    if (!property) {
        return std::unexpected(Error{"PropertyTypeUnsupported",
                                     "Property type is not authorable; 0 and 16 have no fixed value layout"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly",
                                     "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    std::size_t added = 0;
    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto* record = fo4::find_record(mutated, key);
        if (record == nullptr) {
            return std::unexpected(Error{"RecordNotFound",
                                         "Record is not present in " + pluginName + ": " + formKeyText});
        }
        const auto alias = questAliasFor(*record, *aliasId);
        if (!alias) return std::unexpected(Error{"QuestAliasMissing", "Quest alias is absent from the VMAD"});
        if (*scriptIndex >= alias->scripts.size()) {
            return std::unexpected(Error{"ScriptIndexOutOfRange", "Script index is out of range"});
        }
        added = requestedIndex.value_or(alias->scripts[*scriptIndex].properties.size());
        const auto status = fo4::add_quest_alias_script_property(mutated, key, *aliasId, *scriptIndex,
                                                                 added, *property);
        if (const auto failure = vmadFailure(status, "Quest alias script property"); !failure.empty()) {
            return std::unexpected(Error{"PropertyWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PropertyWriteFailed", error.what()});
    }

    return nlohmann::json("Added property '" + propertyName + "' at index " + std::to_string(added) +
                          " on script " + std::to_string(*scriptIndex) + " of alias " +
                          std::to_string(*aliasId) + " of " + formKeyText + " in " + pluginName +
                          ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeAliasScriptProperty(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto aliasId = aliasIdArgument(arguments, 2);
    const auto scriptIndex = sizeArgument(arguments, 3);
    const auto propertyIndex = sizeArgument(arguments, 4);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!aliasId) return std::unexpected(Error{"AliasIdInvalid", k_invalid_alias_id});
    if (invalidArgument<std::uint32_t>(arguments, 3)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});
    if (invalidArgument<std::uint32_t>(arguments, 4)) {
        return std::unexpected(Error{"PropertyIndexInvalid", "Property index must be an integer from 0 to 4294967295"});
    }
    if (!propertyIndex) return std::unexpected(Error{"PropertyIndexMissing", "Property index is required"});

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly",
                                     "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::remove_quest_alias_script_property(mutated, key, *aliasId, *scriptIndex,
                                                                    *propertyIndex);
        if (const auto failure = vmadFailure(status, "Quest alias script property"); !failure.empty()) {
            return std::unexpected(Error{"PropertyWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PropertyWriteFailed", error.what()});
    }

    return nlohmann::json("Removed property index " + std::to_string(*propertyIndex) + " from script " +
                          std::to_string(*scriptIndex) + " of alias " + std::to_string(*aliasId) + " of " +
                          formKeyText + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::setAliasScriptValue(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto aliasId = aliasIdArgument(arguments, 2);
    const auto scriptIndex = sizeArgument(arguments, 3);
    const auto propertyIndex = sizeArgument(arguments, 4);
    const auto kind = support::stringArgument(arguments, 5);
    const auto rawValue = support::argumentAt(arguments, 6);
    const auto rawAlias = support::argumentAt(arguments, 7);
    const auto rawElement = support::argumentAt(arguments, 8);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!aliasId) return std::unexpected(Error{"AliasIdInvalid", k_invalid_alias_id});
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});
    if (!propertyIndex) return std::unexpected(Error{"PropertyIndexMissing", "Property index is required"});
    if (kind != "object" && kind != "string" && kind != "int" && kind != "float" && kind != "bool") {
        return std::unexpected(Error{"InvalidPropertyValue",
                                     "kind must be object, string, int, float or bool"});
    }
    std::size_t element = 0;
    if (!rawElement.is_null() && !rawElement.is_discarded()) {
        const auto parsed = support::integerValue<std::uint32_t>(rawElement);
        if (!parsed) {
            return std::unexpected(Error{"InvalidPropertyValue", "element must be an unsigned integer"});
        }
        element = static_cast<std::size_t>(*parsed);
    }
    std::int16_t alias = -1;
    if (!rawAlias.is_null() && !rawAlias.is_discarded()) {
        const auto parsed = support::integerValue<std::int32_t>(rawAlias);
        if (!parsed || *parsed < std::numeric_limits<std::int16_t>::min() ||
            *parsed > std::numeric_limits<std::int16_t>::max()) {
            return std::unexpected(Error{"InvalidPropertyValue", "alias must fit a signed 16-bit value"});
        }
        alias = static_cast<std::int16_t>(*parsed);
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly",
                                     "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto* record = fo4::find_record(mutated, key);
        if (record == nullptr) {
            return std::unexpected(Error{"RecordNotFound",
                                         "Record is not present in " + pluginName + ": " + formKeyText});
        }
        if (record->header.signature.to_string() != "QUST") {
            return std::unexpected(Error{"ScriptValueNotAuthorable", k_value_not_authorable});
        }
        const auto quest = questAliasFor(*record, *aliasId);
        if (!quest) return std::unexpected(Error{"QuestAliasMissing", "Quest alias is absent from the VMAD"});
        if (*scriptIndex >= quest->scripts.size()) {
            return std::unexpected(Error{"ScriptIndexOutOfRange", "Script index is out of range"});
        }
        const auto& properties = quest->scripts[*scriptIndex].properties;
        if (*propertyIndex >= properties.size()) {
            return std::unexpected(Error{"PropertyIndexOutOfRange", "Property index is out of range"});
        }
        const auto& property = properties[*propertyIndex];
        const auto baseType = baseVmadType(property.type);
        if (!baseType) {
            return std::unexpected(Error{"PropertyTypeUnsupported",
                                         "VMAD property type is not one of 1-5 or 11-15"});
        }
        if (kind != baseTypeName(*baseType)) {
            return std::unexpected(Error{
                "PropertyTypeMismatch",
                "Property '" + property.name + "' is " + baseTypeName(*baseType) + ", not " + kind});
        }
        const bool array = property.type >= 11 && property.type <= 15;
        if (array) {
            if (!property.element_count || element >= *property.element_count) {
                return std::unexpected(Error{"ElementIndexOutOfRange",
                                             "Element index is out of range for this array property"});
            }
        } else if (element != 0) {
            return std::unexpected(Error{"InvalidPropertyValue", "A scalar property only has element 0"});
        }
        if (!rawAlias.is_null() && !rawAlias.is_discarded() && kind != "object") {
            return std::unexpected(Error{"InvalidPropertyValue", "alias only applies to an object property"});
        }

        if ((rawValue.is_null() || rawValue.is_discarded()) && kind != "object") {
            return std::unexpected(Error{"InvalidPropertyValue", kind + " kind requires a value"});
        }
        const auto text = rawValue.is_null() || rawValue.is_discarded() ? std::string{}
                                                                        : support::stringArgument(arguments, 6);
        fo4::VmadValue value;
        if (kind == "object") {
            fo4::FormKey target{};
            if (!text.empty()) {
                try {
                    target = fo4::FormKey::parse(text);
                } catch (const std::exception& error) {
                    return std::unexpected(Error{"InvalidFormKey", error.what()});
                }
            }
            value = fo4::VmadObjectValue{target, alias};
        } else if (kind == "string") {
            if (text.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(text)) {
                return std::unexpected(Error{"InvalidPropertyValue",
                                             "string value must be valid UTF-8 without NUL bytes"});
            }
            value = text;
        } else if (kind == "int") {
            const auto parsed = parseIntText(text);
            if (!parsed) {
                return std::unexpected(Error{"InvalidPropertyValue", "int value must fit a signed 32-bit integer"});
            }
            value = *parsed;
        } else if (kind == "float") {
            const auto parsed = parseFloatText(text);
            if (!parsed) {
                return std::unexpected(Error{"InvalidPropertyValue", "float value must be a finite number"});
            }
            value = *parsed;
        } else {
            const auto parsed = parseBoolText(text);
            if (!parsed) {
                return std::unexpected(Error{"InvalidPropertyValue", "bool value must be true or false"});
            }
            value = *parsed;
        }

        const auto status = fo4::set_quest_alias_script_value(mutated, key, *aliasId, *scriptIndex,
                                                              *propertyIndex, value, element);
        if (status == fo4::VmadStructureMutationStatus::not_authorable) {
            return std::unexpected(Error{"ScriptValueNotAuthorable", k_value_not_authorable});
        }
        if (status == fo4::VmadStructureMutationStatus::record_missing) {
            return std::unexpected(Error{"ScriptValueWriteFailed", "Record is not present in the plugin"});
        }
        if (status != fo4::VmadStructureMutationStatus::updated &&
            status != fo4::VmadStructureMutationStatus::unchanged) {
            return std::unexpected(Error{"ScriptValueWriteFailed",
                                         "Quest alias script property value slot or index is absent from the VMAD"});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const fo4::schema::ScalarError& error) {
        return std::unexpected(Error{"PropertyTypeMismatch", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptValueWriteFailed", error.what()});
    }

    return nlohmann::json("Set " + kind + " value of property index " + std::to_string(*propertyIndex) +
                          " on script " + std::to_string(*scriptIndex) + " of alias " +
                          std::to_string(*aliasId) + " of " + formKeyText + " in " + pluginName +
                          ". SavePlugin to persist.");
}

}  // namespace fo4ide::plugin
