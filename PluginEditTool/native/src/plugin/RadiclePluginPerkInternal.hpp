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
using PerkMasterEnsurer = std::function<void(fo4::Plugin&, const fo4::PluginKey&)>;

struct PerkParseContext {
    fo4::Plugin& plugin;
    const fo4::ConflictGraph& graph;
    const PerkFormKeyResolver& resolve;
    const PerkMasterEnsurer& ensureMaster;
    const fo4::Plugin& baseline;
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

std::optional<Error> parametersFromJson(const nlohmann::json& object,
                                        const PerkParseContext& context,
                                        fo4::forms::PerkFunctionParameters& parameters);

std::optional<Error> typedDataFromJson(const nlohmann::json& object,
                                       const PerkParseContext& context,
                                       const PerkEffect& effect,
                                       const PerkEffect* current,
                                       std::optional<std::vector<std::byte>>& data);

nlohmann::json parametersToJson(const PerkEffect& effect, const fo4::Plugin& plugin);

Result<std::vector<PerkEffect>> perkEffectsFromJson(const nlohmann::json& items,
                                                    const PerkParseContext& context,
                                                    const std::vector<PerkEffect>& current);

}  // namespace fo4ide::plugin::perk_detail
