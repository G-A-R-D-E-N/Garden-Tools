#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace fo4ide::plugin {
namespace {

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

std::string formIdString(std::uint32_t raw, const fo4::Plugin& plugin) {
    const fo4::RawFormId formId{raw};
    if (!formId.is_null()) {
        try {
            const auto key = fo4::resolve_plugin_form_id(formId, fo4::plugin_form_id_context(plugin));
            const auto text = key.to_string();
            if (!text.empty()) return text;
        } catch (const std::exception&) {
            // fall through to the raw formatting below
        }
    }
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << raw << ':';
    return out.str();
}

std::optional<fo4::RawFormId> placeholderFormId(std::string_view text) {
    if (text.size() != 9 || text[8] != ':') return std::nullopt;
    std::uint32_t raw = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + 8, raw, 16);
    if (error != std::errc{} || end != text.data() + 8) return std::nullopt;
    return fo4::RawFormId{raw};
}

constexpr std::uint32_t kNamedFlagMask = fo4::k_ctda_flag_or | fo4::k_ctda_flag_aliases |
                                         fo4::k_ctda_flag_pack_data | fo4::k_ctda_flag_swap;

std::optional<std::uint32_t> flagValue(std::string_view name) {
    const auto needle = lower(name);
    if (needle == "or") return fo4::k_ctda_flag_or;
    if (needle == "parametersusealiases") return fo4::k_ctda_flag_aliases;
    if (needle == "usepackdata") return fo4::k_ctda_flag_pack_data;
    if (needle == "swapsubjectandtarget") return fo4::k_ctda_flag_swap;
    char* end = nullptr;
    const auto value = std::strtoul(name.data(), &end, 10);
    if (end != name.data() && *end == '\0' && (value | kNamedFlagMask) == kNamedFlagMask) {
        return static_cast<std::uint32_t>(value);
    }
    return std::nullopt;
}

constexpr std::int64_t kParamMin = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t kParamMax = std::numeric_limits<std::uint32_t>::max();

bool isPresent(const nlohmann::json& element, const char* key) {
    return element.contains(key) && !element[key].is_null();
}

std::string scalarText(const nlohmann::json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

std::optional<std::uint32_t> runOnFromText(const std::string& text) {
    if (const auto value = runOnValue(text)) return value;
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    char* end = nullptr;
    const auto value = std::strtoull(text.c_str(), &end, 10);
    if (*end != '\0' || value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

}  // namespace

std::string conditionOperatorString(std::uint8_t compareOperator) {
    switch (compareOperator) {
        case 1: return "!=";
        case 2: return ">";
        case 3: return ">=";
        case 4: return "<";
        case 5: return "<=";
        default: return "==";
    }
}

std::string conditionFlagString(std::uint8_t flags) {
    // C# Enum.ToString() on a non-[Flags] enum yields the single matching
    // member name, or the numeric string when no member matches exactly.
    switch (flags) {
        case fo4::k_ctda_flag_or: return "OR";
        case fo4::k_ctda_flag_aliases: return "ParametersUseAliases";
        case fo4::k_ctda_flag_pack_data: return "UsePackData";
        case fo4::k_ctda_flag_swap: return "SwapSubjectAndTarget";
        default: return std::to_string(flags);
    }
}

nlohmann::json describeCondition(const fo4::CtdaCondition& condition,
                                 const fo4::Plugin& plugin) {
    nlohmann::json d = nlohmann::json::object();
    d["function"] = condition.is_event_data() ? "GetEventData" : functionName(condition.function);
    d["operator"] = conditionOperatorString(condition.compare_operator());
    if (!condition.is_event_data()) {
        if (condition.param1 != 0) d["param1"] = formIdString(condition.param1, plugin);
        if (condition.param2 != 0) d["param2"] = formIdString(condition.param2, plugin);
    }
    if (condition.use_global()) {
        d["compareGlobal"] = formIdString(condition.comparison_raw, plugin);
    } else {
        d["value"] = condition.comparison_float();
    }
    auto runOn = runOnName(condition.run_on);
    d["runOn"] = runOn.empty() ? std::to_string(condition.run_on) : runOn;
    if (condition.reference != 0) {
        d["reference"] = formIdString(condition.reference, plugin);
    }
    // Exposed Flags clear UseGlobal, mirroring Mutagen's GetFlagsCustom.
    const auto flags = static_cast<std::uint8_t>(condition.flags() & ~fo4::k_ctda_flag_use_global);
    if (flags != 0) {
        d["flags"] = conditionFlagString(flags);
    }
    return d;
}

std::optional<fo4::RawFormId> resolveConditionFormId(std::string_view text,
                                                     const fo4::Plugin& plugin,
                                                     const EditorIdResolver& resolveEditorId) {
    if (text.empty()) return std::nullopt;
    if (const auto placeholder = placeholderFormId(text)) return placeholder;
    try {
        const auto key = fo4::FormKey::parse(text);
        const auto raw = fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(plugin));
        return fo4::RawFormId{raw.value()};
    } catch (const std::exception&) {
        // not a FormKey text — try the editor-ID resolver below
    }
    if (resolveEditorId) {
        return resolveEditorId(text);
    }
    return std::nullopt;
}

std::optional<fo4::CtdaCondition> buildCondition(const nlohmann::json& element,
                                                 const fo4::Plugin& plugin,
                                                 const EditorIdResolver& resolveEditorId,
                                                 std::string& failure) {
    failure.clear();
    const auto fnName = element.is_object() && element.contains("function") &&
                                element["function"].is_string()
                            ? element["function"].get<std::string>()
                            : std::string{};
    const auto fnIndex = functionIndex(fnName);
    if (!fnIndex.has_value()) {
        failure = "unknown function '" + fnName + "'";
        return std::nullopt;
    }

    fo4::CtdaCondition condition;
    condition.function = *fnIndex;
    condition.unknown3 = -1;

    if (isPresent(element, "runOn")) {
        const auto runOn = element["runOn"].is_string()
                               ? runOnFromText(element["runOn"].get<std::string>())
                               : std::nullopt;
        if (!runOn.has_value()) {
            failure = "unknown runOn '" + scalarText(element["runOn"]) + "'";
            return std::nullopt;
        }
        condition.run_on = *runOn;
    }
    if (isPresent(element, "reference")) {
        const auto raw = element["reference"].is_string()
                             ? resolveConditionFormId(element["reference"].get<std::string>(),
                                                      plugin, resolveEditorId)
                             : std::nullopt;
        if (!raw.has_value()) {
            failure = "reference '" + scalarText(element["reference"]) +
                      "' does not resolve to a form ID";
            return std::nullopt;
        }
        condition.reference = raw->value();
    }

    const auto reject = [&](const char* key, const std::string& text) {
        failure = std::string(key) + " " + text + " is outside [-2147483648, 4294967295]";
        return false;
    };
    const auto applyParam = [&](const char* key, std::int32_t& target) {
        if (!isPresent(element, key)) return true;
        const auto& value = element[key];
        std::string text;
        std::int64_t parsed = 0;
        if (value.is_string()) {
            text = value.get<std::string>();
            if (text.empty()) return true;
            char* end = nullptr;
            parsed = std::strtoll(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != '\0') {
                const auto raw = resolveConditionFormId(text, plugin, resolveEditorId);
                if (!raw.has_value()) {
                    failure = std::string(key) + " '" + text + "' is not an integer or form ID";
                    return false;
                }
                target = static_cast<std::int32_t>(raw->value());
                return true;
            }
        } else if (value.is_number_integer()) {
            text = value.dump();
            if (value.is_number_unsigned() &&
                value.get<std::uint64_t>() > static_cast<std::uint64_t>(kParamMax)) {
                return reject(key, text);
            }
            parsed = value.get<std::int64_t>();
        } else {
            failure = std::string(key) + " must be an integer or form ID text";
            return false;
        }
        if (parsed < kParamMin || parsed > kParamMax) return reject(key, text);
        target = static_cast<std::int32_t>(static_cast<std::uint32_t>(parsed));
        return true;
    };
    if (!applyParam("param1", condition.param1) || !applyParam("param2", condition.param2)) {
        return std::nullopt;
    }

    // Compare operator: high 3 bits of the flags byte.
    const auto opText = isPresent(element, "operator") ? scalarText(element["operator"])
                                                       : std::string{"=="};
    std::uint8_t compareOperator = 0;
    if (opText == "==" || opText == "=" || opText == "EqualTo") {
        compareOperator = 0;
    } else if (opText == "!=" || opText == "<>" || opText == "NotEqualTo") {
        compareOperator = 1;
    } else if (opText == ">" || opText == "GreaterThan") {
        compareOperator = 2;
    } else if (opText == ">=" || opText == "GreaterThanOrEqualTo") {
        compareOperator = 3;
    } else if (opText == "<" || opText == "LessThan") {
        compareOperator = 4;
    } else if (opText == "<=" || opText == "LessThanOrEqualTo") {
        compareOperator = 5;
    } else {
        failure = "unknown operator '" + opText + "'";
        return std::nullopt;
    }
    condition.flags_byte = static_cast<std::uint8_t>(compareOperator << 5);

    if (isPresent(element, "flags")) {
        if (!element["flags"].is_string()) {
            failure = "flags must be a string";
            return std::nullopt;
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
            const auto flag = flagValue(part);
            if (!flag.has_value()) {
                failure = "unknown flag '" + part + "'";
                return std::nullopt;
            }
            condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte | *flag);
        }
    }

    float value = 0.0f;
    if (isPresent(element, "value")) {
        if (!element["value"].is_number()) {
            failure = "value must be a number";
            return std::nullopt;
        }
        const auto number = element["value"].get<double>();
        if (!std::isfinite(number) || std::fabs(number) > std::numeric_limits<float>::max()) {
            failure = "value out of float range";
            return std::nullopt;
        }
        value = static_cast<float>(number);
    }
    if (isPresent(element, "compareGlobal")) {
        const auto raw = element["compareGlobal"].is_string()
                             ? resolveConditionFormId(element["compareGlobal"].get<std::string>(),
                                                      plugin, resolveEditorId)
                             : std::nullopt;
        if (!raw.has_value()) {
            failure = "compareGlobal '" + scalarText(element["compareGlobal"]) +
                      "' does not resolve to a form ID";
            return std::nullopt;
        }
        condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte |
                                                         fo4::k_ctda_flag_use_global);
        condition.comparison_raw = raw->value();
    } else {
        static_assert(sizeof(value) == sizeof(condition.comparison_raw));
        std::memcpy(&condition.comparison_raw, &value, sizeof(value));
    }
    return condition;
}

}  // namespace fo4ide::plugin
