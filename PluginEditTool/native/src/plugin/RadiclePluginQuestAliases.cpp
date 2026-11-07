#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

const fo4::forms::QuestAlias* previousAliasWithId(const fo4::forms::QuestAliases& previous,
                                                  std::uint32_t id) {
    const auto found = std::find_if(
        previous.aliases.begin(), previous.aliases.end(), [id](const fo4::forms::QuestAlias& alias) {
            return alias.kind != fo4::forms::QuestAliasKind::collection && alias.id == id;
        });
    return found == previous.aliases.end() ? nullptr : &*found;
}

std::vector<fo4::CtdaCondition> previousAliasConditions(const fo4::forms::QuestAliases& previous,
                                                        std::uint32_t id) {
    std::vector<fo4::CtdaCondition> conditions;
    if (const auto* alias = previousAliasWithId(previous, id)) {
        aliasConditionRun(*alias, conditions);
    }
    return conditions;
}

bool fillFitsKind(const fo4::forms::QuestAliasFill& fill, fo4::forms::QuestAliasKind kind) {
    if (std::holds_alternative<std::monostate>(fill)) return true;
    const bool referenceFill =
        std::holds_alternative<fo4::forms::QuestSpecificReferenceFill>(fill) ||
        std::holds_alternative<fo4::forms::QuestUniqueActorFill>(fill) ||
        std::holds_alternative<fo4::forms::QuestLocationAliasReferenceFill>(fill) ||
        std::holds_alternative<fo4::forms::QuestExternalAliasReferenceFill>(fill) ||
        std::holds_alternative<fo4::forms::QuestCreateReferenceFill>(fill) ||
        std::holds_alternative<fo4::forms::QuestFindMatchingReferenceFill>(fill);
    if (kind == fo4::forms::QuestAliasKind::reference) return referenceFill;
    if (kind == fo4::forms::QuestAliasKind::location) return !referenceFill;
    return false;
}

bool anyFillKeyPresent(const nlohmann::json& element) {
    constexpr std::array<const char*, 12> keys{
        "forcedReference", "uniqueActor", "specificLocation", "createReference",
        "locationAliasReference", "externalAlias", "findMatchingNearAlias",
        "findMatchingFromEvent", "findMatchingClosest", "referenceAliasLocation",
        "externalAliasLocation", "findMatchingLocation"};
    return std::any_of(keys.begin(), keys.end(), [&element](const char* key) {
        return element.contains(key);
    });
}

template <typename Alias>
bool aliasHasExtras(const Alias& alias) {
    return !alias.keywords.empty() || !alias.items.empty() || !alias.linked_aliases.empty() ||
           !alias.spells.empty() || !alias.factions.empty() || !alias.packages.empty() ||
           alias.spectator_override.has_value() || alias.observe_dead_body_override.has_value() ||
           alias.guard_warn_override.has_value() || alias.combat_override.has_value() ||
           alias.display_name.has_value() || alias.forced_voice.has_value() ||
           alias.death_item.has_value() || alias.voice_types.has_value();
}

void carryOmittedExtras(const nlohmann::json& element,
                        const fo4::forms::QuestAlias& previous,
                        fo4::forms::QuestAliasCreation& creation) {
    const auto omitted = [&element](const char* key) { return !element.contains(key); };
    if (omitted("keywords")) creation.keywords = previous.keywords;
    if (omitted("items")) creation.items = previous.items;
    if (omitted("linkedAliases")) creation.linked_aliases = previous.linked_aliases;
    if (omitted("spells")) creation.spells = previous.spells;
    if (omitted("factions")) creation.factions = previous.factions;
    if (omitted("packages")) creation.packages = previous.packages;
    if (omitted("spectatorOverridePackageList")) {
        creation.spectator_override = previous.spectator_override;
    }
    if (omitted("observeDeadBodyOverridePackageList")) {
        creation.observe_dead_body_override = previous.observe_dead_body_override;
    }
    if (omitted("guardWarnOverridePackageList")) {
        creation.guard_warn_override = previous.guard_warn_override;
    }
    if (omitted("combatOverridePackageList")) creation.combat_override = previous.combat_override;
    if (omitted("displayName")) creation.display_name = previous.display_name;
    if (omitted("forcedVoice")) creation.forced_voice = previous.forced_voice;
    if (omitted("deathItem")) creation.death_item = previous.death_item;
    if (omitted("voiceTypes")) creation.voice_types = previous.voice_types;
}

std::optional<Error> collectionReplacementLoses(const fo4::forms::QuestAliases& previous,
                                                std::uint32_t id) {
    const auto* alias = previousAliasWithId(previous, id);
    if (alias == nullptr) return std::nullopt;
    std::vector<fo4::CtdaCondition> conditions;
    const bool conditionsRun = aliasConditionRun(*alias, conditions);
    if (!std::holds_alternative<std::monostate>(alias->fill) || aliasHasExtras(*alias) ||
        !conditions.empty() || !conditionsRun || alias->force_into_alias.has_value() ||
        alias->closest_to_alias.has_value()) {
        return Error{
            "InvalidQuestAliasPayload",
            "collection entry id " + std::to_string(id) +
                " is the id of an existing reference or location alias that has a fill, "
                "extras, conditions, forceIntoAlias, or closestToAlias; remove those from "
                "that alias before converting it to a collection"
        };
    }
    return std::nullopt;
}

bool knownCollectionEntryKey(std::string_view key) {
    return key == "id" || key == "maxInitialFillCount";
}

bool knownAliasKey(std::string_view key) {
    return key == "kind" || key == "id" || key == "name" || key == "flags" ||
           key == "forceIntoAlias" || key == "closestToAlias" ||
           key == "maxInitialFillCount" || key == "forcedReference" ||
           key == "uniqueActor" || key == "specificLocation" ||
           key == "createReference" || key == "locationAliasReference" ||
           key == "externalAlias" || key == "findMatchingNearAlias" ||
           key == "findMatchingFromEvent" || key == "findMatchingClosest" ||
           key == "referenceAliasLocation" || key == "externalAliasLocation" ||
           key == "findMatchingLocation" ||
           key == "keywords" || key == "items" || key == "linkedAliases" ||
           key == "spells" || key == "factions" || key == "packages" ||
           key == "spectatorOverridePackageList" ||
           key == "observeDeadBodyOverridePackageList" ||
           key == "guardWarnOverridePackageList" ||
           key == "combatOverridePackageList" || key == "displayName" ||
           key == "forcedVoice" || key == "deathItem" || key == "voiceTypes" ||
           key == "conditions" || key == "entries";
}

bool knownItemKey(std::string_view key) {
    return key == "item" || key == "count" || key == "extra";
}

bool knownItemExtraKey(std::string_view key) {
    return key == "owner" || key == "global" || key == "requiredRank" ||
           key == "itemCondition";
}

bool knownLinkedAliasKey(std::string_view key) {
    return key == "keyword" || key == "alias";
}

}

Result<nlohmann::json> RadiclePluginBackend::setQuestAliases(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    const auto* aliasesJson = questArrayArg(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (aliasesJson == nullptr) {
        return std::unexpected(Error{
            "InvalidQuestAliasPayload",
            "SetQuestAliases requires a JSON array of alias objects"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey questKey;
    try {
        questKey = fo4::FormKey::parse(recordText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    const auto* record = fo4::find_record(loaded->plugin, questKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + recordText
        });
    }
    if (record->header.signature != fo4::Signature::from_string("QUST")) {
        return std::unexpected(Error{
            "RecordTypeMismatch",
            "SetQuestAliases requires a QUST record"
        });
    }
    if (const auto error = existingQuestVmadUnrepresentable(*record, true, false)) {
        return std::unexpected(*error);
    }
    fo4::forms::QuestAliases previousAliases;
    try {
        if (const auto error =
                existingAliasesUnrepresentable(loaded->plugin, questKey, graph_)) {
            return std::unexpected(*error);
        }
        previousAliases = fo4::forms::decode_quest_aliases(loaded->plugin, questKey);
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }

    std::vector<fo4::forms::QuestAliasCreation> creations;
    std::vector<std::string> nameStorage;
    creations.reserve(aliasesJson->size());
    nameStorage.reserve(aliasesJson->size());
    std::uint32_t autoId = 0;
    std::vector<fo4::FormKey> fillKeys;
    std::vector<std::vector<PendingQuestCondition>> pendingConditions;
    std::vector<bool> carryConditionRuns;
    bool previousCollection = false;

    for (const auto& element : *aliasesJson) {
        if (!element.is_object()) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "Every quest alias must be an object"
            });
        }
        for (auto it = element.begin(); it != element.end(); ++it) {
            if (!knownAliasKey(it.key())) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "Unsupported quest alias field '" + it.key() + "'"
                });
            }
        }

        fo4::forms::QuestAliasCreation creation;
        std::string kindText{"reference"};
        const auto kindField = element.find("kind");
        if (kindField != element.end() && !kindField->is_null()) {
            if (!kindField->is_string()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "kind must be a string"
                });
            }
            kindText = kindField->get<std::string>();
        }
        if (kindText == "reference") creation.kind = fo4::forms::QuestAliasKind::reference;
        else if (kindText == "location") creation.kind = fo4::forms::QuestAliasKind::location;
        else if (kindText == "collection") creation.kind = fo4::forms::QuestAliasKind::collection;
        else {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "kind must be reference, location, or collection"
            });
        }
        const bool isCollection = creation.kind == fo4::forms::QuestAliasKind::collection;
        if (isCollection && previousCollection) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "collection aliases cannot be adjacent; contiguous ALCS runs are one alias"
            });
        }
        previousCollection = isCollection;

        const bool hasEntries =
            element.contains("entries") && !element["entries"].is_null();
        const bool hasId = element.contains("id") && !element["id"].is_null();
        const bool hasMaxFill =
            element.contains("maxInitialFillCount") &&
            !element["maxInitialFillCount"].is_null();
        if (hasEntries) {
            if (creation.kind != fo4::forms::QuestAliasKind::collection) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "entries is valid only on collection aliases"
                });
            }
            if (hasId || hasMaxFill) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "collection entries cannot mix with top-level id or maxInitialFillCount"
                });
            }
            if (!element["entries"].is_array() || element["entries"].empty()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "collection entries must be a non-empty array"
                });
            }
            std::set<std::uint32_t> entryIds;
            for (const auto& entryJson : element["entries"]) {
                if (!entryJson.is_object()) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "every collection entry must be an object"
                    });
                }
                for (auto it = entryJson.begin(); it != entryJson.end(); ++it) {
                    if (!knownCollectionEntryKey(it.key())) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "Unsupported collection entry field '" + it.key() + "'"
                        });
                    }
                }
                fo4::forms::QuestCollectionEntry entry;
                if (!entryJson.contains("id") || entryJson["id"].is_null() ||
                    !questUnsigned(entryJson, "id", 0, entry.id)) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "collection entry id must be an unsigned 32-bit value"
                    });
                }
                if (!entryIds.insert(entry.id).second) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "collection aliases cannot contain duplicate ALCS ids"
                    });
                }
                if (!questU8(entryJson, "maxInitialFillCount",
                             entry.max_initial_fill_count)) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "collection entry maxInitialFillCount has an invalid type"
                    });
                }
                creation.collection_entries.push_back(std::move(entry));
            }
            creation.id = creation.collection_entries.front().id;
        } else {
            if (!questUnsigned(element, "id", autoId, creation.id)) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "alias id must be an unsigned 32-bit value"
                });
            }
            if (!isCollection) {
                autoId = creation.id + 1;
            }
        }

        if (isCollection) {
            std::vector<std::uint32_t> collectionIds;
            if (creation.collection_entries.empty()) collectionIds.push_back(creation.id);
            for (const auto& entry : creation.collection_entries) {
                collectionIds.push_back(entry.id);
            }
            for (const auto id : collectionIds) {
                if (const auto error = collectionReplacementLoses(previousAliases, id)) {
                    return std::unexpected(*error);
                }
            }
        }
        const fo4::forms::QuestAlias* previous =
            isCollection ? nullptr : previousAliasWithId(previousAliases, creation.id);

        std::optional<std::string> name;
        if (!questOptionalString(element, "name", name)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "name must be a string or null"
            });
        }
        if (!element.contains("name") && previous != nullptr) {
            name = previous->name;
        }
        if (creation.kind == fo4::forms::QuestAliasKind::reference && !name.has_value()) {
            name = "Alias" + std::to_string(creation.id);
        }
        if (name.has_value()) {
            nameStorage.push_back(*name);
            creation.name = nameStorage.back();
        }

        std::uint32_t flags = 0;
        const bool hasFlags = element.contains("flags") && !element["flags"].is_null();
        if (!questUnsigned(element, "flags", 0, flags)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "flags must be an unsigned 32-bit value"
            });
        }
        if (creation.kind != fo4::forms::QuestAliasKind::collection) {
            creation.flags = flags;
        } else if (hasFlags) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "collection aliases do not use flags"
            });
        }

        if (!questInt32(element, "forceIntoAlias", creation.force_into_alias) ||
            !questInt32(element, "closestToAlias", creation.closest_to_alias) ||
            !questU8(element, "maxInitialFillCount", creation.max_initial_fill_count)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "forceIntoAlias, closestToAlias, and maxInitialFillCount have invalid types"
            });
        }

        if (const auto error = parseAliasFill(
                element, creation.kind, graph_, creation.fill, fillKeys)) {
            return std::unexpected(*error);
        }

        const auto extraPresent = [&element](const char* key) {
            if (!element.contains(key) || element[key].is_null()) return false;
            return !element[key].is_array() || !element[key].empty();
        };
        const bool hasExtras =
            extraPresent("keywords") || extraPresent("items") ||
            extraPresent("linkedAliases") || extraPresent("spells") ||
            extraPresent("factions") || extraPresent("packages") ||
            extraPresent("spectatorOverridePackageList") ||
            extraPresent("observeDeadBodyOverridePackageList") ||
            extraPresent("guardWarnOverridePackageList") ||
            extraPresent("combatOverridePackageList") || extraPresent("displayName") ||
            extraPresent("forcedVoice") || extraPresent("deathItem") ||
            extraPresent("voiceTypes");
        if (hasExtras && creation.kind != fo4::forms::QuestAliasKind::reference) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "keywords, items, linkedAliases, package-list overrides, displayName, "
                "forcedVoice, deathItem, voiceTypes, spells, factions, and packages "
                "are valid only on reference aliases"
            });
        }
        if (const auto error = parseAliasFormKeyList(
                element, "keywords", graph_, {"KYWD"}, false, creation.keywords, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseAliasFormKeyList(
                element, "spells", graph_, {"SPEL"}, false, creation.spells, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseAliasFormKeyList(
                element, "factions", graph_, {"FACT"}, false, creation.factions, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseAliasFormKeyList(
                element, "packages", graph_, {"PACK"}, false, creation.packages, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "spectatorOverridePackageList", graph_, {"FLST"},
                creation.spectator_override, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "observeDeadBodyOverridePackageList", graph_, {"FLST"},
                creation.observe_dead_body_override, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "guardWarnOverridePackageList", graph_, {"FLST"},
                creation.guard_warn_override, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "combatOverridePackageList", graph_, {"FLST"},
                creation.combat_override, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "displayName", graph_, {"MESG"}, creation.display_name, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "forcedVoice", graph_, {"VTYP"}, creation.forced_voice, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "deathItem", graph_, {"LVLI"}, creation.death_item, fillKeys)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseOptionalTypedFormKey(
                element, "voiceTypes", graph_, {"VTYP", "FLST", "NPC_", "FACT"},
                creation.voice_types, fillKeys)) {
            return std::unexpected(*error);
        }
        if (element.contains("items") && !element["items"].is_null()) {
            if (!element["items"].is_array()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "items must be an array"
                });
            }
            for (const auto& itemJson : element["items"]) {
                if (!itemJson.is_object() || !itemJson.contains("item") ||
                    !itemJson["item"].is_string()) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "every item must be an object with an item FormKey"
                    });
                }
                for (auto it = itemJson.begin(); it != itemJson.end(); ++it) {
                    if (!knownItemKey(it.key())) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "Unsupported item field '" + it.key() + "'"
                        });
                    }
                }
                fo4::forms::QuestAliasItem item;
                try {
                    item.item = fo4::FormKey::parse(itemJson["item"].get<std::string>());
                } catch (const std::exception& error) {
                    return std::unexpected(Error{"InvalidFormKey", error.what()});
                }
                const auto* winner = (*graph_).winner(item.item);
                if (winner == nullptr) {
                    return std::unexpected(Error{
                        "InvalidTypedFormKey",
                        "items must resolve to a loaded record: " + item.item.to_string()
                    });
                }
                if (!fo4::schema::is_sig_base_object(winner->signature)) {
                    return std::unexpected(Error{
                        "InvalidTypedFormKey",
                        "items " + item.item.to_string() + " is a " +
                            winner->signature.to_string() + ", but this slot holds an item"
                    });
                }
                fillKeys.push_back(item.item);
                std::uint32_t count = 1;
                if (!questUnsigned(itemJson, "count", 1, count) || count == 0 ||
                    count > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "item count must be 1..2147483647"
                    });
                }
                item.count = static_cast<std::int32_t>(count);
                if (itemJson.contains("extra") && !itemJson["extra"].is_null()) {
                    if (!itemJson["extra"].is_object()) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "item extra must be an object"
                        });
                    }
                    const auto& extraJson = itemJson["extra"];
                    for (auto it = extraJson.begin(); it != extraJson.end(); ++it) {
                        if (!knownItemExtraKey(it.key())) {
                            return std::unexpected(Error{
                                "InvalidQuestAliasPayload",
                                "Unsupported item extra field '" + it.key() + "'"
                            });
                        }
                    }
                    fo4::forms::QuestAliasItemExtra extra;
                    if (const auto error = parseOptionalTypedFormKey(
                            extraJson, "owner", graph_, {"NPC_", "FACT"}, extra.owner,
                            fillKeys)) {
                        return std::unexpected(*error);
                    }
                    if (const auto error = parseOptionalTypedFormKey(
                            extraJson, "global", graph_, {"GLOB"}, extra.global, fillKeys)) {
                        return std::unexpected(*error);
                    }
                    if (!questInt32(extraJson, "requiredRank", extra.required_rank)) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "item extra requiredRank must be an integer"
                        });
                    }
                    if (!questFloat(extraJson, "itemCondition", 0.0f, extra.item_condition)) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "item extra itemCondition must be a finite number"
                        });
                    }
                    if (extra.global.has_value() && extra.required_rank.has_value()) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "item extra cannot set both global and requiredRank"
                        });
                    }
                    if (extra.global.has_value()) {
                        if (!extra.owner.has_value()) {
                            return std::unexpected(Error{
                                "InvalidQuestAliasPayload",
                                "item extra global requires an NPC_ owner"
                            });
                        }
                        const auto* ownerWinner = (*graph_).winner(*extra.owner);
                        if (ownerWinner == nullptr ||
                            ownerWinner->signature.to_string() != "NPC_") {
                            return std::unexpected(Error{
                                "InvalidQuestAliasPayload",
                                "item extra global requires an NPC_ owner"
                            });
                        }
                    }
                    if (extra.required_rank.has_value()) {
                        if (!extra.owner.has_value()) {
                            return std::unexpected(Error{
                                "InvalidQuestAliasPayload",
                                "item extra requiredRank requires a FACT owner"
                            });
                        }
                        const auto* ownerWinner = (*graph_).winner(*extra.owner);
                        if (ownerWinner == nullptr ||
                            ownerWinner->signature.to_string() != "FACT") {
                            return std::unexpected(Error{
                                "InvalidQuestAliasPayload",
                                "item extra requiredRank requires a FACT owner"
                            });
                        }
                    }
                    item.extra = std::move(extra);
                }
                creation.items.push_back(std::move(item));
            }
        }
        if (element.contains("linkedAliases") && !element["linkedAliases"].is_null()) {
            if (!element["linkedAliases"].is_array()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "linkedAliases must be an array"
                });
            }
            for (const auto& linkedJson : element["linkedAliases"]) {
                if (!linkedJson.is_object() || !linkedJson.contains("keyword") ||
                    !linkedJson["keyword"].is_string()) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "every linked alias must be an object with a keyword FormKey"
                    });
                }
                for (auto it = linkedJson.begin(); it != linkedJson.end(); ++it) {
                    if (!knownLinkedAliasKey(it.key())) {
                        return std::unexpected(Error{
                            "InvalidQuestAliasPayload",
                            "Unsupported linked alias field '" + it.key() + "'"
                        });
                    }
                }
                fo4::forms::QuestLinkedAlias linked;
                try {
                    linked.keyword = fo4::FormKey::parse(linkedJson["keyword"].get<std::string>());
                } catch (const std::exception& error) {
                    return std::unexpected(Error{"InvalidFormKey", error.what()});
                }
                if (const auto error = requireLoadedFill(
                        graph_, linked.keyword, {"KYWD"}, false, "linkedAliases")) {
                    return std::unexpected(*error);
                }
                fillKeys.push_back(linked.keyword);
                std::optional<std::int32_t> aliasId;
                if (!questInt32(linkedJson, "alias", aliasId) || !aliasId.has_value()) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "linked alias alias id must be an integer"
                    });
                }
                linked.alias = *aliasId;
                creation.linked_aliases.push_back(std::move(linked));
            }
        }
        std::vector<PendingQuestCondition> parsedConditions;
        if (element.contains("conditions") && !element["conditions"].is_null()) {
            if (creation.kind == fo4::forms::QuestAliasKind::collection) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "conditions are valid only on reference and location aliases"
                });
            }
            if (!element["conditions"].is_array()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "conditions must be an array"
                });
            }
            for (const auto& conditionJson : element["conditions"]) {
                PendingQuestCondition pending;
                if (const auto error = parseQuestCondition(
                        conditionJson, graph_, pending, fillKeys,
                        "InvalidQuestAliasPayload", "quest aliases")) {
                    if (error->type == "InvalidQuestStagePayload") {
                        auto remapped = *error;
                        remapped.type = "InvalidQuestAliasPayload";
                        return std::unexpected(std::move(remapped));
                    }
                    return std::unexpected(*error);
                }
                parsedConditions.push_back(std::move(pending));
            }
        }

        if (previous != nullptr) {
            if (!element.contains("flags") && previous->flags.has_value()) {
                creation.flags = previous->flags;
            }
            if (!element.contains("forceIntoAlias")) {
                creation.force_into_alias = previous->force_into_alias;
            }
            if (!element.contains("closestToAlias")) {
                if (creation.kind == fo4::forms::QuestAliasKind::location) {
                    creation.closest_to_alias = previous->closest_to_alias;
                } else if (previous->closest_to_alias.has_value()) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "closestToAlias of the previous location alias cannot carry to a "
                        "reference alias; send closestToAlias explicitly"
                    });
                }
            }
            if (!anyFillKeyPresent(element)) {
                if (!fillFitsKind(previous->fill, creation.kind)) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "the previous Fill Type cannot carry to this alias kind; send the "
                        "fill explicitly"
                    });
                }
                creation.fill = previous->fill;
            }
            if (creation.kind == fo4::forms::QuestAliasKind::reference) {
                carryOmittedExtras(element, *previous, creation);
            } else {
                fo4::forms::QuestAliasCreation omittedExtras;
                carryOmittedExtras(element, *previous, omittedExtras);
                if (aliasHasExtras(omittedExtras)) {
                    return std::unexpected(Error{
                        "InvalidQuestAliasPayload",
                        "the previous reference alias has reference-only data (keywords, items, "
                        "linkedAliases, spells, factions, packages, package-list overrides, "
                        "displayName, forcedVoice, deathItem, voiceTypes) that cannot carry to a "
                        "location alias; remove that data from the alias before changing its kind"
                    });
                }
            }
        }
        carryConditionRuns.push_back(previous != nullptr && !element.contains("conditions"));

        pendingConditions.push_back(std::move(parsedConditions));
        creations.push_back(std::move(creation));
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& key : fillKeys) ensureOwnerMaster(mutated, key.owner());
        const auto remappedAliases = fo4::forms::decode_quest_aliases(mutated, questKey);
        for (std::size_t i = 0; i < creations.size(); ++i) {
            if (!carryConditionRuns[i]) continue;
            const auto* carriedFrom = previousAliasWithId(remappedAliases, creations[i].id);
            if (carriedFrom == nullptr ||
                !aliasConditionRun(*carriedFrom, creations[i].conditions)) {
                return std::unexpected(Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST alias has a CTDA/CIS run this adapter cannot reconstruct"
                });
            }
        }
        const auto context = fo4::plugin_form_id_context(mutated);
        const auto encodeKey = [&](const std::optional<fo4::FormKey>& key) -> std::uint32_t {
            if (!key.has_value()) return 0;
            return fo4::encode_plugin_form_id(*key, context).value();
        };
        for (std::size_t i = 0; i < creations.size(); ++i) {
            const auto& pending = pendingConditions[i];
            if (pending.empty()) continue;
            creations[i].conditions.reserve(pending.size());
            for (auto condition : pending) {
                if (condition.param1.has_value()) {
                    condition.condition.param1 =
                        static_cast<std::int32_t>(encodeKey(condition.param1));
                }
                if (condition.param2.has_value()) {
                    condition.condition.param2 =
                        static_cast<std::int32_t>(encodeKey(condition.param2));
                }
                if (condition.reference.has_value()) {
                    condition.condition.reference = encodeKey(condition.reference);
                }
                if (condition.compareGlobal.has_value()) {
                    condition.condition.comparison_raw = encodeKey(condition.compareGlobal);
                }
                creations[i].conditions.push_back(std::move(condition.condition));
            }
            carryConditionPadding(previousAliasConditions(remappedAliases, creations[i].id),
                                  creations[i].conditions);
        }
        fo4::forms::write_quest_aliases(mutated, questKey, creations);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set " + std::to_string(creations.size()) + " quest alias(es) on " +
            recordText + " in " + pluginName + ". SavePlugin to persist.");
    } catch (const fo4::forms::QuestError& error) {
        const std::string message = error.what();
        if (message.find("QUST VMAD") != std::string::npos) {
            return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", message});
        }
        return std::unexpected(Error{"SetQuestAliasesFailed", message});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestAliasesFailed", error.what()});
    }
}

}
