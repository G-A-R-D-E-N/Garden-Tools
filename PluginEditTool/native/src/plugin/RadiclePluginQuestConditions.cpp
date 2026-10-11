#include "RadiclePluginQuestConditions.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4ide/plugin/ConditionFunctions.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

namespace {

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

bool knownConditionKey(std::string_view key) {
    return key == "function" || key == "operator" || key == "param1" ||
           key == "param2" || key == "value" || key == "compareGlobal" ||
           key == "runOn" || key == "reference" || key == "flags" ||
           key == "cis1" || key == "cis2";
}

}  // namespace

bool isPlacedSignature(fo4::Signature signature) {
    const auto text = signature.to_string();
    return text == "REFR" || text == "ACHR" || text == "PGRE" || text == "PHZD" ||
           text == "PMIS" || text == "PARW" || text == "PBAR" || text == "PBEA" ||
           text == "PCON" || text == "PFLA";
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

namespace {

bool editorTokenMatches(fo4::Signature signature, std::string_view token) {
    const auto name = signature.to_string();
    const auto wanted = support::asciiLower(token);
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

}  // namespace

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

std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label,
                                       std::string_view errorType) {
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

namespace {

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

}  // namespace

std::optional<Error> parseQuestCondition(
    const nlohmann::json& element,
    const fo4::ConflictGraph& graph,
    PendingQuestCondition& out,
    std::vector<fo4::FormKey>& conditionKeys,
    std::string_view errorType,
    std::string_view surface) {
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
    if (support::asciiLower(functionText) == "geteventdata" ||
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
            if (support::asciiLower(paramType) == "factionnull") return std::nullopt;
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

}  // namespace fo4ide::plugin
