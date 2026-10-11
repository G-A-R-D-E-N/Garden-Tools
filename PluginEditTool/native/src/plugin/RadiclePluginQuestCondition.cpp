#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

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

bool knownConditionKey(std::string_view key) {
    return key == "function" || key == "operator" || key == "param1" ||
           key == "param2" || key == "value" || key == "compareGlobal" ||
           key == "runOn" || key == "reference" || key == "flags" ||
           key == "cis1" || key == "cis2";
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

}

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
        float value = 0.0f;
        if (hasValue) {
            const auto number = element["value"].is_number() ? element["value"].get<double>() : 0.0;
            if (!element["value"].is_number() || !std::isfinite(number) ||
                std::fabs(number) > std::numeric_limits<float>::max()) {
                return fail("comparison value must be a finite number");
            }
            value = static_cast<float>(number);
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
        const auto stored = conditionCisFromUtf8(element[key].get<std::string>());
        if (!stored) {
            return fail(std::string(key) + " is not valid UTF-8 text representable in Windows-1252");
        }
        target = *stored;
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

void carryConditionPadding(const std::vector<fo4::CtdaCondition>& previous,
                           std::vector<fo4::CtdaCondition>& conditions) {
    const auto withoutPadding = [](fo4::CtdaCondition condition) {
        condition.padding = {};
        return condition;
    };
    std::vector<bool> used(previous.size(), false);
    std::vector<bool> carried(conditions.size(), false);
    for (std::size_t k = 0; k < conditions.size(); ++k) {
        auto& condition = conditions[k];
        condition.padding = {};
        const auto payload = fo4::serialize_ctda_condition(condition);
        for (std::size_t index = 0; index < previous.size(); ++index) {
            const auto& candidate = previous[index];
            if (used[index] ||
                fo4::serialize_ctda_condition(withoutPadding(candidate)) != payload ||
                candidate.parameter1_string != condition.parameter1_string ||
                candidate.parameter2_string != condition.parameter2_string) {
                continue;
            }
            condition.padding = candidate.padding;
            used[index] = true;
            carried[k] = true;
            break;
        }
    }
    for (std::size_t k = 0; k < conditions.size() && k < previous.size(); ++k) {
        if (carried[k] || used[k]) continue;
        conditions[k].padding = previous[k].padding;
        used[k] = true;
    }
}

std::optional<Error> adapterConditionUnrepresentable(
    const fo4::Plugin& plugin,
    const fo4::ConflictGraph& graph,
    const fo4::CtdaCondition& condition,
    std::string_view surface) {
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

}
