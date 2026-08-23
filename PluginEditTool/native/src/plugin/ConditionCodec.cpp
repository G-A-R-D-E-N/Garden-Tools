#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

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

// Mirrors Mutagen's FormKey.ToString() for a raw FormID resolved in the
// plugin's master context: "000812:Master.esp". Raws whose master index is
// out of range fall back to the empty-owner form "000001:" exactly like
// Mutagen's FormKey.Factory(reference: true) does for unmatched indices.
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
    out << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
        << (raw & 0x00FFFFFFu) << ':';
    return out.str();
}

std::optional<std::uint32_t> flagValue(std::string_view name) {
    const auto needle = lower(name);
    if (needle == "or") return fo4::k_ctda_flag_or;
    if (needle == "parametersusealiases") return fo4::k_ctda_flag_aliases;
    if (needle == "usepackdata") return fo4::k_ctda_flag_pack_data;
    if (needle == "swapsubjectandtarget") return fo4::k_ctda_flag_swap;
    char* end = nullptr;
    const auto value = std::strtoul(name.data(), &end, 10);
    if (end != name.data() && *end == '\0' && value <= 0xFFu) {
        return static_cast<std::uint32_t>(value);
    }
    return std::nullopt;
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

    if (element.contains("runOn") && element["runOn"].is_string()) {
        if (const auto value = runOnValue(element["runOn"].get<std::string>())) {
            condition.run_on = *value;
        }
    }
    if (element.contains("reference") && element["reference"].is_string()) {
        if (const auto raw = resolveConditionFormId(element["reference"].get<std::string>(),
                                                   plugin, resolveEditorId)) {
            condition.reference = raw->value();
        }
    }

    // ApplyParam: numbers land as-is; strings try integer first, then a
    // FormKey / editor-ID resolution.
    const auto applyParam = [&](const char* key, std::int32_t& target) {
        if (!element.contains(key)) return;
        const auto& value = element[key];
        if (value.is_number_integer()) {
            target = value.get<std::int32_t>();
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (!text.empty()) {
                char* end = nullptr;
                const auto parsed = std::strtol(text.c_str(), &end, 10);
                if (end != text.c_str() && *end == '\0') {
                    target = static_cast<std::int32_t>(parsed);
                } else if (const auto raw = resolveConditionFormId(text, plugin, resolveEditorId)) {
                    target = static_cast<std::int32_t>(raw->value());
                }
            }
        }
    };
    applyParam("param1", condition.param1);
    applyParam("param2", condition.param2);

    // Compare operator: high 3 bits of the flags byte.
    const auto opText = element.contains("operator") && element["operator"].is_string()
                            ? element["operator"].get<std::string>()
                            : std::string{};
    std::uint8_t compareOperator = 0;
    if (opText == "!=" || opText == "<>" || opText == "NotEqualTo") {
        compareOperator = 1;
    } else if (opText == ">" || opText == "GreaterThan") {
        compareOperator = 2;
    } else if (opText == ">=" || opText == "GreaterThanOrEqualTo") {
        compareOperator = 3;
    } else if (opText == "<" || opText == "LessThan") {
        compareOperator = 4;
    } else if (opText == "<=" || opText == "LessThanOrEqualTo") {
        compareOperator = 5;
    }
    condition.flags_byte = static_cast<std::uint8_t>(compareOperator << 5);

    if (element.contains("flags") && element["flags"].is_string()) {
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
            if (const auto flag = flagValue(part)) {
                condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte | *flag);
            }
        }
    }

    if (element.contains("compareGlobal") && element["compareGlobal"].is_string()) {
        if (const auto raw = resolveConditionFormId(element["compareGlobal"].get<std::string>(),
                                                   plugin, resolveEditorId)) {
            condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte |
                                                             fo4::k_ctda_flag_use_global);
            condition.comparison_raw = raw->value();
        } else {
            condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte |
                                                             fo4::k_ctda_flag_use_global);
            condition.comparison_raw = 0;
        }
    } else {
        float value = 1.0f;
        if (element.contains("value") && element["value"].is_number()) {
            value = element["value"].get<float>();
        }
        static_assert(sizeof(value) == sizeof(condition.comparison_raw));
        std::memcpy(&condition.comparison_raw, &value, sizeof(value));
    }
    return condition;
}

}  // namespace fo4ide::plugin
