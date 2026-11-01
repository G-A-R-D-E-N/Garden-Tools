#include "RadiclePluginSupport.hpp"

#include <fo4/plugin/vmad_writer.hpp>
#include <fo4/schema/scalar.hpp>

#include <cstddef>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

namespace {

constexpr const char* k_invalid_script_name = "Script name is empty, contains a NUL byte or is not valid UTF-8";
constexpr const char* k_invalid_property_name =
    "Script property name is empty, contains a NUL byte or is not valid UTF-8";

std::string vmadFailure(fo4::VmadStructureMutationStatus status, const std::string& what) {
    switch (status) {
        case fo4::VmadStructureMutationStatus::updated:
        case fo4::VmadStructureMutationStatus::unchanged:
            return {};
        case fo4::VmadStructureMutationStatus::already_present:
            return what + " is already present and a second copy is refused";
        case fo4::VmadStructureMutationStatus::record_missing:
            return "Record is not present in the plugin";
        case fo4::VmadStructureMutationStatus::subrecord_missing:
            return "Record has no VMAD subrecord";
        case fo4::VmadStructureMutationStatus::binding_missing:
            return what + " binding is absent from the VMAD";
        case fo4::VmadStructureMutationStatus::index_out_of_range:
            return what + " index is out of range";
        case fo4::VmadStructureMutationStatus::not_authorable:
            return what + " structure edits are not authorable on this record type";
    }
    return what + " structure edit reported an unknown status";
}

std::optional<std::uint8_t> byteArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    if (value.is_null() || value.is_discarded()) return std::nullopt;
    return support::integerValue<std::uint8_t>(value);
}

std::optional<std::size_t> sizeArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    if (value.is_null() || value.is_discarded()) return std::nullopt;
    return support::integerValue<std::uint32_t>(value);
}

template <class T>
bool invalidArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    return !value.is_null() && !value.is_discarded() && !support::integerValue<T>(value).has_value();
}

bool invalidName(const std::string& name) {
    return name.empty() || name.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(name);
}

std::optional<fo4::VmadProperty> propertyForType(std::string name, std::uint8_t type,
                                                std::uint8_t flags) {
    fo4::VmadProperty property;
    property.name = std::move(name);
    property.type = type;
    property.flags = flags;
    switch (type) {
        case 1:
            property.object = fo4::RawFormId{0u};
            property.alias = static_cast<std::int16_t>(-1);
            return property;
        case 2:
            property.text = std::string{};
            return property;
        case 3:
            property.integer = std::int32_t{0};
            return property;
        case 4:
            property.real = 0.0f;
            return property;
        case 5:
            property.boolean = false;
            return property;
        case 7:
            property.struct_elements = std::vector<fo4::VmadStruct>{fo4::VmadStruct{}};
            return property;
        case 11:
            property.element_count = 1u;
            property.object_elements =
                std::vector<fo4::VmadObjectElement>{{fo4::RawFormId{0u}, static_cast<std::int16_t>(-1)}};
            return property;
        case 12:
            property.element_count = 1u;
            property.text_elements = std::vector<std::string>{std::string{}};
            return property;
        case 13:
            property.element_count = 1u;
            property.integer_elements = std::vector<std::int32_t>{std::int32_t{0}};
            return property;
        case 14:
            property.element_count = 1u;
            property.real_elements = std::vector<float>{0.0f};
            return property;
        case 15:
            property.element_count = 1u;
            property.boolean_elements = std::vector<bool>{false};
            return property;
        case 17:
            property.element_count = 1u;
            property.struct_elements = std::vector<fo4::VmadStruct>{fo4::VmadStruct{}};
            return property;
        default:
            return std::nullopt;
    }
}

const fo4::Subrecord* vmadSubrecord(const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == "VMAD") return &sub;
    }
    return nullptr;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::addScript(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto scriptName = support::stringArgument(arguments, 2);
    const auto flags = byteArgument(arguments, 3).value_or(0);
    const auto requestedIndex = sizeArgument(arguments, 4);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (invalidName(scriptName)) return std::unexpected(Error{"InvalidScriptName", k_invalid_script_name});
    if (invalidArgument<std::uint8_t>(arguments, 3)) {
        return std::unexpected(Error{"ScriptFlagsInvalid", "Script flags must be an integer from 0 to 255"});
    }
    if (invalidArgument<std::uint32_t>(arguments, 4)) {
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
        std::size_t count = 0;
        if (const auto* vmad = vmadSubrecord(*record)) {
            count = fo4::read_vmad_scripts(vmad->data).scripts.size();
        } else if (const auto status = fo4::add_vmad(mutated, key);
                   status != fo4::VmadStructureMutationStatus::updated) {
            return std::unexpected(Error{"ScriptWriteFailed", vmadFailure(status, "VMAD")});
        }
        added = requestedIndex.value_or(count);
        const auto status = fo4::add_vmad_script(mutated, key, added, scriptName, flags);
        if (const auto failure = vmadFailure(status, "VMAD script"); !failure.empty()) {
            return std::unexpected(Error{"ScriptWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptWriteFailed", error.what()});
    }

    return nlohmann::json("Added script '" + scriptName + "' at index " + std::to_string(added) +
                          " to " + formKeyText + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeScript(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto scriptIndex = sizeArgument(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (invalidArgument<std::uint32_t>(arguments, 2)) {
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
        const auto status = fo4::remove_vmad_script(mutated, key, *scriptIndex);
        if (const auto failure = vmadFailure(status, "VMAD script"); !failure.empty()) {
            return std::unexpected(Error{"ScriptWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptWriteFailed", error.what()});
    }

    return nlohmann::json("Removed script index " + std::to_string(*scriptIndex) + " from " +
                          formKeyText + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::addScriptProperty(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto scriptIndex = sizeArgument(arguments, 2);
    const auto propertyName = support::stringArgument(arguments, 3);
    const auto type = byteArgument(arguments, 4);
    const auto flags = byteArgument(arguments, 5).value_or(0);
    const auto requestedIndex = sizeArgument(arguments, 6);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (invalidArgument<std::uint32_t>(arguments, 2)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});
    if (invalidName(propertyName)) {
        return std::unexpected(Error{"InvalidPropertyName", k_invalid_property_name});
    }
    if (invalidArgument<std::uint8_t>(arguments, 4)) {
        return std::unexpected(Error{"PropertyTypeInvalid", "Property type must be an integer from 0 to 255"});
    }
    if (!type) return std::unexpected(Error{"PropertyTypeMissing", "Property type is required"});
    if (invalidArgument<std::uint8_t>(arguments, 5)) {
        return std::unexpected(Error{"PropertyFlagsInvalid", "Property flags must be an integer from 0 to 255"});
    }
    if (invalidArgument<std::uint32_t>(arguments, 6)) {
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
        const auto* vmad = vmadSubrecord(*record);
        if (vmad == nullptr) {
            return std::unexpected(Error{"VmadMissing",
                                         "Record has no VMAD subrecord; add a script first"});
        }
        const auto scripts = fo4::read_vmad_scripts(vmad->data);
        if (*scriptIndex >= scripts.scripts.size()) {
            return std::unexpected(Error{"ScriptIndexOutOfRange", "Script index is out of range"});
        }
        const auto count = scripts.scripts[*scriptIndex].properties.size();
        added = requestedIndex.value_or(count);
        const auto status = fo4::add_vmad_property(mutated, key, *scriptIndex, added, *property);
        if (const auto failure = vmadFailure(status, "VMAD property"); !failure.empty()) {
            return std::unexpected(Error{"PropertyWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PropertyWriteFailed", error.what()});
    }

    return nlohmann::json("Added property '" + propertyName + "' at index " + std::to_string(added) +
                          " on script " + std::to_string(*scriptIndex) + " of " + formKeyText +
                          " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeScriptProperty(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto scriptIndex = sizeArgument(arguments, 2);
    const auto propertyIndex = sizeArgument(arguments, 3);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (invalidArgument<std::uint32_t>(arguments, 2)) {
        return std::unexpected(Error{"ScriptIndexInvalid", "Script index must be an integer from 0 to 4294967295"});
    }
    if (!scriptIndex) return std::unexpected(Error{"ScriptIndexMissing", "Script index is required"});
    if (invalidArgument<std::uint32_t>(arguments, 3)) {
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
        const auto status = fo4::remove_vmad_property(mutated, key, *scriptIndex, *propertyIndex);
        if (const auto failure = vmadFailure(status, "VMAD property"); !failure.empty()) {
            return std::unexpected(Error{"PropertyWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PropertyWriteFailed", error.what()});
    }

    return nlohmann::json("Removed property index " + std::to_string(*propertyIndex) +
                          " from script " + std::to_string(*scriptIndex) + " of " + formKeyText +
                          " in " + pluginName + ". SavePlugin to persist.");
}

namespace {

std::string scalarValueFailure(fo4::ScalarMutationStatus status, const std::string& what) {
    switch (status) {
        case fo4::ScalarMutationStatus::updated:
        case fo4::ScalarMutationStatus::unchanged:
            return {};
        case fo4::ScalarMutationStatus::record_missing:
            return "Record is not present in the plugin";
        case fo4::ScalarMutationStatus::subrecord_missing:
            return what + " value slot or index is absent from the VMAD";
        case fo4::ScalarMutationStatus::next_alias_id_too_low:
            return "VMAD alias id counter is exhausted";
    }
    return what + " write reported an unknown status";
}

std::optional<std::int32_t> parseIntText(std::string_view text) {
    std::int32_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

std::optional<float> parseFloatText(std::string_view text) {
    std::string owned{text};
    char* end = nullptr;
    const auto parsed = std::strtof(owned.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}

std::optional<bool> parseBoolText(std::string_view text) {
    if (text == "true") return true;
    if (text == "false") return false;
    return std::nullopt;
}

std::optional<std::uint8_t> baseVmadType(std::uint8_t type) {
    if (type >= 11 && type <= 15) return static_cast<std::uint8_t>(type - 10);
    if (type >= 1 && type <= 5) return type;
    return std::nullopt;
}

const char* baseTypeName(std::uint8_t type) {
    switch (type) {
        case 1: return "object";
        case 2: return "string";
        case 3: return "int";
        case 4: return "float";
        case 5: return "bool";
        default: return "unknown";
    }
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::setScriptValue(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto scriptIndex = sizeArgument(arguments, 2);
    const auto propertyIndex = sizeArgument(arguments, 3);
    const auto kind = support::stringArgument(arguments, 4);
    const auto rawValue = support::argumentAt(arguments, 5);
    const auto rawAlias = support::argumentAt(arguments, 6);
    const auto rawElement = support::argumentAt(arguments, 7);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
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
        const auto* vmad = vmadSubrecord(*record);
        if (vmad == nullptr) {
            return std::unexpected(Error{"VmadMissing",
                                         "Record has no VMAD subrecord; add a script first"});
        }
        const auto scripts = fo4::read_vmad_scripts(vmad->data);
        if (*scriptIndex >= scripts.scripts.size()) {
            return std::unexpected(Error{"ScriptIndexOutOfRange", "Script index is out of range"});
        }
        const auto& properties = scripts.scripts[*scriptIndex].properties;
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
            return std::unexpected(Error{"InvalidPropertyValue",
                                         "A scalar property only has element 0"});
        }
        if (!rawAlias.is_null() && !rawAlias.is_discarded() && kind != "object") {
            return std::unexpected(Error{"InvalidPropertyValue",
                                         "alias only applies to an object property"});
        }

        if ((rawValue.is_null() || rawValue.is_discarded()) && kind != "object") {
            return std::unexpected(Error{"InvalidPropertyValue", kind + " kind requires a value"});
        }
        const auto text = rawValue.is_null() || rawValue.is_discarded() ? std::string{}
                                                                        : support::stringArgument(arguments, 5);
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
            if (rawValue.is_null() || rawValue.is_discarded()) {
                return std::unexpected(Error{"InvalidPropertyValue", "string kind requires a value"});
            }
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

        const auto status = fo4::set_vmad_property(mutated, key, *scriptIndex, *propertyIndex,
                                                   value, element);
        if (const auto failure = scalarValueFailure(status, "VMAD property"); !failure.empty()) {
            return std::unexpected(Error{"ScriptValueWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const fo4::schema::ScalarError& error) {
        return std::unexpected(Error{"PropertyTypeMismatch", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ScriptValueWriteFailed", error.what()});
    }

    return nlohmann::json("Set " + kind + " value of property index " + std::to_string(*propertyIndex) +
                          " on script " + std::to_string(*scriptIndex) + " of " + formKeyText + " in " +
                          pluginName + ". SavePlugin to persist.");
}

}  // namespace fo4ide::plugin
