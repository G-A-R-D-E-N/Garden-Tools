#include "RadiclePluginQuestInternal.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/schema/formlink_lists.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

bool knownCreateReferenceKey(std::string_view key) {
    return key == "object" || key == "alias" || key == "create" || key == "level";
}

bool knownLocationAliasReferenceKey(std::string_view key) {
    return key == "alias" || key == "referenceType";
}

bool knownExternalAliasKey(std::string_view key) {
    return key == "quest" || key == "alias";
}

bool knownNearAliasKey(std::string_view key) {
    return key == "alias" || key == "type";
}

bool knownEventDataKey(std::string_view key) {
    return key == "event" || key == "data";
}

bool knownClosestAliasKey(std::string_view key) {
    return key == "alias";
}

bool knownReferenceAliasLocationKey(std::string_view key) {
    return key == "alias" || key == "keyword";
}

std::optional<Error> requireJsonObject(
    const nlohmann::json& object, const char* key, const nlohmann::json*& out) {
    out = nullptr;
    if (!jsonObjectPresent(object, key)) return std::nullopt;
    if (!object[key].is_object()) {
        return Error{
            "InvalidQuestAliasPayload",
            std::string(key) + " must be an object"
        };
    }
    out = &object[key];
    return std::nullopt;
}

std::optional<Error> requireInt32Field(
    const nlohmann::json& object, const char* key, std::int32_t& out) {
    std::optional<std::int32_t> parsed;
    if (!questInt32(object, key, parsed) || !parsed.has_value()) {
        return Error{
            "InvalidQuestAliasPayload",
            std::string(key) + " must be an integer"
        };
    }
    out = *parsed;
    return std::nullopt;
}

std::optional<Error> requireU32Field(
    const nlohmann::json& object, const char* key, std::uint32_t& out) {
    if (!questUnsigned(object, key, 0, out) ||
        !object.contains(key) || object[key].is_null()) {
        return Error{
            "InvalidQuestAliasPayload",
            std::string(key) + " must be an unsigned 32-bit value"
        };
    }
    return std::nullopt;
}

std::optional<Error> parseCreateMode(const nlohmann::json& object, std::uint16_t& out) {
    out = 0;
    const auto found = object.find("create");
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (found->is_string()) {
        const auto text = found->get<std::string>();
        if (text == "At" || text == "0") {
            out = 0;
            return std::nullopt;
        }
        if (text == "In") {
            out = 0x8000;
            return std::nullopt;
        }
        return Error{"InvalidQuestAliasPayload", "create must be At or In"};
    }
    if (found->is_number_unsigned() || found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed == 0) {
            out = 0;
            return std::nullopt;
        }
        if (parsed == 0x8000) {
            out = 0x8000;
            return std::nullopt;
        }
        return Error{"InvalidQuestAliasPayload", "create must be At (0) or In (0x8000)"};
    }
    return Error{"InvalidQuestAliasPayload", "create must be At or In"};
}

std::optional<Error> parseCreateLevel(const nlohmann::json& object, std::uint32_t& out) {
    out = 0;
    const auto found = object.find("level");
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (found->is_string()) {
        const auto text = found->get<std::string>();
        constexpr std::array<std::string_view, 5> names{"Easy", "Medium", "Hard", "VeryHard", "None"};
        for (std::uint32_t level = 0; level < names.size(); ++level) {
            if (text == names[level] || text == std::to_string(level)) {
                out = level;
                return std::nullopt;
            }
        }
        return Error{
            "InvalidQuestAliasPayload",
            "createReference.level must be Easy, Medium, Hard, VeryHard, or None"
        };
    }
    if (found->is_number_unsigned() || found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 || parsed > 4) {
            return Error{
                "InvalidQuestAliasPayload",
                "createReference.level must be 0..4 (Easy, Medium, Hard, VeryHard, None)"
            };
        }
        out = static_cast<std::uint32_t>(parsed);
        return std::nullopt;
    }
    return Error{
        "InvalidQuestAliasPayload",
        "createReference.level must be Easy, Medium, Hard, VeryHard, or None"
    };
}

std::optional<Error> requireLoadedRecord(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::string_view slot) {
    if (graph.winner(key) == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slot) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    return std::nullopt;
}

}

std::optional<Error> parseAliasFill(
    const nlohmann::json& element,
    fo4::forms::QuestAliasKind kind,
    const fo4::ConflictGraph& graph,
    fo4::forms::QuestAliasFill& fill,
    std::vector<fo4::FormKey>& fillKeys) {
    std::optional<fo4::FormKey> forced;
    std::optional<fo4::FormKey> actor;
    std::optional<fo4::FormKey> location;
    if (const auto error = parseFillFormKey(element, "forcedReference", forced)) return error;
    if (const auto error = parseFillFormKey(element, "uniqueActor", actor)) return error;
    if (const auto error = parseFillFormKey(element, "specificLocation", location)) {
        return error;
    }

    const nlohmann::json* createJson = nullptr;
    const nlohmann::json* locRefJson = nullptr;
    const nlohmann::json* externalJson = nullptr;
    const nlohmann::json* nearJson = nullptr;
    const nlohmann::json* eventJson = nullptr;
    const nlohmann::json* closestJson = nullptr;
    const nlohmann::json* refLocJson = nullptr;
    const nlohmann::json* extLocJson = nullptr;
    const nlohmann::json* matchLocJson = nullptr;
    if (const auto error = requireJsonObject(element, "createReference", createJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "locationAliasReference", locRefJson)) {
        return error;
    }
    if (const auto error = requireJsonObject(element, "externalAlias", externalJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "findMatchingNearAlias", nearJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "findMatchingFromEvent", eventJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "findMatchingClosest", closestJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "referenceAliasLocation", refLocJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "externalAliasLocation", extLocJson)) {
        return error;
    }
    if (const auto error =
            requireJsonObject(element, "findMatchingLocation", matchLocJson)) {
        return error;
    }

    const int fillCount =
        (forced.has_value() ? 1 : 0) + (actor.has_value() ? 1 : 0) +
        (location.has_value() ? 1 : 0) + (createJson != nullptr ? 1 : 0) +
        (locRefJson != nullptr ? 1 : 0) + (externalJson != nullptr ? 1 : 0) +
        (nearJson != nullptr ? 1 : 0) + (eventJson != nullptr ? 1 : 0) +
        (closestJson != nullptr ? 1 : 0) + (refLocJson != nullptr ? 1 : 0) +
        (extLocJson != nullptr ? 1 : 0) + (matchLocJson != nullptr ? 1 : 0);
    if (fillCount > 1) {
        return Error{
            "InvalidQuestAliasPayload",
            "an alias may set only one Fill Type"
        };
    }
    if (fillCount == 0) return std::nullopt;

    if (forced.has_value()) {
        if (const auto error =
                requireLoadedFill(graph, *forced, {}, true, "forcedReference")) {
            return error;
        }
        fill = fo4::forms::QuestSpecificReferenceFill{forced};
        fillKeys.push_back(*forced);
        return std::nullopt;
    }
    if (actor.has_value()) {
        if (const auto error =
                requireLoadedFill(graph, *actor, {"NPC_"}, false, "uniqueActor")) {
            return error;
        }
        fill = fo4::forms::QuestUniqueActorFill{*actor};
        fillKeys.push_back(*actor);
        return std::nullopt;
    }
    if (location.has_value()) {
        if (const auto error = requireLoadedFill(
                graph, *location, {"LCTN"}, false, "specificLocation")) {
            return error;
        }
        fill = fo4::forms::QuestSpecificLocationFill{*location};
        fillKeys.push_back(*location);
        return std::nullopt;
    }

    if (createJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "createReference is valid only on reference aliases"
            };
        }
        if (const auto error =
                rejectUnknownKeys(*createJson, knownCreateReferenceKey, "createReference",
                                  "InvalidQuestAliasPayload")) {
            return error;
        }
        std::optional<fo4::FormKey> object;
        if (const auto error = parseFillFormKey(*createJson, "object", object)) return error;
        if (object.has_value()) {
            if (const auto error = requireLoadedRecord(graph, *object, "createReference.object")) {
                return error;
            }
            fillKeys.push_back(*object);
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*createJson, "alias", aliasId)) return error;
        if (aliasId < std::numeric_limits<std::int16_t>::min() ||
            aliasId > std::numeric_limits<std::int16_t>::max()) {
            return Error{"InvalidQuestAliasPayload", "createReference.alias must fit int16"};
        }
        std::uint16_t createMode = 0;
        if (const auto error = parseCreateMode(*createJson, createMode)) return error;
        std::uint32_t level = 0;
        if (const auto error = parseCreateLevel(*createJson, level)) return error;
        fill = fo4::forms::QuestCreateReferenceFill{
            .object = object,
            .alias = static_cast<std::int16_t>(aliasId),
            .create_mode = createMode,
            .level = level,
        };
        return std::nullopt;
    }

    if (locRefJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "locationAliasReference is valid only on reference aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *locRefJson, knownLocationAliasReferenceKey, "locationAliasReference",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*locRefJson, "alias", aliasId)) return error;
        std::optional<fo4::FormKey> refType;
        if (const auto error = parseFillFormKey(*locRefJson, "referenceType", refType)) {
            return error;
        }
        if (!refType.has_value()) {
            return Error{
                "InvalidQuestAliasPayload",
                "locationAliasReference.referenceType must be a FormKey"
            };
        }
        if (const auto error = requireLoadedFill(
                graph, *refType, {"LCRT"}, false, "locationAliasReference.referenceType")) {
            return error;
        }
        fillKeys.push_back(*refType);
        fill = fo4::forms::QuestLocationAliasReferenceFill{
            .alias = aliasId,
            .reference_type = *refType,
        };
        return std::nullopt;
    }

    if (externalJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "externalAlias is valid only on reference aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *externalJson, knownExternalAliasKey, "externalAlias",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::optional<fo4::FormKey> quest;
        if (const auto error = parseFillFormKey(*externalJson, "quest", quest)) return error;
        if (quest.has_value()) {
            if (const auto error =
                    requireLoadedFill(graph, *quest, {"QUST"}, false, "externalAlias.quest")) {
                return error;
            }
            fillKeys.push_back(*quest);
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*externalJson, "alias", aliasId)) return error;
        fill = fo4::forms::QuestExternalAliasReferenceFill{
            .quest = quest,
            .alias = aliasId,
        };
        return std::nullopt;
    }

    if (nearJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "findMatchingNearAlias is valid only on reference aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *nearJson, knownNearAliasKey, "findMatchingNearAlias",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*nearJson, "alias", aliasId)) return error;
        std::uint32_t type = 0;
        if (nearJson->contains("type") && !(*nearJson)["type"].is_null()) {
            if (const auto error = requireU32Field(*nearJson, "type", type)) return error;
        }
        if (type > 1u) {
            return Error{
                "InvalidQuestAliasPayload",
                "findMatchingNearAlias.type must be 0 (LinkedFrom) or 1 (LinkedRef)"
            };
        }
        fill = fo4::forms::QuestFindMatchingReferenceFill{
            fo4::forms::QuestMatchingReferenceNearAlias{.alias = aliasId, .type = type}};
        return std::nullopt;
    }

    if (eventJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "findMatchingFromEvent is valid only on reference aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *eventJson, knownEventDataKey, "findMatchingFromEvent",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::uint32_t event = 0;
        std::uint32_t data = 0;
        if (const auto error = requireU32Field(*eventJson, "event", event)) return error;
        if (const auto error = requireU32Field(*eventJson, "data", data)) return error;
        fill = fo4::forms::QuestFindMatchingReferenceFill{
            fo4::forms::QuestMatchingReferenceEvent{.event = event, .data = data}};
        return std::nullopt;
    }

    if (closestJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::reference) {
            return Error{
                "InvalidQuestAliasPayload",
                "findMatchingClosest is valid only on reference aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *closestJson, knownClosestAliasKey, "findMatchingClosest",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*closestJson, "alias", aliasId)) return error;
        fill = fo4::forms::QuestFindMatchingReferenceFill{
            fo4::forms::QuestMatchingReferenceClosest{.alias = aliasId}};
        return std::nullopt;
    }

    if (refLocJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::location) {
            return Error{
                "InvalidQuestAliasPayload",
                "referenceAliasLocation is valid only on location aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *refLocJson, knownReferenceAliasLocationKey, "referenceAliasLocation",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::int32_t aliasId = 0;
        if (const auto error = requireInt32Field(*refLocJson, "alias", aliasId)) return error;
        std::optional<fo4::FormKey> keyword;
        if (const auto error = parseFillFormKey(*refLocJson, "keyword", keyword)) return error;
        if (keyword.has_value()) {
            if (const auto error = requireLoadedFill(
                    graph, *keyword, {"KYWD"}, false, "referenceAliasLocation.keyword")) {
                return error;
            }
            fillKeys.push_back(*keyword);
        }
        fill = fo4::forms::QuestReferenceAliasLocationFill{
            .alias = aliasId,
            .keyword = keyword,
        };
        return std::nullopt;
    }

    if (extLocJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::location) {
            return Error{
                "InvalidQuestAliasPayload",
                "externalAliasLocation is valid only on location aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *extLocJson, knownExternalAliasKey, "externalAliasLocation",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::optional<fo4::FormKey> quest;
        if (const auto error = parseFillFormKey(*extLocJson, "quest", quest)) return error;
        if (quest.has_value()) {
            if (const auto error = requireLoadedFill(
                    graph, *quest, {"QUST"}, false, "externalAliasLocation.quest")) {
                return error;
            }
            fillKeys.push_back(*quest);
        }
        std::optional<std::int32_t> aliasId;
        if (!questInt32(*extLocJson, "alias", aliasId)) {
            return Error{
                "InvalidQuestAliasPayload",
                "externalAliasLocation.alias must be an integer"
            };
        }
        if (!quest.has_value() && !aliasId.has_value()) {
            return Error{
                "InvalidQuestAliasPayload",
                "externalAliasLocation must contain quest or alias"
            };
        }
        fill = fo4::forms::QuestExternalAliasLocationFill{
            .quest = quest,
            .alias = aliasId,
        };
        return std::nullopt;
    }

    if (matchLocJson != nullptr) {
        if (kind != fo4::forms::QuestAliasKind::location) {
            return Error{
                "InvalidQuestAliasPayload",
                "findMatchingLocation is valid only on location aliases"
            };
        }
        if (const auto error = rejectUnknownKeys(
                *matchLocJson, knownEventDataKey, "findMatchingLocation",
                "InvalidQuestAliasPayload")) {
            return error;
        }
        std::uint32_t event = 0;
        std::uint32_t data = 0;
        if (const auto error = requireU32Field(*matchLocJson, "event", event)) return error;
        if (const auto error = requireU32Field(*matchLocJson, "data", data)) return error;
        fill = fo4::forms::QuestFindMatchingLocationFill{.event = event, .data = data};
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<Error> parseAliasFormKeyList(
    const nlohmann::json& object,
    const char* key,
    const fo4::ConflictGraph& graph,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::vector<fo4::FormKey>& out,
    std::vector<fo4::FormKey>& allKeys) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (!found->is_array()) {
        return Error{"InvalidQuestAliasPayload", std::string(key) + " must be an array"};
    }
    for (const auto& element : *found) {
        if (!element.is_string()) {
            return Error{
                "InvalidQuestAliasPayload",
                std::string(key) + " entries must be FormKey strings"
            };
        }
        fo4::FormKey parsed;
        try {
            parsed = fo4::FormKey::parse(element.get<std::string>());
        } catch (const std::exception& error) {
            return Error{"InvalidFormKey", std::string(key) + ": " + error.what()};
        }
        if (placedTargets) {
            if (const auto error = requireLoadedFill(graph, parsed, {}, true, key)) {
                return error;
            }
        } else if (accepted.size() == 0) {
            const auto* winner = graph.winner(parsed);
            if (winner == nullptr) {
                return Error{
                    "InvalidTypedFormKey",
                    std::string(key) + " must resolve to a loaded record: " + parsed.to_string()
                };
            }
            if (!fo4::schema::is_sig_base_object(winner->signature)) {
                return Error{
                    "InvalidTypedFormKey",
                    std::string(key) + " " + parsed.to_string() + " is a " +
                        winner->signature.to_string() + ", but this slot holds an item"
                };
            }
        } else if (const auto error =
                       requireLoadedFill(graph, parsed, accepted, false, key)) {
            return error;
        }
        out.push_back(parsed);
        allKeys.push_back(parsed);
    }
    return std::nullopt;
}

}
