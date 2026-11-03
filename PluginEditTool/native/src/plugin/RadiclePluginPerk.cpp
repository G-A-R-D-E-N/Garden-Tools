#include "RadiclePluginPerkInternal.hpp"

#include <fo4/environment/strings.hpp>
#include <fo4/forms/perk.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace lifecycle;
using namespace perk_detail;

namespace {

struct EffectTypeName {
    std::string_view name;
    fo4::forms::PerkEffectType type;
};

constexpr std::array<EffectTypeName, 3> k_effect_type_names{{
    {"quest_stage", fo4::forms::PerkEffectType::quest_stage},
    {"ability", fo4::forms::PerkEffectType::ability},
    {"entry_point", fo4::forms::PerkEffectType::entry_point},
}};

std::optional<fo4::forms::PerkEffectType> effectTypeFromName(std::string_view name) {
    for (const auto& entry : k_effect_type_names) {
        if (entry.name == name) return entry.type;
    }
    return std::nullopt;
}

std::string effectTypeName(fo4::forms::PerkEffectType type) {
    for (const auto& entry : k_effect_type_names) {
        if (entry.type == type) return std::string(entry.name);
    }
    return {};
}

std::optional<std::size_t> epfdDataSize(std::uint8_t type) {
    if (type == 1 || type == 3 || type == 4 || type == 5) return 4;
    if (type == 2 || type == 8) return 8;
    return std::nullopt;
}

Result<std::vector<PerkTab>> tabsFromJson(const nlohmann::json& tabs,
                                          const PerkParseContext& context,
                                          const std::vector<PerkTab>& current) {
    if (!tabs.is_array() || tabs.size() > std::numeric_limits<std::uint8_t>::max()) {
        return std::unexpected(invalidEffects("PERK tabs must be an array of at most 255 tabs"));
    }
    std::vector<PerkTab> parsed;
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        const auto& tab = tabs[index];
        if (!tab.is_object()) return std::unexpected(invalidEffects("PERK tab must be an object"));
        PerkTab value;
        if (!lifecycleSignedField<std::int8_t>(tab, "runOn", 0, value.run_on)) {
            return std::unexpected(invalidEffects("PERK tab runOn must be an int8"));
        }
        const auto found = tab.find("conditions");
        const auto document = found == tab.end() ? nlohmann::json::array() : *found;
        const std::vector<fo4::CtdaCondition> existing =
            index < current.size() ? current[index].conditions : std::vector<fo4::CtdaCondition>{};
        std::string failure;
        auto built = buildConditions(document, context.plugin, existing, conditionResolver(context), failure);
        if (!built) return std::unexpected(invalidEffects(failure));
        value.conditions = std::move(*built);
        parsed.push_back(std::move(value));
    }
    return parsed;
}

nlohmann::json effectToJson(const PerkEffect& effect, const fo4::Plugin& plugin) {
    nlohmann::json out{
        {"type", effectTypeName(effect.type)},
        {"rank", effect.rank},
        {"priority", effect.priority}
    };
    if (effect.type == fo4::forms::PerkEffectType::quest_stage) {
        out["quest"] = support::formKeyJson(effect.quest);
        out["questStage"] = effect.quest_stage;
    } else if (effect.type == fo4::forms::PerkEffectType::ability) {
        out["ability"] = support::formKeyJson(effect.ability);
    } else {
        out["entryPoint"] = effect.entry_point;
        out["function"] = effect.function;
    }
    nlohmann::json tabs = nlohmann::json::array();
    for (const auto& tab : effect.tabs) {
        nlohmann::json conditions = nlohmann::json::array();
        for (const auto& condition : tab.conditions) conditions.push_back(describeCondition(condition, plugin));
        tabs.push_back(nlohmann::json{{"runOn", tab.run_on}, {"conditions", std::move(conditions)}});
    }
    out["tabs"] = std::move(tabs);
    out["parameters"] = parametersToJson(effect, plugin);
    return out;
}

}  // namespace

namespace perk_detail {

Result<PerkEffect> effectFromJson(const nlohmann::json& object,
                                  const PerkParseContext& context,
                                  const PerkEffect* current) {
    if (!object.is_object()) return std::unexpected(invalidEffects("PERK effect must be an object"));
    bool valid = true;
    const auto typeName = optionalStringField(object, "type", valid);
    const auto type = typeName.has_value() ? effectTypeFromName(*typeName) : std::nullopt;
    if (!valid || !type) {
        return std::unexpected(invalidEffects("PERK effect type must be quest_stage, ability or entry_point"));
    }

    PerkEffect effect;
    effect.type = *type;
    if (!lifecycleUnsignedField(object, "rank", std::uint8_t{0}, effect.rank) ||
        !lifecycleUnsignedField(object, "priority", std::uint8_t{0}, effect.priority)) {
        return std::unexpected(invalidEffects("PERK effect rank and priority must be uint8"));
    }

    if (effect.type == fo4::forms::PerkEffectType::quest_stage) {
        if (auto error = parseFormKeyField(object, "quest", effect.quest)) return std::unexpected(*error);
        if (!lifecycleUnsignedField(object, "questStage", std::uint16_t{0}, effect.quest_stage)) {
            return std::unexpected(invalidEffects("PERK questStage must be uint16"));
        }
        if (effect.quest == fo4::FormKey{}) return std::unexpected(invalidEffects("PERK quest_stage effect needs a quest"));
        if (auto error = requireDeclaredOwner(context.plugin, effect.quest, "PERK quest")) return std::unexpected(*error);
        if (auto error = checkLoadedSlot(context.graph, effect.quest, {"QUST"}, "PERK quest")) return std::unexpected(*error);
    } else if (effect.type == fo4::forms::PerkEffectType::ability) {
        if (auto error = parseFormKeyField(object, "ability", effect.ability)) return std::unexpected(*error);
        if (effect.ability == fo4::FormKey{}) return std::unexpected(invalidEffects("PERK ability effect needs an ability"));
        if (auto error = requireDeclaredOwner(context.plugin, effect.ability, "PERK ability")) return std::unexpected(*error);
        if (auto error = checkLoadedSlot(context.graph, effect.ability, {"SPEL"}, "PERK ability")) return std::unexpected(*error);
    } else {
        if (!lifecycleUnsignedField(object, "entryPoint", std::uint8_t{0}, effect.entry_point) ||
            !lifecycleUnsignedField(object, "function", std::uint8_t{0}, effect.function)) {
            return std::unexpected(invalidEffects("PERK entryPoint and function must be uint8"));
        }
    }

    const auto currentTabs = current != nullptr ? current->tabs : std::vector<PerkTab>{};
    if (const auto found = object.find("tabs"); found != object.end()) {
        auto tabs = tabsFromJson(*found, context, currentTabs);
        if (!tabs) return std::unexpected(tabs.error());
        effect.tabs = std::move(*tabs);
    }
    effect.condition_tab_count = static_cast<std::uint8_t>(effect.tabs.size());

    if (const auto found = object.find("parameters"); found != object.end()) {
        if (auto error = parametersFromJson(*found, context, effect.parameters)) return std::unexpected(*error);
        if (auto error = typedDataFromJson(*found, context, effect, effect.parameters.data)) {
            return std::unexpected(*error);
        }
    }
    if (effect.parameters.type && effect.parameters.data) {
        const auto expected = epfdDataSize(*effect.parameters.type);
        if (expected && effect.parameters.data->size() != *expected) {
            return std::unexpected(
                invalidEffects("PERK parameter data must be " + std::to_string(*expected) + " bytes for its type"));
        }
    }

    if (const auto form = fo4::forms::perk_epfd_form(context.plugin, effect)) {
        const auto accepted = effect.parameters.type == std::uint8_t{3} ? "LVLI" : "SPEL";
        if (auto error = checkLoadedSlot(context.graph, *form, {accepted}, "PERK parameter form")) {
            return std::unexpected(*error);
        }
    }
    if (const auto actorValue = fo4::forms::perk_epfd_actor_value(context.plugin, effect)) {
        if (auto error = checkLoadedSlot(context.graph, actorValue->first, {"AVIF"}, "PERK actor value")) {
            return std::unexpected(*error);
        }
    }
    return effect;
}

Result<std::vector<PerkEffect>> perkEffectsFromJson(const nlohmann::json& items,
                                                    const PerkParseContext& context,
                                                    const std::vector<PerkEffect>& current) {
    if (!items.is_array()) return std::unexpected(invalidEffects("PERK effects must be an array"));
    std::vector<PerkEffect> effects;
    try {
        for (std::size_t index = 0; index < items.size(); ++index) {
            auto effect = effectFromJson(items[index], context,
                                         index < current.size() ? &current[index] : nullptr);
            if (!effect) return std::unexpected(effect.error());
            effects.push_back(std::move(*effect));
        }
    } catch (const std::exception& error) {
        return std::unexpected(invalidEffects(error.what()));
    }
    return effects;
}

}  // namespace perk_detail

Result<nlohmann::json> RadiclePluginBackend::getPerkEffects(const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 0), *key);
    if (!version) return std::unexpected(version.error());
    const auto* source = *version;
    const auto* record = fo4::find_record(*source, *key);
    if (const auto error = wrongPerkType(*record, *key)) return std::unexpected(*error);
    try {
        nlohmann::json effects = nlohmann::json::array();
        for (const auto& effect : fo4::forms::read_perk_effects(*source, *record)) {
            effects.push_back(effectToJson(effect, *source));
        }
        return nlohmann::json{{"formKey", key->to_string()}, {"effects", std::move(effects)}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PerkReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setPerkEffects(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto items = support::argumentAt(arguments, 2);
    if (!key) return std::unexpected(key.error());

    std::optional<Error> refusal;
    std::size_t count = 0;
    const PerkFormKeyResolver resolve = [this](std::string_view text) { return resolveListValue(text); };
    const auto edit = [&](fo4::Plugin& mutated) {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) {
            refusal = *error;
            return;
        }
        const auto current = fo4::forms::read_perk_effects(mutated, *record);
        const PerkParseContext context{mutated, graph_, resolve};
        auto effects = perkEffectsFromJson(items, context, current);
        if (!effects) {
            refusal = effects.error();
            return;
        }
        count = effects->size();
        if (fo4::forms::set_perk_effects(mutated, *key, *effects) != fo4::forms::PerkEffectWrite::written) {
            refusal = Error{"PerkWriteFailed", "PERK effects could not be written to " + key->to_string()};
        }
    };
    const auto staged = stageTypedRecordEdit(pluginName, *key, "PerkWriteFailed", edit);
    if (refusal.has_value()) return std::unexpected(*refusal);
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) {
        return nlohmann::json("No change for PERK effects on " + key->to_string() + " in " + pluginName + ".");
    }
    return nlohmann::json("Set " + std::to_string(count) + " perk effect(s) on " + key->to_string() +
                          " in " + pluginName + ". Call save_plugin to persist.");
}

}  // namespace fo4ide::plugin
