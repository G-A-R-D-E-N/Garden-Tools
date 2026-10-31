#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

std::vector<fo4::CtdaCondition> previousAliasConditions(const fo4::forms::QuestAliases& previous,
                                                        std::uint32_t id) {
    const auto sameId = [id](const fo4::forms::QuestAlias& alias) { return alias.id == id; };
    std::vector<fo4::CtdaCondition> conditions;
    if (std::count_if(previous.aliases.begin(), previous.aliases.end(), sameId) != 1) {
        return conditions;
    }
    const auto& members =
        std::find_if(previous.aliases.begin(), previous.aliases.end(), sameId)->subrecords;
    const auto isCondition = [](const fo4::Subrecord& member) {
        return member.signature == fo4::sig_ctda ||
               member.signature == fo4::Signature::from_string("CIS1") ||
               member.signature == fo4::Signature::from_string("CIS2");
    };
    const auto first = std::find_if(members.begin(), members.end(), isCondition);
    const auto last = std::find_if_not(first, members.end(), isCondition);
    const std::span<const fo4::Subrecord> run{
        members.data() + (first - members.begin()), static_cast<std::size_t>(last - first)};
    std::size_t consumed = 0;
    if (!fo4::forms::lossless_quest_log_entry_conditions(run, conditions, consumed) ||
        consumed != run.size()) {
        conditions.clear();
    }
    return conditions;
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

        std::optional<std::string> name;
        if (!questOptionalString(element, "name", name)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "name must be a string or null"
            });
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

        const bool hasExtras =
            (element.contains("keywords") && !element["keywords"].is_null()) ||
            (element.contains("items") && !element["items"].is_null()) ||
            (element.contains("linkedAliases") && !element["linkedAliases"].is_null()) ||
            (element.contains("spells") && !element["spells"].is_null()) ||
            (element.contains("factions") && !element["factions"].is_null()) ||
            (element.contains("packages") && !element["packages"].is_null()) ||
            jsonObjectPresent(element, "spectatorOverridePackageList") ||
            jsonObjectPresent(element, "observeDeadBodyOverridePackageList") ||
            jsonObjectPresent(element, "guardWarnOverridePackageList") ||
            jsonObjectPresent(element, "combatOverridePackageList") ||
            jsonObjectPresent(element, "displayName") ||
            jsonObjectPresent(element, "forcedVoice") ||
            jsonObjectPresent(element, "deathItem") ||
            jsonObjectPresent(element, "voiceTypes");
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
        pendingConditions.push_back(std::move(parsedConditions));
        creations.push_back(std::move(creation));
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& key : fillKeys) ensureOwnerMaster(mutated, key.owner());
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
            carryConditionPadding(previousAliasConditions(previousAliases, creations[i].id),
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
