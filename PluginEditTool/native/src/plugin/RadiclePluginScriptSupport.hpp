#pragma once

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
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4ide::plugin::script_support {

constexpr const char* k_invalid_script_name = "Script name is empty, contains a NUL byte or is not valid UTF-8";
constexpr const char* k_invalid_property_name =
    "Script property name is empty, contains a NUL byte or is not valid UTF-8";

inline std::string vmadFailure(fo4::VmadStructureMutationStatus status, const std::string& what) {
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

inline std::optional<std::uint8_t> byteArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    if (value.is_null() || value.is_discarded()) return std::nullopt;
    return support::integerValue<std::uint8_t>(value);
}

inline std::optional<std::size_t> sizeArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    if (value.is_null() || value.is_discarded()) return std::nullopt;
    return support::integerValue<std::uint32_t>(value);
}

template <class T>
inline bool invalidArgument(const nlohmann::json& args, std::size_t index) {
    const auto value = support::argumentAt(args, index);
    return !value.is_null() && !value.is_discarded() && !support::integerValue<T>(value).has_value();
}

inline bool invalidName(const std::string& name) {
    return name.empty() || name.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(name);
}

inline std::optional<fo4::VmadProperty> propertyForType(std::string name, std::uint8_t type,
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

inline const fo4::Subrecord* vmadSubrecord(const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == "VMAD") return &sub;
    }
    return nullptr;
}

inline std::string scalarValueFailure(fo4::ScalarMutationStatus status, const std::string& what) {
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

inline std::optional<std::int32_t> parseIntText(std::string_view text) {
    std::int32_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

inline std::optional<float> parseFloatText(std::string_view text) {
    std::string owned{text};
    char* end = nullptr;
    const auto parsed = std::strtof(owned.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}

inline std::optional<bool> parseBoolText(std::string_view text) {
    if (text == "true") return true;
    if (text == "false") return false;
    return std::nullopt;
}

inline std::optional<std::uint8_t> baseVmadType(std::uint8_t type) {
    if (type >= 11 && type <= 15) return static_cast<std::uint8_t>(type - 10);
    if (type >= 1 && type <= 5) return type;
    return std::nullopt;
}

inline const char* baseTypeName(std::uint8_t type) {
    switch (type) {
        case 1: return "object";
        case 2: return "string";
        case 3: return "int";
        case 4: return "float";
        case 5: return "bool";
        default: return "unknown";
    }
}

}  // namespace fo4ide::plugin::script_support
