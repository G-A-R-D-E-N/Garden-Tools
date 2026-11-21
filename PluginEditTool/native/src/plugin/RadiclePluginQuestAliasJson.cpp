#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

constexpr std::uint32_t kRunOnReference = 2;
constexpr std::uint8_t kPayloadFlagBits = static_cast<std::uint8_t>(
    fo4::k_ctda_flag_or | fo4::k_ctda_flag_aliases | fo4::k_ctda_flag_pack_data |
    fo4::k_ctda_flag_swap);

struct NamedConditionFlag {
    std::uint8_t bit;
    std::string_view name;
};

constexpr std::array<NamedConditionFlag, 4> kNamedConditionFlags{{
    {fo4::k_ctda_flag_or, "OR"},
    {fo4::k_ctda_flag_aliases, "ParametersUseAliases"},
    {fo4::k_ctda_flag_pack_data, "UsePackData"},
    {fo4::k_ctda_flag_swap, "SwapSubjectAndTarget"},
}};

constexpr std::array<std::string_view, 5> kLevelNames{"Easy", "Medium", "Hard", "VeryHard", "None"};

bool parsesAsFormKey(const std::string& text) {
    try {
        static_cast<void>(fo4::FormKey::parse(text));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<std::string> formKeyText(const nlohmann::json& described, const char* key) {
    if (!described.contains(key) || !described[key].is_string()) return std::nullopt;
    auto text = described[key].get<std::string>();
    if (!parsesAsFormKey(text)) return std::nullopt;
    return text;
}

bool paramPayload(const nlohmann::json& described,
                  const char* key,
                  std::int32_t raw,
                  nlohmann::json& out) {
    const auto& value = described[key];
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (parsesAsFormKey(text)) {
            out = text;
            return true;
        }
        if (text.empty() || text.back() == ':') return false;
    }
    out = raw;
    return true;
}

std::optional<nlohmann::json> conditionPayload(const fo4::Plugin& plugin,
                                               const fo4::CtdaCondition& condition) {
    if (condition.is_event_data() || condition.unknown2 != 0 || condition.unknown3 != -1) {
        return std::nullopt;
    }
    const auto name = functionName(condition.function);
    if (name.empty() || functionIndex(name) != condition.function) return std::nullopt;
    const auto runOn = runOnName(condition.run_on);
    if (condition.compare_operator() > 5 || runOn.empty()) return std::nullopt;
    const auto bits = static_cast<std::uint8_t>(condition.flags() & ~fo4::k_ctda_flag_use_global);
    if ((bits & ~kPayloadFlagBits) != 0) return std::nullopt;

    const auto described = describeCondition(condition, plugin);
    nlohmann::json out = nlohmann::json::object();
    out["function"] = name;
    out["operator"] = conditionOperatorString(condition.compare_operator());
    out["runOn"] = runOn;

    std::string flags;
    for (const auto& flag : kNamedConditionFlags) {
        if ((bits & flag.bit) == 0) continue;
        if (!flags.empty()) flags += ",";
        flags += std::string(flag.name);
    }
    if (!flags.empty()) out["flags"] = flags;

    const auto addParam = [&described, &out](const char* key, std::int32_t raw) {
        if (!described.contains(key)) return true;
        nlohmann::json value;
        if (!paramPayload(described, key, raw, value)) return false;
        out[key] = std::move(value);
        return true;
    };
    if (!addParam("param1", condition.param1) || !addParam("param2", condition.param2)) {
        return std::nullopt;
    }

    if (condition.use_global()) {
        const auto global = formKeyText(described, "compareGlobal");
        if (!global.has_value()) return std::nullopt;
        out["compareGlobal"] = *global;
    } else {
        if (!std::isfinite(condition.comparison_float())) return std::nullopt;
        out["value"] = condition.comparison_float();
    }

    if (condition.run_on == kRunOnReference) {
        const auto reference = formKeyText(described, "reference");
        if (!reference.has_value()) return std::nullopt;
        out["reference"] = *reference;
    } else if (condition.reference != 0) {
        return std::nullopt;
    }

    const auto addCis = [&described, &out](const char* key,
                                           const std::optional<std::string>& stored) {
        if (!stored.has_value()) return true;
        if (!described.contains(key) || !described[key].is_string()) return false;
        const auto utf8 = described[key].get<std::string>();
        const auto bytes = conditionCisFromUtf8(utf8);
        if (!bytes.has_value() || *bytes != *stored) return false;
        out[key] = utf8;
        return true;
    };
    if (!addCis("cis1", condition.parameter1_string) ||
        !addCis("cis2", condition.parameter2_string)) {
        return std::nullopt;
    }
    return out;
}

std::optional<nlohmann::json> aliasConditionPayload(const fo4::Plugin& plugin,
                                                    const fo4::forms::QuestAlias& alias) {
    std::vector<fo4::CtdaCondition> conditions;
    if (!aliasConditionRun(alias, conditions)) return std::nullopt;
    nlohmann::json out = nlohmann::json::array();
    for (const auto& condition : conditions) {
        auto payload = conditionPayload(plugin, condition);
        if (!payload.has_value()) return std::nullopt;
        out.push_back(std::move(*payload));
    }
    return out;
}

template <typename T>
nlohmann::json optionalValueJson(const std::optional<T>& value) {
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optionalKeyJson(const std::optional<fo4::FormKey>& key) {
    return key.has_value() ? nlohmann::json(key->to_string()) : nlohmann::json(nullptr);
}

nlohmann::json formKeysJson(const std::vector<fo4::FormKey>& keys) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& key : keys) out.push_back(key.to_string());
    return out;
}

nlohmann::json itemsJson(const std::vector<fo4::forms::QuestAliasItem>& items) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& item : items) {
        nlohmann::json row = nlohmann::json::object();
        row["item"] = item.item.to_string();
        row["count"] = item.count;
        if (item.extra.has_value()) {
            nlohmann::json extra = nlohmann::json::object();
            if (item.extra->owner.has_value()) extra["owner"] = item.extra->owner->to_string();
            if (item.extra->global.has_value()) extra["global"] = item.extra->global->to_string();
            if (item.extra->required_rank.has_value()) {
                extra["requiredRank"] = *item.extra->required_rank;
            }
            extra["itemCondition"] = item.extra->item_condition;
            row["extra"] = std::move(extra);
        }
        out.push_back(std::move(row));
    }
    return out;
}

bool itemExtraOwnerUnresolved(const std::vector<fo4::forms::QuestAliasItem>& items,
                              const fo4::FormLinkTypeLookup& ownerTypes) {
    return std::any_of(
        items.begin(), items.end(), [&ownerTypes](const fo4::forms::QuestAliasItem& item) {
            return item.extra.has_value() && item.extra->owner.has_value() &&
                   !ownerTypes(*item.extra->owner).has_value();
        });
}

nlohmann::json linkedAliasesJson(const std::vector<fo4::forms::QuestLinkedAlias>& links) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& link : links) {
        nlohmann::json row = nlohmann::json::object();
        row["keyword"] = link.keyword.to_string();
        row["alias"] = link.alias;
        out.push_back(std::move(row));
    }
    return out;
}

void addFillJson(nlohmann::json& out, const fo4::forms::QuestAliasFill& fill) {
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, fo4::forms::QuestSpecificReferenceFill>) {
                if (value.reference.has_value()) {
                    out["forcedReference"] = value.reference->to_string();
                }
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestUniqueActorFill>) {
                out["uniqueActor"] = value.actor.to_string();
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestLocationAliasReferenceFill>) {
                nlohmann::json row = nlohmann::json::object();
                row["alias"] = value.alias;
                row["referenceType"] = value.reference_type.to_string();
                out["locationAliasReference"] = std::move(row);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestExternalAliasReferenceFill>) {
                nlohmann::json row = nlohmann::json::object();
                if (value.quest.has_value()) row["quest"] = value.quest->to_string();
                row["alias"] = value.alias;
                out["externalAlias"] = std::move(row);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestCreateReferenceFill>) {
                nlohmann::json row = nlohmann::json::object();
                if (value.object.has_value()) row["object"] = value.object->to_string();
                row["alias"] = value.alias;
                row["create"] = value.create_mode == 0x8000u ? "In" : "At";
                const auto level = value.level < kLevelNames.size()
                                       ? value.level
                                       : static_cast<std::uint32_t>(kLevelNames.size() - 1);
                row["level"] = std::string(kLevelNames[level]);
                out["createReference"] = std::move(row);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestFindMatchingReferenceFill>) {
                std::visit(
                    [&out](const auto& selector) {
                        using S = std::decay_t<decltype(selector)>;
                        if constexpr (std::is_same_v<S, fo4::forms::QuestMatchingReferenceNearAlias>) {
                            nlohmann::json row = nlohmann::json::object();
                            row["alias"] = selector.alias;
                            row["type"] = selector.type;
                            out["findMatchingNearAlias"] = std::move(row);
                        } else if constexpr (std::is_same_v<S, fo4::forms::QuestMatchingReferenceEvent>) {
                            nlohmann::json row = nlohmann::json::object();
                            row["event"] = selector.event;
                            row["data"] = selector.data;
                            out["findMatchingFromEvent"] = std::move(row);
                        } else if constexpr (std::is_same_v<S, fo4::forms::QuestMatchingReferenceClosest>) {
                            nlohmann::json row = nlohmann::json::object();
                            row["alias"] = selector.alias;
                            out["findMatchingClosest"] = std::move(row);
                        }
                    },
                    value.selector);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestSpecificLocationFill>) {
                out["specificLocation"] = value.location.to_string();
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestReferenceAliasLocationFill>) {
                nlohmann::json row = nlohmann::json::object();
                row["alias"] = value.alias;
                if (value.keyword.has_value()) row["keyword"] = value.keyword->to_string();
                out["referenceAliasLocation"] = std::move(row);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestExternalAliasLocationFill>) {
                nlohmann::json row = nlohmann::json::object();
                if (value.quest.has_value()) row["quest"] = value.quest->to_string();
                if (value.alias.has_value()) row["alias"] = *value.alias;
                out["externalAliasLocation"] = std::move(row);
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestFindMatchingLocationFill>) {
                nlohmann::json row = nlohmann::json::object();
                row["event"] = value.event;
                row["data"] = value.data;
                out["findMatchingLocation"] = std::move(row);
            }
        },
        fill);
}

}

bool aliasConditionRun(const fo4::forms::QuestAlias& alias,
                       std::vector<fo4::CtdaCondition>& conditions) {
    conditions.clear();
    const auto isCondition = [](const fo4::Subrecord& member) {
        return member.signature == fo4::sig_ctda ||
               member.signature == fo4::Signature::from_string("CIS1") ||
               member.signature == fo4::Signature::from_string("CIS2");
    };
    const auto first = std::find_if(alias.subrecords.begin(), alias.subrecords.end(), isCondition);
    if (first == alias.subrecords.end()) return true;
    const auto last = std::find_if_not(first, alias.subrecords.end(), isCondition);
    const std::span<const fo4::Subrecord> run{
        alias.subrecords.data() + (first - alias.subrecords.begin()),
        static_cast<std::size_t>(last - first)};
    std::size_t consumed = 0;
    if (!fo4::forms::lossless_quest_log_entry_conditions(run, conditions, consumed) ||
        consumed != run.size()) {
        conditions.clear();
        return false;
    }
    return true;
}

nlohmann::json questAliasJson(const fo4::Plugin& plugin,
                              const fo4::forms::QuestAlias& alias,
                              const fo4::FormLinkTypeLookup& ownerTypes) {
    nlohmann::json out = nlohmann::json::object();
    if (alias.kind == fo4::forms::QuestAliasKind::collection) {
        out["kind"] = "collection";
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& entry : alias.collection_entries) {
            nlohmann::json row = nlohmann::json::object();
            row["id"] = entry.id;
            if (entry.max_initial_fill_count.has_value()) {
                row["maxInitialFillCount"] = *entry.max_initial_fill_count;
            }
            entries.push_back(std::move(row));
        }
        out["entries"] = std::move(entries);
        return out;
    }

    const bool reference = alias.kind == fo4::forms::QuestAliasKind::reference;
    out["kind"] = reference ? "reference" : "location";
    out["id"] = alias.id;
    out["name"] = optionalValueJson(alias.name);
    out["flags"] = optionalValueJson(alias.flags);
    out["forceIntoAlias"] = optionalValueJson(alias.force_into_alias);
    out["closestToAlias"] = optionalValueJson(alias.closest_to_alias);
    addFillJson(out, alias.fill);
    if (reference) {
        out["keywords"] = formKeysJson(alias.keywords);
        out["items"] = itemsJson(alias.items);
        out["linkedAliases"] = linkedAliasesJson(alias.linked_aliases);
        out["spells"] = formKeysJson(alias.spells);
        out["factions"] = formKeysJson(alias.factions);
        out["packages"] = formKeysJson(alias.packages);
        out["spectatorOverridePackageList"] = optionalKeyJson(alias.spectator_override);
        out["observeDeadBodyOverridePackageList"] =
            optionalKeyJson(alias.observe_dead_body_override);
        out["guardWarnOverridePackageList"] = optionalKeyJson(alias.guard_warn_override);
        out["combatOverridePackageList"] = optionalKeyJson(alias.combat_override);
        out["displayName"] = optionalKeyJson(alias.display_name);
        out["forcedVoice"] = optionalKeyJson(alias.forced_voice);
        out["deathItem"] = optionalKeyJson(alias.death_item);
        out["voiceTypes"] = optionalKeyJson(alias.voice_types);
        if (itemExtraOwnerUnresolved(alias.items, ownerTypes)) out["extrasUnreadable"] = true;
    }
    if (auto conditions = aliasConditionPayload(plugin, alias)) {
        out["conditions"] = std::move(*conditions);
    }
    return out;
}

}
