#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <fo4/schema/vmad.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

bool allowedAliasMember(fo4::forms::QuestAliasKind kind, std::string_view signature) {
    if (kind == fo4::forms::QuestAliasKind::collection) {
        return signature == "ALCS" || signature == "ALMI";
    }
    if (kind == fo4::forms::QuestAliasKind::location) {
        return signature == "ALLS" || signature == "ALID" || signature == "FNAM" ||
               signature == "ALFI" || signature == "ALFL" || signature == "ALFA" ||
               signature == "KNAM" || signature == "ALEQ" || signature == "ALEA" ||
               signature == "ALFE" || signature == "ALFD" || signature == "ALCC" ||
               signature == "CTDA" || signature == "CIS1" || signature == "CIS2" ||
               signature == "ALED";
    }
    return signature == "ALST" || signature == "ALID" || signature == "FNAM" ||
           signature == "ALFI" || signature == "ALFR" || signature == "ALUA" ||
           signature == "ALFA" || signature == "ALRT" || signature == "ALEQ" ||
           signature == "ALEA" || signature == "ALCO" || signature == "ALCA" ||
           signature == "ALCL" ||
           signature == "ALNA" || signature == "ALNT" || signature == "ALFE" ||
           signature == "ALFD" || signature == "ALCC" ||
           signature == "CTDA" || signature == "CIS1" || signature == "CIS2" ||
           signature == "KSIZ" || signature == "KWDA" || signature == "COCT" ||
           signature == "CNTO" || signature == "COED" || signature == "SPOR" || signature == "OCOR" ||
           signature == "GWOR" || signature == "ECOR" || signature == "ALLA" ||
           signature == "ALDN" || signature == "ALFV" || signature == "ALDI" ||
           signature == "ALSP" || signature == "ALFC" || signature == "ALPC" ||
           signature == "VTCK" || signature == "ALED";
}

std::optional<Error> existingFillKeyUnrepresentable(
    const fo4::ConflictGraph& graph,
    const std::optional<fo4::FormKey>& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    bool anyMajor,
    std::string_view slot) {
    if (!key.has_value()) return std::nullopt;
    const auto* winner = graph.winner(*key);
    if (winner == nullptr) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) +
                " does not resolve to a loaded record"
        };
    }
    if (anyMajor) return std::nullopt;
    if (placedTargets && isPlacedSignature(winner->signature)) return std::nullopt;
    const auto signature = winner->signature.to_string();
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    return Error{
        "QuestAuthoringCoverageIncomplete",
        "existing QUST alias " + std::string(slot) + " is a " + signature +
            ", which this adapter cannot reconstruct"
    };
}

std::optional<Error> existingFillUnrepresentable(
    const fo4::forms::QuestAliasFill& fill,
    const fo4::ConflictGraph& graph) {
    return std::visit(
        [&](const auto& value) -> std::optional<Error> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, fo4::forms::QuestSpecificReferenceFill>) {
                if (!value.reference.has_value()) {
                    return Error{
                        "QuestAuthoringCoverageIncomplete",
                        "existing QUST alias forcedReference is null"
                    };
                }
                return existingFillKeyUnrepresentable(
                    graph, value.reference, {}, true, false, "forcedReference");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestUniqueActorFill>) {
                return existingFillKeyUnrepresentable(
                    graph, std::optional<fo4::FormKey>{value.actor}, {"NPC_"}, false, false,
                    "uniqueActor");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestLocationAliasReferenceFill>) {
                return existingFillKeyUnrepresentable(
                    graph, std::optional<fo4::FormKey>{value.reference_type}, {"LCRT"}, false,
                    false, "locationAliasReference");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestExternalAliasReferenceFill>) {
                return existingFillKeyUnrepresentable(
                    graph, value.quest, {"QUST"}, false, false, "externalAlias");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestCreateReferenceFill>) {
                if (value.create_mode != 0 && value.create_mode != 0x8000u) {
                    return Error{
                        "QuestAuthoringCoverageIncomplete",
                        "existing QUST alias createReference create mode cannot be reconstructed"
                    };
                }
                if (value.level > 4u) {
                    return Error{
                        "QuestAuthoringCoverageIncomplete",
                        "existing QUST alias createReference level cannot be reconstructed"
                    };
                }
                return existingFillKeyUnrepresentable(
                    graph, value.object, {}, false, true, "createReference");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestFindMatchingReferenceFill>) {
                if (const auto* near =
                        std::get_if<fo4::forms::QuestMatchingReferenceNearAlias>(
                            &value.selector)) {
                    if (near->type > 1u) {
                        return Error{
                            "QuestAuthoringCoverageIncomplete",
                            "existing QUST alias findMatchingNearAlias type cannot be reconstructed"
                        };
                    }
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestSpecificLocationFill>) {
                return existingFillKeyUnrepresentable(
                    graph, std::optional<fo4::FormKey>{value.location}, {"LCTN"}, false, false,
                    "specificLocation");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestReferenceAliasLocationFill>) {
                return existingFillKeyUnrepresentable(
                    graph, value.keyword, {"KYWD"}, false, false, "referenceAliasLocation");
            } else if constexpr (std::is_same_v<T, fo4::forms::QuestExternalAliasLocationFill>) {
                return existingFillKeyUnrepresentable(
                    graph, value.quest, {"QUST"}, false, false, "externalAliasLocation");
            } else {
                return std::nullopt;
            }
        },
        fill);
}

bool supportedAliasFill(const fo4::forms::QuestAliasFill& fill) {
    return std::holds_alternative<std::monostate>(fill) ||
           std::holds_alternative<fo4::forms::QuestSpecificReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestUniqueActorFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestLocationAliasReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestExternalAliasReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestCreateReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestFindMatchingReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestSpecificLocationFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestReferenceAliasLocationFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestExternalAliasLocationFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestFindMatchingLocationFill>(fill);
}

bool isAliasConditionSignature(std::string_view name) {
    return name == "CTDA" || name == "CIS1" || name == "CIS2";
}

bool isAliasConditionPrefix(fo4::forms::QuestAliasKind kind, std::string_view name) {
    if (name == "ALST" || name == "ALLS" || name == "ALID" || name == "FNAM" ||
        name == "ALFI" || name == "ALFR" || name == "ALUA" || name == "ALFL" ||
        name == "ALFA" || name == "ALRT" || name == "KNAM" || name == "ALEQ" ||
        name == "ALEA" || name == "ALCO" || name == "ALCA" || name == "ALCL" ||
        name == "ALNA" ||
        name == "ALNT" || name == "ALFE" || name == "ALFD") {
        return true;
    }
    return kind == fo4::forms::QuestAliasKind::reference && name == "ALCC";
}

bool isAliasConditionSuccessor(fo4::forms::QuestAliasKind kind, std::string_view name) {
    if (kind == fo4::forms::QuestAliasKind::location) return name == "ALCC";
    return name == "KSIZ" || name == "KWDA" || name == "COCT" || name == "CNTO" ||
           name == "COED" ||
           name == "SPOR" || name == "OCOR" || name == "GWOR" || name == "ECOR" ||
           name == "ALLA" || name == "ALDN" || name == "ALFV" || name == "ALDI" ||
           name == "ALSP" || name == "ALFC" || name == "ALPC" || name == "VTCK";
}

std::optional<Error> zeroFormLinkUnrepresentable(const fo4::forms::QuestAlias& alias) {
    const auto countMembers = [&alias](std::string_view name) {
        return static_cast<std::size_t>(std::count_if(
            alias.subrecords.begin(), alias.subrecords.end(),
            [name](const fo4::Subrecord& member) { return member.signature.to_string() == name; }));
    };
    const auto refuse = [](std::string_view slot) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) +
                " has a zero-valued FormLink this adapter cannot reconstruct"
        };
    };
    std::size_t keywordSlots = 0;
    std::size_t linkedSlots = 0;
    for (const auto& member : alias.subrecords) {
        const auto name = member.signature.to_string();
        if (name == "KWDA") keywordSlots += member.data.size() / 4;
        if (name == "ALLA") linkedSlots += member.data.size() / 8;
    }
    if (keywordSlots != alias.keywords.size()) return refuse("keywords");
    if (linkedSlots != alias.linked_aliases.size()) return refuse("linkedAliases");
    if (countMembers("CNTO") != alias.items.size()) return refuse("items");
    const auto itemsWithExtra = static_cast<std::size_t>(std::count_if(
        alias.items.begin(), alias.items.end(),
        [](const fo4::forms::QuestAliasItem& item) { return item.extra.has_value(); }));
    if (countMembers("COED") != itemsWithExtra) return refuse("items");
    if (countMembers("ALSP") != alias.spells.size()) return refuse("spells");
    if (countMembers("ALFC") != alias.factions.size()) return refuse("factions");
    if (countMembers("ALPC") != alias.packages.size()) return refuse("packages");

    struct OptionalLink {
        std::string_view signature;
        std::string_view slot;
        bool present;
    };
    const OptionalLink optionalLinks[] = {
        {"SPOR", "spectatorOverridePackageList", alias.spectator_override.has_value()},
        {"OCOR", "observeDeadBodyOverridePackageList", alias.observe_dead_body_override.has_value()},
        {"GWOR", "guardWarnOverridePackageList", alias.guard_warn_override.has_value()},
        {"ECOR", "combatOverridePackageList", alias.combat_override.has_value()},
        {"ALDN", "displayName", alias.display_name.has_value()},
        {"ALFV", "forcedVoice", alias.forced_voice.has_value()},
        {"ALDI", "deathItem", alias.death_item.has_value()},
        {"VTCK", "voiceTypes", alias.voice_types.has_value()},
    };
    for (const auto& link : optionalLinks) {
        const std::size_t expected = link.present ? 1 : 0;
        if (countMembers(link.signature) != expected) return refuse(link.slot);
    }

    std::size_t questLinks = 0;
    if (const auto* externalReference =
            std::get_if<fo4::forms::QuestExternalAliasReferenceFill>(&alias.fill)) {
        if (externalReference->quest.has_value()) ++questLinks;
    }
    if (const auto* externalLocation =
            std::get_if<fo4::forms::QuestExternalAliasLocationFill>(&alias.fill)) {
        if (externalLocation->quest.has_value()) ++questLinks;
    }
    if (countMembers("ALEQ") != questLinks) return refuse("externalAlias quest");
    return std::nullopt;
}

std::optional<Error> aliasConditionsUnrepresentable(
    fo4::forms::QuestAliasKind kind,
    const std::vector<fo4::Subrecord>& members,
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph) {
    std::optional<std::size_t> begin;
    std::optional<std::size_t> end;
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto name = members[i].signature.to_string();
        if (name == "ALED") break;
        if (!isAliasConditionSignature(name)) continue;
        if (end.has_value() && i != *end) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias conditions are not a contiguous CTDA/CIS run"
            };
        }
        if (!begin.has_value()) begin = i;
        end = i + 1;
    }
    if (!begin.has_value()) return std::nullopt;
    for (std::size_t i = 0; i < *begin; ++i) {
        if (!isAliasConditionPrefix(kind, members[i].signature.to_string())) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias conditions are out of order"
            };
        }
    }
    const std::span<const fo4::Subrecord> run{members.data() + *begin, *end - *begin};
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    if (!fo4::forms::lossless_quest_log_entry_conditions(run, conditions, consumed) ||
        consumed != run.size()) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias has a CTDA/CIS run this adapter cannot reconstruct"
        };
    }
    for (const auto& condition : conditions) {
        if (const auto error =
                adapterConditionUnrepresentable(plugin, graph, condition, "alias")) {
            return error;
        }
    }
    for (std::size_t i = *end; i < members.size(); ++i) {
        const auto name = members[i].signature.to_string();
        if (name == "ALED") break;
        if (!isAliasConditionSuccessor(kind, name)) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias conditions are out of order"
            };
        }
    }
    return std::nullopt;
}

}

std::optional<Error> existingQuestVmadUnrepresentable(const fo4::Record& record,
                                                      bool aliases,
                                                      bool stages) {
    const fo4::Subrecord* vmad = nullptr;
    std::size_t vmadCount = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_vmad) continue;
        ++vmadCount;
        if (vmad == nullptr) vmad = &subrecord;
    }
    if (vmadCount == 0) return std::nullopt;
    if (vmadCount > 1) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST has more than one VMAD subrecord"
        };
    }
    const auto bindings = fo4::quest_vmad_bindings(vmad->data);
    if (bindings.unparseable) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST VMAD cannot be inspected for alias or stage bindings"
        };
    }
    (void)aliases;
    (void)stages;
    return std::nullopt;
}

fo4::FormLinkTypeLookup questOwnerTypes(const fo4::Plugin& plugin, const fo4::ConflictGraph& graph) {
    return [&plugin, &graph](const fo4::FormKey& owner) -> std::optional<fo4::Signature> {
        if (const auto* record = fo4::find_record(plugin, owner)) return record->header.signature;
        const auto* winner = graph.winner(owner);
        if (winner == nullptr) {
            throw fo4::forms::QuestError(
                "QUST alias item extra owner " + owner.to_string() +
                " does not resolve to a loaded record");
        }
        return winner->signature;
    };
}

std::optional<Error> existingAliasesUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::FormKey& quest,
    const fo4::ConflictGraph& graph) {
    const auto decoded = fo4::forms::decode_quest_aliases(
        plugin, quest, questOwnerTypes(plugin, graph));
    for (const auto& alias : decoded.aliases) {
        if (!alias.unresolved_links.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias has an unresolved FormLink"
            };
        }
        if (const auto error = zeroFormLinkUnrepresentable(alias)) {
            return error;
        }
        if (!supportedAliasFill(alias.fill)) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias uses a Fill Type this adapter cannot reconstruct"
            };
        }
        if (const auto error = existingFillUnrepresentable(alias.fill, graph)) {
            return error;
        }
        if (alias.kind == fo4::forms::QuestAliasKind::collection &&
            alias.collection_entries.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST collection alias has no ALCS entries"
            };
        }
        for (const auto& member : alias.subrecords) {
            const auto signature = member.signature.to_string();
            if (!allowedAliasMember(alias.kind, signature)) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST alias has unsupported member " + signature
                };
            }
        }
        if (alias.kind == fo4::forms::QuestAliasKind::reference) {
            if (const auto error =
                    extraAliasMembersUnrepresentable(alias.subrecords, plugin, graph)) {
                return error;
            }
        }
        if (alias.kind == fo4::forms::QuestAliasKind::reference ||
            alias.kind == fo4::forms::QuestAliasKind::location) {
            if (const auto error =
                    aliasConditionsUnrepresentable(
                        alias.kind, alias.subrecords, plugin, graph)) {
                return error;
            }
        }
    }
    return std::nullopt;
}

}
