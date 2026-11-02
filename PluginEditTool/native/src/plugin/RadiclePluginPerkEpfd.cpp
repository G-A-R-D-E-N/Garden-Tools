#include "RadiclePluginPerkInternal.hpp"

#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace lifecycle;

namespace perk_detail {

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::vector<std::byte>> hexDecode(std::string_view text) {
    if (text.size() % 2 != 0) return std::nullopt;
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto high = hexDigit(text[i]);
        const auto low = hexDigit(text[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes.push_back(static_cast<std::byte>((high << 4) | low));
    }
    return bytes;
}

std::string hexEncode(const std::vector<std::byte>& bytes) {
    std::string text;
    for (const auto byte : bytes) text += std::format("{:02x}", std::to_integer<unsigned>(byte));
    return text;
}

void appendFloat(std::vector<std::byte>& out, float value) {
    std::array<std::byte, sizeof(float)> raw{};
    std::memcpy(raw.data(), &value, sizeof(float));
    out.insert(out.end(), raw.begin(), raw.end());
}

void appendFormId(std::vector<std::byte>& out, const fo4::Plugin& plugin, const fo4::FormKey& key) {
    const std::uint32_t raw = fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(plugin)).value();
    std::array<std::byte, sizeof(std::uint32_t)> bytes{};
    std::memcpy(bytes.data(), &raw, sizeof(raw));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::optional<Error> formFromJson(const nlohmann::json& object,
                                  const char* key,
                                  const PerkParseContext& context,
                                  std::string_view slot,
                                  fo4::FormKey& out) {
    bool valid = true;
    const auto text = optionalStringField(object, key, valid);
    if (!valid || !text.has_value()) {
        return invalidEffects(std::string("PERK parameter ") + key + " must be a FormKey or editor ID string");
    }
    const auto resolved = context.resolve(*text);
    if (!resolved) {
        return Error{"InvalidTypedFormKey", std::string(slot) + " " + *text + " does not resolve to a loaded record"};
    }
    out = *resolved;
    return requireDeclaredOwner(context.plugin, out, slot);
}

void typedFieldsToJson(const PerkEffect& effect, const fo4::Plugin& plugin, nlohmann::json& out) {
    switch (fo4::forms::perk_function_type(effect)) {
        case fo4::forms::PerkFunctionType::single_float:
            if (const auto value = fo4::forms::perk_epfd_float(effect)) out["value"] = *value;
            break;
        case fo4::forms::PerkFunctionType::float_pair:
            if (const auto pair = fo4::forms::perk_epfd_float_pair(effect)) {
                out["value"] = pair->first;
                out["value2"] = pair->second;
            }
            break;
        case fo4::forms::PerkFunctionType::leveled_item:
        case fo4::forms::PerkFunctionType::labeled_spell:
        case fo4::forms::PerkFunctionType::spell:
            if (const auto form = fo4::forms::perk_epfd_form(plugin, effect)) out["form"] = support::formKeyJson(*form);
            break;
        case fo4::forms::PerkFunctionType::text:
            if (const auto text = fo4::forms::perk_epfd_text(effect)) {
                out["text"] = fo4::text::windows1252_to_utf8(*text);
            }
            break;
        case fo4::forms::PerkFunctionType::actor_value:
            if (const auto actorValue = fo4::forms::perk_epfd_actor_value(plugin, effect)) {
                out["actorValue"] = support::formKeyJson(actorValue->first);
                out["value"] = actorValue->second;
            }
            break;
        default:
            break;
    }
}

}  // namespace

std::optional<Error> parametersFromJson(const nlohmann::json& object,
                                        const PerkParseContext& context,
                                        fo4::forms::PerkFunctionParameters& parameters) {
    if (!object.is_object()) return invalidEffects("PERK effect parameters must be an object");
    if (!optionalUnsignedField(object, "type", parameters.type) ||
        !optionalUnsignedField(object, "entryId", parameters.entry_id) ||
        !optionalUnsignedField(object, "scriptFlags", parameters.script_flags)) {
        return invalidEffects("PERK parameter type, entryId and scriptFlags must be unsigned integers in range");
    }

    bool valid = true;
    const auto data = optionalStringField(object, "data", valid);
    if (!valid) return invalidEffects("PERK parameter data must be a hex string");
    if (data.has_value()) {
        auto bytes = hexDecode(*data);
        if (!bytes) return invalidEffects("PERK parameter data must be an even-length hex string");
        parameters.data = std::move(*bytes);
    }

    const auto label = optionalStringField(object, "buttonLabel", valid);
    if (!valid) return invalidEffects("PERK buttonLabel must be a string");
    std::optional<std::uint32_t> labelId;
    if (!optionalUnsignedField(object, "buttonLabelId", labelId)) {
        return invalidEffects("PERK buttonLabelId must be an unsigned 32-bit integer");
    }
    if (label.has_value() && labelId.has_value()) {
        return invalidEffects("PERK parameters take buttonLabel or buttonLabelId, not both");
    }
    if (label.has_value() || labelId.has_value()) {
        fo4::schema::LocalizedString value;
        if (labelId.has_value()) {
            if (!context.plugin.header.tes4_flags.is_localized()) {
                return invalidEffects("PERK buttonLabelId needs a localized plugin");
            }
            value.id = labelId;
        } else {
            if (context.plugin.header.tes4_flags.is_localized()) {
                return invalidEffects("PERK buttonLabel text needs a non-localized plugin; use buttonLabelId");
            }
            auto stored = fo4::text::utf8_to_windows1252(*label);
            if (!stored) return invalidEffects("PERK buttonLabel is not representable in Windows-1252");
            value.text = std::move(*stored);
        }
        parameters.button_label = std::move(value);
    }
    return std::nullopt;
}

std::optional<Error> typedDataFromJson(const nlohmann::json& object,
                                       const PerkParseContext& context,
                                       const PerkEffect& effect,
                                       std::optional<std::vector<std::byte>>& data) {
    static constexpr std::array<const char*, 5> keys{"value", "value2", "form", "actorValue", "text"};
    const bool anyTyped = std::any_of(keys.begin(), keys.end(), [&object](const char* key) {
        return presentField(object, key);
    });
    if (!anyTyped) return std::nullopt;
    if (data.has_value()) return invalidEffects("PERK parameters take data or typed fields, not both");
    if (!effect.parameters.type.has_value()) return invalidEffects("PERK typed parameter fields need parameters.type");

    const auto type = std::to_string(*effect.parameters.type);
    const auto shape = fo4::forms::perk_function_type(effect);
    const bool wantsValue = shape == fo4::forms::PerkFunctionType::single_float ||
                            shape == fo4::forms::PerkFunctionType::float_pair ||
                            shape == fo4::forms::PerkFunctionType::actor_value;
    const bool wantsValue2 = shape == fo4::forms::PerkFunctionType::float_pair;
    const bool wantsForm = shape == fo4::forms::PerkFunctionType::leveled_item ||
                           shape == fo4::forms::PerkFunctionType::labeled_spell ||
                           shape == fo4::forms::PerkFunctionType::spell;
    const bool wantsActor = shape == fo4::forms::PerkFunctionType::actor_value;
    const bool wantsText = shape == fo4::forms::PerkFunctionType::text;
    const std::array<bool, 5> allowed{wantsValue, wantsValue2, wantsForm, wantsActor, wantsText};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (presentField(object, keys[i]) && !allowed[i]) {
            return invalidEffects(std::string("PERK parameter ") + keys[i] + " does not apply to parameters.type " + type);
        }
        if (allowed[i] && !presentField(object, keys[i])) {
            return invalidEffects(std::string("PERK parameter ") + keys[i] + " is required for parameters.type " + type);
        }
    }

    std::vector<std::byte> bytes;
    if (wantsForm || wantsActor) {
        fo4::FormKey key;
        const auto slot = wantsForm ? std::string_view{"PERK parameter form"} : std::string_view{"PERK actor value"};
        if (auto error = formFromJson(object, wantsForm ? "form" : "actorValue", context, slot, key)) return error;
        appendFormId(bytes, context.plugin, key);
    }
    if (wantsValue) {
        float number = 0.0f;
        if (!lifecycleFloatField(object, "value", 0.0f, number)) {
            return invalidEffects("PERK parameter value must be a finite number");
        }
        appendFloat(bytes, number);
    }
    if (wantsValue2) {
        float number = 0.0f;
        if (!lifecycleFloatField(object, "value2", 0.0f, number)) {
            return invalidEffects("PERK parameter value2 must be a finite number");
        }
        appendFloat(bytes, number);
    }
    if (wantsText) {
        bool valid = true;
        const auto text = optionalStringField(object, "text", valid);
        if (!valid || !text.has_value()) return invalidEffects("PERK parameter text must be a string");
        const auto stored = fo4::text::utf8_to_windows1252(*text);
        if (!stored) return invalidEffects("PERK parameter text is not representable in Windows-1252");
        for (const char ch : *stored) bytes.push_back(static_cast<std::byte>(ch));
        bytes.push_back(std::byte{0});
    }
    data = std::move(bytes);
    return std::nullopt;
}

nlohmann::json parametersToJson(const PerkEffect& effect, const fo4::Plugin& plugin) {
    const auto& parameters = effect.parameters;
    nlohmann::json out = nlohmann::json::object();
    if (parameters.type.has_value()) out["type"] = *parameters.type;
    if (parameters.entry_id.has_value()) out["entryId"] = *parameters.entry_id;
    if (parameters.script_flags.has_value()) out["scriptFlags"] = *parameters.script_flags;
    if (parameters.button_label.has_value()) {
        if (parameters.button_label->localized()) {
            out["buttonLabelId"] = *parameters.button_label->id;
        } else {
            out["buttonLabel"] = fo4::text::windows1252_to_utf8(parameters.button_label->inline_text());
        }
    }
    if (parameters.data.has_value()) out["data"] = hexEncode(*parameters.data);
    typedFieldsToJson(effect, plugin, out);
    return out;
}

}  // namespace perk_detail

}  // namespace fo4ide::plugin
