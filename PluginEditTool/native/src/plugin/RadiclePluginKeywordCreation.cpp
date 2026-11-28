#include "RadiclePluginKeywordCreation.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/schema/scalar.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin::keyword_creation {

namespace {

constexpr std::string_view k_invalid =
    "KYWD takes an optional color (\"RRGGBB\", \"RRGGBBAA\" or [r, g, b, a]) and an optional "
    "type (a schema enum name such as \"Sound\" or an integer 0-18); source:Radicle/schema/fallout4/fields.json";

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

fo4::schema::SubrecordAuthoringConstraint* findRule(
    std::vector<fo4::schema::SubrecordAuthoringConstraint>& rules,
    std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (auto& rule : rules) {
        if (rule.signature == wanted) return &rule;
    }
    return nullptr;
}

bool hexByte(std::string_view text, std::size_t offset, std::uint8_t& value) {
    std::uint8_t parsed = 0;
    const auto [end, error] =
        std::from_chars(text.data() + offset, text.data() + offset + 2, parsed, 16);
    if (error != std::errc{} || end != text.data() + offset + 2) return false;
    value = parsed;
    return true;
}

Result<std::array<std::uint8_t, 4>> colorBytes(const nlohmann::json& value) {
    std::array<std::uint8_t, 4> bytes{0, 0, 0, 255};
    if (value.is_string()) {
        auto text = value.get<std::string>();
        if (!text.empty() && text.front() == '#') text.erase(text.begin());
        if (text.size() != 6 && text.size() != 8) {
            return std::unexpected(Error{"InvalidRecordCreationPayload", std::string(k_invalid)});
        }
        for (std::size_t channel = 0; channel < text.size() / 2; ++channel) {
            if (!hexByte(text, channel * 2, bytes[channel])) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", std::string(k_invalid)});
            }
        }
        return bytes;
    }
    if (!value.is_array() || value.size() < 3 || value.size() > 4) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", std::string(k_invalid)});
    }
    for (std::size_t channel = 0; channel < value.size(); ++channel) {
        const auto byte = support::integerValue<std::uint16_t>(value[channel]);
        if (!byte || *byte > 255) {
            return std::unexpected(Error{"InvalidRecordCreationPayload", std::string(k_invalid)});
        }
        bytes[channel] = static_cast<std::uint8_t>(*byte);
    }
    return bytes;
}

Result<std::uint32_t> typeValue(const nlohmann::json& value) {
    const auto field = fo4::schema::scalar_field_descriptor(
        fo4::Signature::from_string("KYWD"), "KYWD\\Type");
    if (!field.has_value()) {
        return std::unexpected(Error{
            "RecordCreationRequiresSchema", "KYWD Type is not in the authoring schema"});
    }
    const auto& descriptor = field->scalar;
    std::optional<std::int64_t> resolved;
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        const auto wanted = lowered(text);
        for (const auto& name : descriptor.names) {
            if (lowered(name.name) == wanted) {
                resolved = name.value;
                break;
            }
        }
        if (!resolved.has_value()) {
            std::int64_t parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
            if (error == std::errc{} && end == text.data() + text.size()) resolved = parsed;
        }
    } else if (value.is_number_integer()) {
        resolved = value.get<std::int64_t>();
    }
    if (!resolved.has_value() || static_cast<double>(*resolved) < descriptor.minimum ||
        static_cast<double>(*resolved) > descriptor.maximum) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", std::string(k_invalid)});
    }
    return static_cast<std::uint32_t>(*resolved);
}

}

Result<void> apply(std::string_view signature,
                   const nlohmann::json& payload,
                   AuthoringRules& out,
                   std::vector<fo4::schema::SubrecordAuthoringConstraint>& rules,
                   bool& explicitRules) {
    if (signature != "KYWD") return {};
    if (const auto flags = payload.find("flags"); flags != payload.end() && !flags->is_null()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "KYWD has no flags subrecord, so flags cannot be set; it takes editorId, color and type"});
    }
    const auto constraint = fo4::schema::record_authoring_constraint(
        fo4::Signature::from_string("KYWD"));
    if (!constraint.has_value()) {
        return std::unexpected(Error{
            "RecordCreationRequiresSchema", "No authoring schema is registered for KYWD"});
    }
    const auto generated = fo4::schema::authoring_subrecords(*constraint);
    out.rules.assign(generated.begin(), generated.end());
    out.color = {0, 0, 0, 255};
    out.type = {0, 0, 0, 0};
    bool wantsColor = false;
    bool wantsType = false;

    const auto color = payload.find("color");
    if (color != payload.end() && !color->is_null()) {
        const auto bytes = colorBytes(*color);
        if (!bytes) return std::unexpected(bytes.error());
        out.color = *bytes;
        wantsColor = true;
    }
    const auto type = payload.find("type");
    if (type != payload.end() && !type->is_null()) {
        const auto value = typeValue(*type);
        if (!value) return std::unexpected(value.error());
        out.type = {
            static_cast<std::uint8_t>(*value & 0xFFu),
            static_cast<std::uint8_t>((*value >> 8) & 0xFFu),
            static_cast<std::uint8_t>((*value >> 16) & 0xFFu),
            static_cast<std::uint8_t>((*value >> 24) & 0xFFu)};
        wantsType = true;
    }

    if (wantsColor) {
        auto* rule = findRule(out.rules, "CNAM");
        if (rule == nullptr) {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema", "KYWD's CNAM color is not in the authoring schema"});
        }
        rule->minimum = 1;
        rule->maximum = 1;
        rule->default_data = out.color.data();
        rule->default_size = static_cast<std::uint16_t>(out.color.size());
    }
    if (wantsType) {
        auto* rule = findRule(out.rules, "TNAM");
        if (rule == nullptr) {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema", "KYWD's TNAM type is not in the authoring schema"});
        }
        rule->minimum = 1;
        rule->maximum = 1;
        rule->default_data = out.type.data();
        rule->default_size = static_cast<std::uint16_t>(out.type.size());
    }
    rules = out.rules;
    explicitRules = true;
    return {};
}

}
