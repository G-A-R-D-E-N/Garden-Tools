#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

namespace fo4ide::plugin {
namespace {

std::string questStringArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return {};
    if (args[index].is_string()) return args[index].get<std::string>();
    return {};
}

const nlohmann::json* questArrayArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return nullptr;
    if (args[index].is_array()) return &args[index];
    if (args[index].is_string()) {
        static thread_local nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(args[index].get<std::string>());
        } catch (const std::exception&) {
            return nullptr;
        }
        if (!parsed.is_array()) return nullptr;
        return &parsed;
    }
    return nullptr;
}

constexpr std::uint16_t kMutagenGetEventData = 576;
constexpr std::uint32_t kRunOnReference = 2;

std::string asciiLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool parseEntireInt32(std::string_view text, std::int32_t& value) {
    if (text.empty()) return false;
    if (text.front() == '+') return false;
    int base = 10;
    auto view = text;
    bool negative = false;
    if (view.front() == '-') {
        negative = true;
        view.remove_prefix(1);
        if (view.empty()) return false;
    }
    if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        base = 16;
        view.remove_prefix(2);
        if (view.empty()) return false;
    }
    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(
        view.data(), view.data() + view.size(), parsed, base);
    if (error != std::errc{} || ptr != view.data() + view.size()) return false;
    if (negative) {
        if (parsed > static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max()) + 1ull) {
            return false;
        }
        if (parsed == static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max()) + 1ull) {
            value = std::numeric_limits<std::int32_t>::min();
            return true;
        }
        value = -static_cast<std::int32_t>(parsed);
        return true;
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    value = static_cast<std::int32_t>(parsed);
    return true;
}

bool parseEntireUnsigned32(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    if (text.front() == '+' || text.front() == '-') return false;
    int base = 10;
    auto view = text;
    if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        base = 16;
        view.remove_prefix(2);
        if (view.empty()) return false;
    }
    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(
        view.data(), view.data() + view.size(), parsed, base);
    if (error != std::errc{} || ptr != view.data() + view.size()) return false;
    if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool questUnsigned(const nlohmann::json& object,
                   const char* key,
                   std::uint32_t defaultValue,
                   std::uint32_t& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = defaultValue;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 ||
            parsed > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_string()) {
        return parseEntireUnsigned32(found->get<std::string>(), value);
    }
    return false;
}

bool questInt32(const nlohmann::json& object, const char* key, std::optional<std::int32_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        value = static_cast<std::int32_t>(parsed);
        return true;
    }
    return false;
}

bool questFloat(const nlohmann::json& object,
                const char* key,
                float defaultValue,
                float& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = defaultValue;
        return true;
    }
    if (!found->is_number()) return false;
    const auto parsed = found->get<double>();
    if (!std::isfinite(parsed)) return false;
    const auto converted = static_cast<float>(parsed);
    if (!std::isfinite(converted)) return false;
    value = converted;
    return true;
}

bool questU8(const nlohmann::json& object, const char* key, std::optional<std::uint8_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 || parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    return false;
}

bool questOptionalString(const nlohmann::json& object,
                         const char* key,
                         std::optional<std::string>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (!found->is_string()) return false;
    value = found->get<std::string>();
    return true;
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

bool knownStageKey(std::string_view key) {
    return key == "index" || key == "flags" || key == "logEntry" ||
           key == "logEntries" || key == "complete" || key == "fail";
}

bool knownLogEntryKey(std::string_view key) {
    return key == "flags" || key == "note" || key == "logText" || key == "conditions";
}

bool knownConditionKey(std::string_view key) {
    return key == "function" || key == "operator" || key == "param1" ||
           key == "param2" || key == "value" || key == "compareGlobal" ||
           key == "runOn" || key == "reference" || key == "flags" ||
           key == "cis1" || key == "cis2";
}

bool isPlacedSignature(fo4::Signature signature) {
    const auto text = signature.to_string();
    return text == "REFR" || text == "ACHR" || text == "PGRE" || text == "PHZD" ||
           text == "PMIS" || text == "PARW" || text == "PBAR" || text == "PBEA" ||
           text == "PCON" || text == "PFLA";
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

std::optional<Error> requireLoadedFill(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::string_view slotName) {
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slotName) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    const auto signature = winner->signature.to_string();
    if (placedTargets && isPlacedSignature(winner->signature)) return std::nullopt;
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    std::string expected;
    if (placedTargets) expected = "placed reference";
    for (const auto name : accepted) {
        if (!expected.empty()) expected += " or ";
        expected += std::string(name);
    }
    return Error{
        "InvalidTypedFormKey",
        std::string(slotName) + " " + key.to_string() + " is a " + signature +
            ", but this slot holds a " + expected
    };
}

bool editorTokenMatches(fo4::Signature signature, std::string_view token) {
    const auto name = signature.to_string();
    const auto wanted = asciiLower(token);
    if (wanted == "placedobject") return isPlacedSignature(signature);
    if (wanted == "placednpc") return name == "ACHR";
    if (wanted == "npc") return name == "NPC_";
    if (wanted == "actorvalueinformation") return name == "AVIF";
    if (wanted == "magiceffect") return name == "MGEF";
    if (wanted == "ingestible") return name == "ALCH";
    if (wanted == "objecteffect") return name == "ENCH";
    if (wanted == "ingredient") return name == "INGR";
    if (wanted == "spell") return name == "SPEL";
    if (wanted == "associationtype") return name == "ASTP";
    if (wanted == "damagetype") return name == "DMGT";
    if (wanted == "encounterzone") return name == "ECZN";
    if (wanted == "equiptype") return name == "EQUP";
    if (wanted == "faction") return name == "FACT";
    if (wanted == "formlist") return name == "FLST";
    if (wanted == "furniture") return name == "FURN";
    if (wanted == "global") return name == "GLOB";
    if (wanted == "idleanimation") return name == "IDLE";
    if (wanted == "keyword") return name == "KYWD";
    if (wanted == "location") return name == "LCTN";
    if (wanted == "locationreferencetype") return name == "LCRT";
    if (wanted == "package") return name == "PACK";
    if (wanted == "perk") return name == "PERK";
    if (wanted == "quest") return name == "QUST";
    if (wanted == "race") return name == "RACE";
    if (wanted == "region") return name == "REGN";
    if (wanted == "scene") return name == "SCEN";
    if (wanted == "voicetype") return name == "VTYP";
    if (wanted == "weather") return name == "WTHR";
    if (wanted == "worldspace") return name == "WRLD";
    if (wanted == "cell") return name == "CELL";
    if (wanted == "class") return name == "CLAS";
    return false;
}

std::optional<Error> requireConditionTarget(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::string_view paramType,
    std::string_view slotName) {
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slotName) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    const auto [kind, types] = ConditionFunctions::editorFor(paramType);
    if (kind != "record") return std::nullopt;
    if (types.empty()) return std::nullopt;
    std::string token;
    std::istringstream stream{std::string(types)};
    while (std::getline(stream, token, ',')) {
        if (editorTokenMatches(winner->signature, token)) return std::nullopt;
    }
    return Error{
        "InvalidTypedFormKey",
        std::string(slotName) + " " + key.to_string() + " is a " +
            winner->signature.to_string() + ", but this slot holds a " + std::string(paramType)
    };
}

bool conditionSlotIsNumeric(std::string_view paramType, std::uint8_t flags) {
    const auto [kind, types] = ConditionFunctions::editorFor(paramType);
    if (kind == "number") return true;
    if (kind != "record") return false;
    return (flags & fo4::k_ctda_flag_aliases) != 0 ||
           (flags & fo4::k_ctda_flag_pack_data) != 0;
}

bool conditionSlotIsText(std::string_view paramType) {
    return ConditionFunctions::editorFor(paramType).first == "text";
}

struct PendingQuestCondition {
    fo4::CtdaCondition condition;
    std::optional<fo4::FormKey> param1;
    std::optional<fo4::FormKey> param2;
    std::optional<fo4::FormKey> reference;
    std::optional<fo4::FormKey> compareGlobal;
};

std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label,
                                       std::string_view errorType = "InvalidQuestStagePayload") {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known(it.key())) {
            return Error{
                std::string(errorType),
                "Unsupported " + std::string(label) + " field '" + it.key() + "'"
            };
        }
    }
    return std::nullopt;
}

std::optional<Error> parseFillFormKey(const nlohmann::json& object,
                                      const char* key,
                                      std::optional<fo4::FormKey>& out) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        out = std::nullopt;
        return std::nullopt;
    }
    if (!found->is_string()) {
        return Error{"InvalidQuestAliasPayload", std::string(key) + " must be a FormKey string"};
    }
    const auto text = found->get<std::string>();
    if (text.empty()) {
        out = std::nullopt;
        return std::nullopt;
    }
    try {
        out = fo4::FormKey::parse(text);
        return std::nullopt;
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", std::string(key) + ": " + error.what()};
    }
}

std::optional<Error> parseOptionalTypedFormKey(
    const nlohmann::json& object,
    const char* key,
    const fo4::ConflictGraph& graph,
    std::initializer_list<std::string_view> accepted,
    std::optional<fo4::FormKey>& out,
    std::vector<fo4::FormKey>& fillKeys) {
    if (const auto error = parseFillFormKey(object, key, out)) return error;
    if (!out.has_value()) return std::nullopt;
    if (const auto error = requireLoadedFill(graph, *out, accepted, false, key)) {
        return error;
    }
    fillKeys.push_back(*out);
    return std::nullopt;
}

bool jsonObjectPresent(const nlohmann::json& object, const char* key) {
    return object.contains(key) && !object[key].is_null();
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
        if (text == "Easy" || text == "0") {
            out = 0;
            return std::nullopt;
        }
        if (text == "Medium" || text == "1") {
            out = 1;
            return std::nullopt;
        }
        if (text == "Hard" || text == "2") {
            out = 2;
            return std::nullopt;
        }
        if (text == "VeryHard" || text == "3") {
            out = 3;
            return std::nullopt;
        }
        return Error{
            "InvalidQuestAliasPayload",
            "createReference.level must be Easy, Medium, Hard, or VeryHard"
        };
    }
    if (found->is_number_unsigned() || found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 || parsed > 3) {
            return Error{
                "InvalidQuestAliasPayload",
                "createReference.level must be 0..3 (Easy, Medium, Hard, VeryHard)"
            };
        }
        out = static_cast<std::uint32_t>(parsed);
        return std::nullopt;
    }
    return Error{
        "InvalidQuestAliasPayload",
        "createReference.level must be Easy, Medium, Hard, or VeryHard"
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

std::optional<Error> parseQuestConditionParam(
    const nlohmann::json& value,
    const char* slot,
    std::int32_t& integer,
    std::optional<fo4::FormKey>& form,
    std::string_view errorType) {
    form = std::nullopt;
    integer = 0;
    if (value.is_number_integer()) {
        const auto parsed = value.get<std::int64_t>();
        if (parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            return Error{std::string(errorType), std::string(slot) + " is out of range"};
        }
        integer = static_cast<std::int32_t>(parsed);
        return std::nullopt;
    }
    if (value.is_number_unsigned()) {
        const auto parsed = value.get<std::uint64_t>();
        if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return Error{std::string(errorType), std::string(slot) + " is out of range"};
        }
        integer = static_cast<std::int32_t>(parsed);
        return std::nullopt;
    }
    if (!value.is_string()) {
        return Error{
            std::string(errorType),
            std::string(slot) + " must be an integer or FormKey string"
        };
    }
    const auto text = value.get<std::string>();
    if (text.empty()) return std::nullopt;
    try {
        form = fo4::FormKey::parse(text);
        return std::nullopt;
    } catch (const std::exception&) {
    }
    if (!parseEntireInt32(text, integer)) {
        return Error{
            std::string(errorType),
            std::string(slot) + " must be an integer or FormKey string"
        };
    }
    return std::nullopt;
}

std::optional<Error> parseQuestCondition(
    const nlohmann::json& element,
    const fo4::ConflictGraph& graph,
    PendingQuestCondition& out,
    std::vector<fo4::FormKey>& conditionKeys,
    std::string_view errorType = "InvalidQuestStagePayload",
    std::string_view surface = "quest log entries") {
    const auto fail = [&](std::string message) {
        return Error{std::string(errorType), std::move(message)};
    };
    if (!element.is_object()) {
        return fail("every condition must be an object");
    }
    if (const auto error =
            rejectUnknownKeys(element, knownConditionKey, "condition", errorType)) {
        return error;
    }
    const auto functionField = element.find("function");
    if (functionField == element.end() || !functionField->is_string()) {
        return fail("condition function must be a string");
    }
    const auto functionText = functionField->get<std::string>();
    const auto function = functionIndex(functionText);
    if (!function.has_value()) {
        return fail("unknown condition function '" + functionText + "'");
    }
    if (asciiLower(functionText) == "geteventdata" ||
        *function == kMutagenGetEventData ||
        *function == fo4::k_ctda_event_function_index) {
        return Error{
            std::string(errorType),
            "GetEventData is not supported on " + std::string(surface)
        };
    }
    out = {};
    out.condition.function = *function;
    out.condition.unknown3 = -1;

    if (element.contains("runOn") && !element["runOn"].is_null()) {
        if (!element["runOn"].is_string()) {
            return fail("runOn must be a string");
        }
        const auto runOn = runOnValue(element["runOn"].get<std::string>());
        if (!runOn.has_value()) {
            return fail("unknown runOn value");
        }
        out.condition.run_on = *runOn;
    }

    if (element.contains("operator") && !element["operator"].is_null()) {
        if (!element["operator"].is_string()) {
            return fail("operator must be a string");
        }
        const auto opText = element["operator"].get<std::string>();
        std::uint8_t compareOperator = 0;
        if (opText == "==" || opText == "EqualTo") compareOperator = 0;
        else if (opText == "!=" || opText == "<>" || opText == "NotEqualTo") compareOperator = 1;
        else if (opText == ">" || opText == "GreaterThan") compareOperator = 2;
        else if (opText == ">=" || opText == "GreaterThanOrEqualTo") compareOperator = 3;
        else if (opText == "<" || opText == "LessThan") compareOperator = 4;
        else if (opText == "<=" || opText == "LessThanOrEqualTo") compareOperator = 5;
        else {
            return fail("unknown condition operator");
        }
        out.condition.flags_byte = static_cast<std::uint8_t>(compareOperator << 5);
    }

    if (element.contains("flags") && !element["flags"].is_null()) {
        if (!element["flags"].is_string()) {
            return fail("condition flags must be a string");
        }
        std::string flagsText = element["flags"].get<std::string>();
        std::string part;
        std::istringstream stream(std::move(flagsText));
        while (std::getline(stream, part, ',')) {
            part.erase(part.begin(),
                       std::find_if(part.begin(), part.end(), [](unsigned char ch) {
                           return !std::isspace(ch);
                       }));
            part.erase(std::find_if(part.rbegin(), part.rend(), [](unsigned char ch) {
                           return !std::isspace(ch);
                       }).base(),
                       part.end());
            if (part.empty()) continue;
            const auto needle = part;
            std::string lower;
            lower.reserve(needle.size());
            for (const auto ch : needle) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            std::optional<std::uint8_t> flag;
            if (lower == "or") flag = fo4::k_ctda_flag_or;
            else if (lower == "parametersusealiases") flag = fo4::k_ctda_flag_aliases;
            else if (lower == "usepackdata") flag = fo4::k_ctda_flag_pack_data;
            else if (lower == "swapsubjectandtarget") flag = fo4::k_ctda_flag_swap;
            if (!flag.has_value()) {
                return fail("unknown condition flag '" + part + "'");
            }
            out.condition.flags_byte =
                static_cast<std::uint8_t>(out.condition.flags_byte | *flag);
        }
    }

    const bool hasGlobal =
        element.contains("compareGlobal") && !element["compareGlobal"].is_null();
    const bool hasValue = element.contains("value") && !element["value"].is_null();
    if (hasGlobal && hasValue) {
        return fail("a condition may set value or compareGlobal, not both");
    }
    if (hasGlobal) {
        if (!element["compareGlobal"].is_string()) {
            return fail("compareGlobal must be a FormKey string");
        }
        try {
            out.compareGlobal = fo4::FormKey::parse(element["compareGlobal"].get<std::string>());
        } catch (const std::exception& error) {
            return Error{"InvalidFormKey", std::string("compareGlobal: ") + error.what()};
        }
        if (const auto error = requireLoadedFill(
                graph, *out.compareGlobal, {"GLOB"}, false, "compareGlobal")) {
            return error;
        }
        conditionKeys.push_back(*out.compareGlobal);
        out.condition.flags_byte =
            static_cast<std::uint8_t>(out.condition.flags_byte | fo4::k_ctda_flag_use_global);
    } else {
        float value = 1.0f;
        if (hasValue) {
            if (!element["value"].is_number()) {
                return fail("condition value must be a number");
            }
            value = element["value"].get<float>();
        }
        static_assert(sizeof(value) == sizeof(out.condition.comparison_raw));
        std::memcpy(&out.condition.comparison_raw, &value, sizeof(value));
    }

    const auto declared = ConditionFunctions::paramsOf(functionText);
    const auto parseSlot = [&](std::size_t index,
                               const char* jsonKey,
                               std::int32_t& integer,
                               std::optional<fo4::FormKey>& form) -> std::optional<Error> {
        const bool present = element.contains(jsonKey) && !element[jsonKey].is_null();
        if (index >= declared.size()) {
            if (present) {
                return fail(std::string(jsonKey) + " is not used by " + functionText);
            }
            return std::nullopt;
        }
        const auto& paramType = declared[index];
        if (conditionSlotIsText(paramType)) {
            if (present) {
                return fail(std::string(jsonKey) + " for " + functionText + " must use cis1/cis2");
            }
            return std::nullopt;
        }
        if (conditionSlotIsNumeric(paramType, out.condition.flags())) {
            if (!present) return std::nullopt;
            std::optional<fo4::FormKey> ignored;
            if (const auto error = parseQuestConditionParam(
                    element[jsonKey], jsonKey, integer, ignored, errorType)) {
                return error;
            }
            if (ignored.has_value()) {
                return fail(std::string(jsonKey) + " for " + functionText + " must be an integer");
            }
            return std::nullopt;
        }
        if (!present) {
            if (asciiLower(paramType) == "factionnull") return std::nullopt;
            return fail(std::string(jsonKey) + " for " + functionText + " must be a FormKey");
        }
        std::int32_t ignored = 0;
        if (const auto error = parseQuestConditionParam(
                element[jsonKey], jsonKey, ignored, form, errorType)) {
            return error;
        }
        if (!form.has_value()) {
            return fail(std::string(jsonKey) + " for " + functionText + " must be a FormKey");
        }
        if (const auto error = requireConditionTarget(graph, *form, paramType, jsonKey)) {
            return error;
        }
        conditionKeys.push_back(*form);
        return std::nullopt;
    };
    if (const auto error = parseSlot(0, "param1", out.condition.param1, out.param1)) {
        return error;
    }
    if (const auto error = parseSlot(1, "param2", out.condition.param2, out.param2)) {
        return error;
    }

    const bool hasReference =
        element.contains("reference") && !element["reference"].is_null();
    if (out.condition.run_on == kRunOnReference) {
        if (!hasReference) {
            return fail("runOn Reference requires a reference FormKey");
        }
        if (!element["reference"].is_string()) {
            return fail("reference must be a FormKey string");
        }
        try {
            out.reference = fo4::FormKey::parse(element["reference"].get<std::string>());
        } catch (const std::exception& error) {
            return Error{"InvalidFormKey", std::string("reference: ") + error.what()};
        }
        if (const auto error =
                requireConditionTarget(graph, *out.reference, "Reference", "reference")) {
            return error;
        }
        conditionKeys.push_back(*out.reference);
    } else if (hasReference) {
        return fail("reference is only valid when runOn is Reference");
    }

    const auto parseCis = [&](const char* key, std::optional<std::string>& target)
        -> std::optional<Error> {
        if (!element.contains(key) || element[key].is_null()) return std::nullopt;
        if (!element[key].is_string()) {
            return fail(std::string(key) + " must be a string");
        }
        auto text = element[key].get<std::string>();
        if (text.find('\0') != std::string::npos) {
            return fail(std::string(key) + " cannot contain an embedded NUL");
        }
        target = std::move(text);
        return std::nullopt;
    };
    const auto cisAllowed = [&](std::size_t index) {
        return index < declared.size() && conditionSlotIsText(declared[index]);
    };
    if (const auto error = parseCis("cis1", out.condition.parameter1_string)) return error;
    if (out.condition.parameter1_string.has_value() && !cisAllowed(0)) {
        return fail("cis1 is only valid when parameter 1 is String or Event");
    }
    if (const auto error = parseCis("cis2", out.condition.parameter2_string)) return error;
    if (out.condition.parameter2_string.has_value() && !cisAllowed(1)) {
        return fail("cis2 is only valid when parameter 2 is String or Event");
    }
    return std::nullopt;
}

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

std::optional<Error> adapterConditionUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph,
    const fo4::CtdaCondition& condition,
    std::string_view surface);

bool fourByteMember(const fo4::Subrecord& member) {
    return member.data.size() == 4;
}

std::optional<Error> existingTypedFormIdUnrepresentable(
    const fo4::PluginFormIdContext& context,
    const fo4::ConflictGraph& graph,
    std::span<const std::byte> data,
    std::size_t offset,
    std::initializer_list<std::string_view> accepted,
    bool baseObjects,
    std::string_view slot,
    bool allowNull = false) {
    if (offset + 4 > data.size()) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " cannot be reconstructed"
        };
    }
    std::uint32_t raw = 0;
    std::memcpy(&raw, data.data() + offset, 4);
    if (raw == 0) {
        if (allowNull) return std::nullopt;
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " is null"
        };
    }
    fo4::FormKey key;
    try {
        key = fo4::resolve_plugin_form_id(fo4::RawFormId{raw}, context);
    } catch (const fo4::FormIdError&) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " cannot be reconstructed"
        };
    }
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST alias " + std::string(slot) + " does not resolve to a loaded record"
        };
    }
    if (baseObjects) {
        if (!fo4::schema::is_sig_base_object(winner->signature)) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias " + std::string(slot) + " is a " +
                    winner->signature.to_string() + ", but this slot holds an item"
            };
        }
        return std::nullopt;
    }
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

std::optional<Error> extraAliasMembersUnrepresentable(
    const std::vector<fo4::Subrecord>& members,
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph) {
    enum class Phase {
        keywords,
        items,
        spor,
        ocor,
        gwor,
        ecor,
        linked,
        display,
        forcedVoice,
        deathItem,
        spells,
        factions,
        packages,
        voiceTypes,
        done
    };
    auto phase = Phase::keywords;
    std::size_t i = 0;
    const auto context = fo4::plugin_form_id_context(plugin);
    const auto fail = [](std::string_view message) {
        return Error{"QuestAuthoringCoverageIncomplete", std::string(message)};
    };
    const auto sig = [&](std::size_t index) {
        return members[index].signature.to_string();
    };
    const auto isExtraStart = [&](std::string_view name) {
        return name == "KSIZ" || name == "KWDA" || name == "COCT" || name == "CNTO" ||
               name == "COED" ||
               name == "SPOR" || name == "OCOR" || name == "GWOR" || name == "ECOR" ||
               name == "ALLA" || name == "ALDN" || name == "ALFV" || name == "ALDI" ||
               name == "ALSP" || name == "ALFC" || name == "ALPC" || name == "VTCK";
    };
    while (i < members.size() && sig(i) != "ALED" && !isExtraStart(sig(i))) {
        ++i;
    }
    while (i < members.size() && sig(i) != "ALED") {
        const auto name = sig(i);
        if (name == "KSIZ") {
            if (phase > Phase::keywords) return fail("existing QUST alias extra members are out of order");
            if (i + 1 >= members.size() || sig(i + 1) != "KWDA") {
                return fail("existing QUST alias KSIZ is missing KWDA");
            }
            if (!fourByteMember(members[i]) || members[i + 1].data.size() % 4 != 0) {
                return fail("existing QUST alias keywords cannot be reconstructed");
            }
            std::uint32_t count = 0;
            std::memcpy(&count, members[i].data.data(), 4);
            if (count != members[i + 1].data.size() / 4) {
                return fail("existing QUST alias KSIZ does not match KWDA");
            }
            for (std::uint32_t n = 0; n < count; ++n) {
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i + 1].data, n * 4u, {"KYWD"}, false,
                        "keywords")) {
                    return error;
                }
            }
            i += 2;
            phase = Phase::items;
            continue;
        }
        if (name == "KWDA") return fail("existing QUST alias KWDA is missing KSIZ");
        if (name == "COCT") {
            if (phase > Phase::items) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias items cannot be reconstructed");
            }
            std::uint32_t count = 0;
            std::memcpy(&count, members[i].data.data(), 4);
            ++i;
            for (std::uint32_t n = 0; n < count; ++n) {
                if (i >= members.size() || sig(i) != "CNTO" || members[i].data.size() != 8) {
                    return fail("existing QUST alias CNTO entries cannot be reconstructed");
                }
                std::int32_t itemCount = 0;
                std::memcpy(&itemCount, members[i].data.data() + 4, 4);
                if (itemCount < 1) {
                    return fail("existing QUST alias item count must be at least 1");
                }
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i].data, 0, {}, true, "items")) {
                    return error;
                }
                ++i;
                if (i < members.size() && sig(i) == "COED") {
                    if (members[i].data.size() != 12) {
                        return fail("existing QUST alias item extra cannot be reconstructed");
                    }
                    float itemCondition = 0.0f;
                    std::memcpy(&itemCondition, members[i].data.data() + 8, 4);
                    if (!std::isfinite(itemCondition)) {
                        return fail(
                            "existing QUST alias item extra itemCondition cannot be reconstructed");
                    }
                    std::uint32_t ownerRaw = 0;
                    std::memcpy(&ownerRaw, members[i].data.data(), 4);
                    if (ownerRaw == 0) {
                        std::uint32_t unused = 0;
                        std::memcpy(&unused, members[i].data.data() + 4, 4);
                        if (unused != 0) {
                            return fail(
                                "existing QUST alias item extra unused owner data cannot be reconstructed");
                        }
                    } else {
                        if (const auto error = existingTypedFormIdUnrepresentable(
                                context, graph, members[i].data, 0, {"NPC_", "FACT"}, false,
                                "item extra owner")) {
                            return error;
                        }
                        fo4::FormKey ownerKey;
                        try {
                            ownerKey = fo4::resolve_plugin_form_id(
                                fo4::RawFormId{ownerRaw}, context);
                        } catch (const fo4::FormIdError&) {
                            return fail("existing QUST alias item extra cannot be reconstructed");
                        }
                        const auto* ownerWinner = graph.winner(ownerKey);
                        if (ownerWinner == nullptr) {
                            return fail(
                                "existing QUST alias item extra owner does not resolve to a loaded record");
                        }
                        const auto ownerSig = ownerWinner->signature.to_string();
                        if (ownerSig == "NPC_") {
                            if (const auto error = existingTypedFormIdUnrepresentable(
                                    context, graph, members[i].data, 4, {"GLOB"}, false,
                                    "item extra global", true)) {
                                return error;
                            }
                        }
                    }
                    ++i;
                }
            }
            phase = Phase::spor;
            continue;
        }
        if (name == "CNTO") return fail("existing QUST alias CNTO is missing COCT");
        if (name == "COED") return fail("existing QUST alias COED is missing preceding CNTO");
        const auto uniqueLink = [&](Phase atMost,
                                    Phase next,
                                    std::initializer_list<std::string_view> accepted,
                                    std::string_view slot) -> std::optional<Error> {
            if (phase > atMost) {
                return fail("existing QUST alias extra members are out of order");
            }
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias " + std::string(slot) +
                            " cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, accepted, false, slot)) {
                return error;
            }
            ++i;
            phase = next;
            return std::nullopt;
        };
        if (name == "SPOR") {
            if (const auto error = uniqueLink(
                    Phase::spor, Phase::ocor, {"FLST"}, "spectatorOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "OCOR") {
            if (const auto error = uniqueLink(
                    Phase::ocor, Phase::gwor, {"FLST"},
                    "observeDeadBodyOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "GWOR") {
            if (const auto error = uniqueLink(
                    Phase::gwor, Phase::ecor, {"FLST"}, "guardWarnOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "ECOR") {
            if (const auto error = uniqueLink(
                    Phase::ecor, Phase::linked, {"FLST"}, "combatOverridePackageList")) {
                return error;
            }
            continue;
        }
        if (name == "ALDN") {
            if (const auto error =
                    uniqueLink(Phase::display, Phase::forcedVoice, {"MESG"}, "displayName")) {
                return error;
            }
            continue;
        }
        if (name == "ALFV") {
            if (const auto error =
                    uniqueLink(Phase::forcedVoice, Phase::deathItem, {"VTYP"}, "forcedVoice")) {
                return error;
            }
            continue;
        }
        if (name == "ALDI") {
            if (const auto error =
                    uniqueLink(Phase::deathItem, Phase::spells, {"LVLI"}, "deathItem")) {
                return error;
            }
            continue;
        }
        if (name == "VTCK") {
            if (const auto error = uniqueLink(
                    Phase::voiceTypes, Phase::done, {"VTYP", "FLST", "NPC_", "FACT"},
                    "voiceTypes")) {
                return error;
            }
            continue;
        }
        if (name == "ALLA") {
            if (phase > Phase::linked) return fail("existing QUST alias extra members are out of order");
            if (members[i].data.empty() || members[i].data.size() % 8 != 0) {
                return fail("existing QUST alias linked aliases cannot be reconstructed");
            }
            for (std::size_t offset = 0; offset < members[i].data.size(); offset += 8) {
                if (const auto error = existingTypedFormIdUnrepresentable(
                        context, graph, members[i].data, offset, {"KYWD"}, false,
                        "linkedAliases")) {
                    return error;
                }
            }
            ++i;
            phase = Phase::display;
            continue;
        }
        if (name == "ALSP") {
            if (phase > Phase::spells) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias spells cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"SPEL"}, false, "spells")) {
                return error;
            }
            ++i;
            phase = Phase::spells;
            continue;
        }
        if (name == "ALFC") {
            if (phase > Phase::factions) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias factions cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"FACT"}, false, "factions")) {
                return error;
            }
            ++i;
            phase = Phase::factions;
            continue;
        }
        if (name == "ALPC") {
            if (phase > Phase::packages) return fail("existing QUST alias extra members are out of order");
            if (!fourByteMember(members[i])) {
                return fail("existing QUST alias packages cannot be reconstructed");
            }
            if (const auto error = existingTypedFormIdUnrepresentable(
                    context, graph, members[i].data, 0, {"PACK"}, false, "packages")) {
                return error;
            }
            ++i;
            phase = Phase::voiceTypes;
            continue;
        }
        return fail("existing QUST alias has unsupported member " + name);
    }
    return std::nullopt;
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
                if (value.level > 3u) {
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

std::optional<Error> adapterConditionUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph,
    const fo4::CtdaCondition& condition,
    std::string_view surface = "log entry") {
    const auto existingFail = [&](std::string_view rest) {
        return Error{
            "QuestAuthoringCoverageIncomplete",
            "existing QUST " + std::string(surface) + " " + std::string(rest)
        };
    };
    if (condition.is_event_data() || condition.function == kMutagenGetEventData) {
        return existingFail("uses GetEventData");
    }
    const auto name = functionName(condition.function);
    if (name.empty() || asciiLower(name) == "geteventdata") {
        return existingFail("has an unsupported condition function");
    }
    if (condition.unknown2 != 0 || condition.unknown3 != -1) {
        return existingFail("CTDA has unknown fields");
    }
    if (condition.compare_operator() > 5) {
        return existingFail("has an unsupported condition operator");
    }
    if (runOnName(condition.run_on).empty()) {
        return existingFail("has an unsupported runOn");
    }
    const auto resolveRaw = [&](std::uint32_t raw,
                                std::string_view paramType,
                                std::string_view slot) -> std::optional<Error> {
        if (raw == 0) {
            if (asciiLower(paramType) == "factionnull") return std::nullopt;
            return existingFail("has an unresolved condition FormLink");
        }
        try {
            const auto key = fo4::resolve_plugin_form_id(
                fo4::RawFormId{raw}, fo4::plugin_form_id_context(plugin));
            if (const auto error = requireConditionTarget(graph, key, paramType, slot)) {
                return Error{"QuestAuthoringCoverageIncomplete", error->message};
            }
            return std::nullopt;
        } catch (const std::exception&) {
            return existingFail("has an unresolved condition FormLink");
        }
    };
    if (condition.use_global()) {
        if (const auto error = resolveRaw(condition.comparison_raw, "Global", "compareGlobal")) {
            return error;
        }
    }
    if (condition.run_on == kRunOnReference) {
        if (const auto error = resolveRaw(condition.reference, "Reference", "reference")) {
            return error;
        }
    } else if (condition.reference != 0) {
        return existingFail("stores a CTDA reference without runOn Reference");
    }
    const auto declared = ConditionFunctions::paramsOf(name);
    const auto checkSlot = [&](std::size_t index,
                               std::int32_t value,
                               std::string_view slot) -> std::optional<Error> {
        if (index >= declared.size()) {
            if (value != 0) {
                return existingFail("has unused condition parameter data");
            }
            return std::nullopt;
        }
        const auto& paramType = declared[index];
        if (conditionSlotIsText(paramType)) {
            if (value != 0) {
                return existingFail("has nonzero raw data on a text condition parameter");
            }
            return std::nullopt;
        }
        if (conditionSlotIsNumeric(paramType, condition.flags())) {
            return std::nullopt;
        }
        return resolveRaw(static_cast<std::uint32_t>(value), paramType, slot);
    };
    if (const auto error = checkSlot(0, condition.param1, "param1")) return error;
    if (const auto error = checkSlot(1, condition.param2, "param2")) return error;
    const auto cisAllowed = [&](std::size_t index) {
        return index < declared.size() && conditionSlotIsText(declared[index]);
    };
    if (condition.parameter1_string.has_value() && !cisAllowed(0)) {
        return existingFail("has CIS1 on a non-text condition parameter");
    }
    if (condition.parameter2_string.has_value() && !cisAllowed(1)) {
        return existingFail("has CIS2 on a non-text condition parameter");
    }
    return std::nullopt;
}

std::optional<Error> existingStagesUnrepresentable(const fo4::Plugin& plugin,
                                                   const fo4::FormKey& quest,
                                                   const fo4::ConflictGraph& graph) {
    const auto decoded = fo4::forms::decode_quest_stages(plugin, quest);
    for (const auto& stage : decoded.stages) {
        if (stage.unknown != 0) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST stage has a non-zero INDX unknown byte"
            };
        }
        if (!stage.leading_members.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST stage has members before the first QSDT"
            };
        }
        for (const auto& entry : stage.log_entries) {
            if (entry.log_text.has_value() && entry.log_text->localized()) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has a localized CNAM that this adapter cannot reconstruct"
                };
            }
            std::vector<fo4::CtdaCondition> conditions;
            std::size_t consumed = 0;
            if (!fo4::forms::lossless_quest_log_entry_conditions(
                    entry.members, conditions, consumed)) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has a CTDA/CIS run this adapter cannot reconstruct"
                };
            }
            for (const auto& condition : conditions) {
                if (const auto error =
                        adapterConditionUnrepresentable(plugin, graph, condition)) {
                    return error;
                }
            }
            bool sawNam2 = false;
            bool sawCnam = false;
            for (std::size_t i = consumed; i < entry.members.size(); ++i) {
                const auto signature = entry.members[i].signature.to_string();
                if (signature == "NAM2") {
                    if (sawNam2 || sawCnam) {
                        return Error{
                            "QuestAuthoringCoverageIncomplete",
                            "existing QUST log entry NAM2/CNAM order cannot be reconstructed"
                        };
                    }
                    sawNam2 = true;
                    continue;
                }
                if (signature == "CNAM") {
                    if (sawCnam) {
                        return Error{
                            "QuestAuthoringCoverageIncomplete",
                            "existing QUST log entry has duplicate CNAM"
                        };
                    }
                    sawCnam = true;
                    continue;
                }
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has unsupported member " + signature
                };
            }
        }
    }
    return std::nullopt;
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

std::optional<Error> existingAliasesUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::FormKey& quest,
    const fo4::ConflictGraph& graph) {
    const auto decoded = fo4::forms::decode_quest_aliases(plugin, quest);
    for (const auto& alias : decoded.aliases) {
        if (!alias.unresolved_links.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias has an unresolved FormLink"
            };
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

std::vector<std::byte> zstringBytes(std::string_view value) {
    std::vector<std::byte> data;
    data.reserve(value.size() + 1);
    for (const char ch : value) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    return data;
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

    std::scoped_lock lock(mutex_);
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
    try {
        if (const auto error =
                existingAliasesUnrepresentable(loaded->plugin, questKey, graph_)) {
            return std::unexpected(*error);
        }
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
                const auto* winner = graph_.winner(item.item);
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
                        const auto* ownerWinner = graph_.winner(*extra.owner);
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
                        const auto* ownerWinner = graph_.winner(*extra.owner);
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
        }
        fo4::forms::write_quest_aliases(mutated, questKey, creations);
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(creations.size()) + " quest alias(es) on " +
            recordText + " in " + pluginName + ". Written to disk.");
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

Result<nlohmann::json> RadiclePluginBackend::setQuestStages(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    const auto* stagesJson = questArrayArg(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (stagesJson == nullptr) {
        return std::unexpected(Error{
            "InvalidQuestStagePayload",
            "SetQuestStages requires a JSON array of stage objects"
        });
    }

    std::scoped_lock lock(mutex_);
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
            "SetQuestStages requires a QUST record"
        });
    }
    if (const auto error = existingQuestVmadUnrepresentable(*record, false, true)) {
        return std::unexpected(*error);
    }
    try {
        if (const auto error = existingStagesUnrepresentable(loaded->plugin, questKey, graph_)) {
            return std::unexpected(*error);
        }
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }

    const bool localized = loaded->plugin.header.tes4_flags.is_localized();
    fo4::forms::QuestStages stages;
    std::vector<std::vector<std::vector<PendingQuestCondition>>> pendingConditions;
    std::vector<fo4::FormKey> conditionKeys;
    std::set<std::uint16_t> indexes;

    for (const auto& element : *stagesJson) {
        if (!element.is_object()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "Every quest stage must be an object"
            });
        }
        if (const auto error = rejectUnknownKeys(element, knownStageKey, "quest stage")) {
            return std::unexpected(*error);
        }
        fo4::forms::QuestStage stage;
        pendingConditions.emplace_back();
        std::uint32_t index = 0;
        std::uint32_t flags = 0;
        if (!questUnsigned(element, "index", 0, index) ||
            index > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "stage index must be 0..65535"
            });
        }
        if (!questUnsigned(element, "flags", 0, flags) || flags > 255) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "stage flags must be 0..255"
            });
        }
        stage.index = static_cast<std::uint16_t>(index);
        stage.flags = static_cast<std::uint8_t>(flags);
        if (!indexes.insert(stage.index).second) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "duplicate stage index " + std::to_string(stage.index)
            });
        }

        const auto completeField = element.find("complete");
        const auto failField = element.find("fail");
        if (completeField != element.end() && !completeField->is_null() &&
            !completeField->is_boolean()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete must be a boolean"
            });
        }
        if (failField != element.end() && !failField->is_null() &&
            !failField->is_boolean()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "fail must be a boolean"
            });
        }
        const bool hasComplete = completeField != element.end() &&
                                 completeField->is_boolean();
        const bool hasFail = failField != element.end() && failField->is_boolean();
        const bool hasLogEntries =
            element.contains("logEntries") && !element["logEntries"].is_null();
        const bool hasLogEntry =
            element.contains("logEntry") && !element["logEntry"].is_null();
        if (hasLogEntry && !element["logEntry"].is_string()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "logEntry must be a string"
            });
        }
        if (hasLogEntries && hasLogEntry) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "a stage may set logEntry or logEntries, not both"
            });
        }
        if (hasLogEntries && (hasComplete || hasFail)) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete/fail apply only to the logEntry sugar path"
            });
        }
        if ((hasComplete || hasFail) && !hasLogEntry) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete/fail require a logEntry string"
            });
        }

        nlohmann::json entries = nlohmann::json::array();
        if (hasLogEntries) {
            if (!element["logEntries"].is_array()) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "logEntries must be an array"
                });
            }
            entries = element["logEntries"];
        } else if (hasLogEntry) {
            nlohmann::json one = nlohmann::json::object();
            one["logText"] = element["logEntry"];
            std::uint32_t sugarFlags = 0;
            if (hasComplete && completeField->get<bool>()) sugarFlags |= 1u;
            if (hasFail && failField->get<bool>()) sugarFlags |= 2u;
            if (sugarFlags != 0) one["flags"] = sugarFlags;
            entries.push_back(std::move(one));
        }

        for (const auto& entryJson : entries) {
            if (!entryJson.is_object()) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "every log entry must be an object"
                });
            }
            if (const auto error =
                    rejectUnknownKeys(entryJson, knownLogEntryKey, "log entry")) {
                return std::unexpected(*error);
            }
            fo4::forms::QuestLogEntry entry;
            std::uint32_t entryFlags = 0;
            if (!questUnsigned(entryJson, "flags", 0, entryFlags) || entryFlags > 255) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "log entry flags must be 0..255"
                });
            }
            entry.flags = static_cast<std::uint8_t>(entryFlags);

            std::optional<std::string> note;
            std::optional<std::string> logText;
            if (!questOptionalString(entryJson, "note", note) ||
                !questOptionalString(entryJson, "logText", logText)) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "note and logText must be strings"
                });
            }
            if (localized && logText.has_value()) {
                return std::unexpected(Error{
                    "LocalizedLogTextUnsupported",
                    "new QUST CNAM log text in a localized plugin needs a string-table ID"
                });
            }
            if (note.has_value()) {
                if (note->find('\0') != std::string::npos) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "note cannot contain an embedded NUL"
                    });
                }
                entry.note = note;
                fo4::Subrecord nam2;
                nam2.signature = fo4::Signature::from_string("NAM2");
                nam2.data = zstringBytes(*note);
                entry.members.push_back(std::move(nam2));
            }
            if (logText.has_value()) {
                if (logText->find('\0') != std::string::npos) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "logText cannot contain an embedded NUL"
                    });
                }
                fo4::schema::LocalizedString value;
                value.text = logText;
                entry.log_text = value;
                fo4::Subrecord cnam;
                cnam.signature = fo4::Signature::from_string("CNAM");
                cnam.data = fo4::schema::write_localized_string(value);
                entry.members.push_back(std::move(cnam));
            }
            std::vector<PendingQuestCondition> parsedConditions;
            if (entryJson.contains("conditions") && !entryJson["conditions"].is_null()) {
                if (!entryJson["conditions"].is_array()) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "conditions must be an array"
                    });
                }
                for (const auto& conditionJson : entryJson["conditions"]) {
                    PendingQuestCondition pending;
                    if (const auto error = parseQuestCondition(
                            conditionJson, graph_, pending, conditionKeys)) {
                        return std::unexpected(*error);
                    }
                    parsedConditions.push_back(std::move(pending));
                }
            }
            stage.log_entries.push_back(std::move(entry));
            pendingConditions.back().push_back(std::move(parsedConditions));
        }
        stages.stages.push_back(std::move(stage));
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& key : conditionKeys) ensureOwnerMaster(mutated, key.owner());
        const auto context = fo4::plugin_form_id_context(mutated);
        const auto encodeKey = [&](const std::optional<fo4::FormKey>& key) -> std::uint32_t {
            if (!key.has_value()) return 0;
            return fo4::encode_plugin_form_id(*key, context).value();
        };
        for (std::size_t stageIndex = 0; stageIndex < stages.stages.size(); ++stageIndex) {
            auto& stage = stages.stages[stageIndex];
            for (std::size_t entryIndex = 0; entryIndex < stage.log_entries.size(); ++entryIndex) {
                const auto& pending = pendingConditions[stageIndex][entryIndex];
                if (pending.empty()) continue;
                std::vector<fo4::CtdaCondition> encoded;
                encoded.reserve(pending.size());
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
                    encoded.push_back(std::move(condition.condition));
                }
                auto run = fo4::serialize_ctda_run(encoded);
                auto& members = stage.log_entries[entryIndex].members;
                members.insert(members.begin(), run.begin(), run.end());
            }
        }
        fo4::forms::write_quest_stages(mutated, questKey, stages);
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
            recordText + " in " + pluginName + ". Written to disk.");
    } catch (const fo4::forms::QuestError& error) {
        const std::string message = error.what();
        if (message.find("QUST VMAD") != std::string::npos) {
            return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", message});
        }
        return std::unexpected(Error{"SetQuestStagesFailed", message});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestStagesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
