#pragma once

#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/forms/perk.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::perk_detail {

using PerkEffect = fo4::forms::PerkEffectEntry;
using PerkTab = fo4::forms::PerkConditionTab;
using PerkFormKeyResolver = std::function<std::optional<fo4::FormKey>(std::string_view)>;

struct PerkParseContext {
    const fo4::Plugin& plugin;
    const fo4::ConflictGraph& graph;
    const PerkFormKeyResolver& resolve;
};

inline Error invalidEffects(std::string message) {
    return Error{"InvalidPerkEffects", std::move(message)};
}

inline std::optional<Error> wrongPerkType(const fo4::Record& record, const fo4::FormKey& key) {
    if (record.header.signature == fo4::Signature::from_string("PERK")) return std::nullopt;
    return Error{
        "InvalidTypedFormKey",
        "PERK form key must name a PERK record: " + key.to_string() + " is a " +
            record.header.signature.to_string()
    };
}

inline std::optional<Error> requireDeclaredOwner(const fo4::Plugin& plugin,
                                                 const fo4::FormKey& key,
                                                 std::string_view slot) {
    try {
        static_cast<void>(fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(plugin)));
    } catch (const std::exception&) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slot) + " " + key.to_string() +
                " names a plugin that is not a master of this plugin; declare it with add_master"
        };
    }
    return std::nullopt;
}

inline bool presentField(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && !found->is_null();
}

template <class UInt>
inline bool optionalUnsignedField(const nlohmann::json& object, const char* key, std::optional<UInt>& value) {
    if (!object.contains(key)) return true;
    UInt parsed = 0;
    if (!lifecycle::lifecycleUnsignedField(object, key, UInt{0}, parsed)) return false;
    value = parsed;
    return true;
}

inline EditorIdResolver conditionResolver(const PerkParseContext& context) {
    return [&context](std::string_view text) -> std::optional<fo4::RawFormId> {
        const auto key = context.resolve(text);
        if (!key) return std::nullopt;
        try {
            return fo4::encode_plugin_form_id(*key, fo4::plugin_form_id_context(context.plugin));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };
}

std::optional<Error> parametersFromJson(const nlohmann::json& object,
                                        const PerkParseContext& context,
                                        fo4::forms::PerkFunctionParameters& parameters);

std::optional<Error> typedDataFromJson(const nlohmann::json& object,
                                       const PerkParseContext& context,
                                       const PerkEffect& effect,
                                       std::optional<std::vector<std::byte>>& data);

nlohmann::json parametersToJson(const PerkEffect& effect, const fo4::Plugin& plugin);

Result<std::vector<PerkEffect>> perkEffectsFromJson(const nlohmann::json& items,
                                                    const PerkParseContext& context,
                                                    const std::vector<PerkEffect>& current);

}  // namespace fo4ide::plugin::perk_detail
