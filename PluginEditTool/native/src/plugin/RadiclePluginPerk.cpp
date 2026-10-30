#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/forms/perk.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace lifecycle;

namespace {

constexpr auto k_perk = fo4::Signature::from_string("PERK");

using PerkEffect = fo4::forms::PerkEffectEntry;
using PerkTab = fo4::forms::PerkConditionTab;

Error invalidEffects(std::string message) {
    return Error{"InvalidPerkEffects", std::move(message)};
}

std::optional<Error> wrongPerkType(const fo4::Record& record, const fo4::FormKey& key) {
    if (record.header.signature == k_perk) return std::nullopt;
    return Error{
        "InvalidTypedFormKey",
        "PERK form key must name a PERK record: " + key.to_string() + " is a " +
            record.header.signature.to_string()
    };
}

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

template <class UInt>
bool optionalUnsignedField(const nlohmann::json& object, const char* key, std::optional<UInt>& value) {
    if (!object.contains(key)) return true;
    UInt parsed = 0;
    if (!lifecycleUnsignedField(object, key, UInt{0}, parsed)) return false;
    value = parsed;
    return true;
}

std::optional<Error> parametersFromJson(const nlohmann::json& object,
                                        const fo4::Plugin& plugin,
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
            if (!plugin.header.tes4_flags.is_localized()) {
                return invalidEffects("PERK buttonLabelId needs a localized plugin");
            }
            value.id = labelId;
        } else {
            if (plugin.header.tes4_flags.is_localized()) {
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

Result<std::vector<PerkTab>> tabsFromJson(const nlohmann::json& tabs,
                                          const fo4::Plugin& plugin,
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
        auto built = buildConditions(document, plugin, existing, {}, failure);
        if (!built) return std::unexpected(invalidEffects(failure));
        value.conditions = std::move(*built);
        parsed.push_back(std::move(value));
    }
    return parsed;
}

Result<PerkEffect> effectFromJson(const nlohmann::json& object,
                                  const fo4::Plugin& plugin,
                                  const fo4::ConflictGraph& graph,
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
        if (auto error = checkLoadedSlot(graph, effect.quest, {"QUST"}, "PERK quest")) return std::unexpected(*error);
    } else if (effect.type == fo4::forms::PerkEffectType::ability) {
        if (auto error = parseFormKeyField(object, "ability", effect.ability)) return std::unexpected(*error);
        if (effect.ability == fo4::FormKey{}) return std::unexpected(invalidEffects("PERK ability effect needs an ability"));
        if (auto error = checkLoadedSlot(graph, effect.ability, {"SPEL"}, "PERK ability")) return std::unexpected(*error);
    } else {
        if (!lifecycleUnsignedField(object, "entryPoint", std::uint8_t{0}, effect.entry_point) ||
            !lifecycleUnsignedField(object, "function", std::uint8_t{0}, effect.function)) {
            return std::unexpected(invalidEffects("PERK entryPoint and function must be uint8"));
        }
    }

    const auto currentTabs = current != nullptr ? current->tabs : std::vector<PerkTab>{};
    if (const auto found = object.find("tabs"); found != object.end()) {
        auto tabs = tabsFromJson(*found, plugin, currentTabs);
        if (!tabs) return std::unexpected(tabs.error());
        effect.tabs = std::move(*tabs);
    }
    effect.condition_tab_count = static_cast<std::uint8_t>(effect.tabs.size());

    if (const auto found = object.find("parameters"); found != object.end()) {
        if (auto error = parametersFromJson(*found, plugin, effect.parameters)) return std::unexpected(*error);
    }
    if (effect.parameters.type && effect.parameters.data) {
        const auto expected = epfdDataSize(*effect.parameters.type);
        if (expected && effect.parameters.data->size() != *expected) {
            return std::unexpected(
                invalidEffects("PERK parameter data must be " + std::to_string(*expected) + " bytes for its type"));
        }
    }

    if (const auto form = fo4::forms::perk_epfd_form(plugin, effect)) {
        const auto accepted = effect.parameters.type == std::uint8_t{3} ? "LVLI" : "SPEL";
        if (auto error = checkLoadedSlot(graph, *form, {accepted}, "PERK parameter form")) {
            return std::unexpected(*error);
        }
    }
    if (const auto actorValue = fo4::forms::perk_epfd_actor_value(plugin, effect)) {
        if (auto error = checkLoadedSlot(graph, actorValue->first, {"AVIF"}, "PERK actor value")) {
            return std::unexpected(*error);
        }
    }
    return effect;
}

Result<std::vector<PerkEffect>> perkEffectsFromJson(const nlohmann::json& items,
                                                    const fo4::Plugin& plugin,
                                                    const fo4::ConflictGraph& graph,
                                                    const std::vector<PerkEffect>& current) {
    if (!items.is_array()) return std::unexpected(invalidEffects("PERK effects must be an array"));
    std::vector<PerkEffect> effects;
    try {
        for (std::size_t index = 0; index < items.size(); ++index) {
            auto effect = effectFromJson(items[index], plugin, graph,
                                         index < current.size() ? &current[index] : nullptr);
            if (!effect) return std::unexpected(effect.error());
            effects.push_back(std::move(*effect));
        }
    } catch (const std::exception& error) {
        return std::unexpected(invalidEffects(error.what()));
    }
    return effects;
}

nlohmann::json parametersToJson(const fo4::forms::PerkFunctionParameters& parameters) {
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
    return out;
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
    out["parameters"] = parametersToJson(effect.parameters);
    return out;
}

}  // namespace

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
    const auto edit = [&](fo4::Plugin& mutated) {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) {
            refusal = *error;
            return;
        }
        const auto current = fo4::forms::read_perk_effects(mutated, *record);
        auto effects = perkEffectsFromJson(items, mutated, graph_, current);
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

Result<fo4::FormKey> RadiclePluginBackend::createPerkLocked(fo4::Plugin& mutated,
                                                            const nlohmann::json& payload) const {
    const auto* editor = editorIdPayloadField(payload);
    if (editor == nullptr || !editor->is_string() || editor->get<std::string>().empty()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PERK creation requires a non-empty string editorId"
        });
    }
    const auto editorId = editor->get<std::string>();
    if (const auto stored = support::encodeEditorId(editorId); !stored) {
        return std::unexpected(stored.error());
    }

    bool valid = true;
    const auto full = optionalStringField(payload, "full", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK full must be a string or null"});
    const auto description = optionalStringField(payload, "desc", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "PERK desc must be a string or null"});

    fo4::forms::PerkCreation creation;
    creation.editor_id = editorId;
    creation.full_name = optionalStringView(full).value_or(std::string_view{});
    creation.description = optionalStringView(description).value_or(std::string_view{});
    if (!lifecycleUnsignedField(payload, "trait", std::uint8_t{0}, creation.trait) ||
        !lifecycleUnsignedField(payload, "level", std::uint8_t{0}, creation.level) ||
        !lifecycleUnsignedField(payload, "ranks", std::uint8_t{1}, creation.ranks) ||
        !lifecycleUnsignedField(payload, "playable", std::uint8_t{1}, creation.playable)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PERK trait, level, ranks and playable must be uint8"
        });
    }
    if (const auto found = payload.find("effects"); found != payload.end()) {
        auto effects = perkEffectsFromJson(*found, mutated, graph_, {});
        if (!effects) return std::unexpected(effects.error());
        creation.effects = std::move(*effects);
    }
    try {
        return fo4::forms::create_perk(mutated, creation);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PerkCreateFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
