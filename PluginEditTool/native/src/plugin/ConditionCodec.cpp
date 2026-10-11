#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <limits>

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
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), value);
    if (error == std::errc{} && end == name.data() + name.size() && value <= 0x1Fu) {
        return value;
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
        case 0: return "==";
        default: return "Unknown (" + std::to_string(compareOperator) + ")";
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
    if (d["function"] == "") d["function"] = "Unknown (" + std::to_string(condition.function) + ")";
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
    auto bytes = nlohmann::json::array();
    for (const auto byte : fo4::serialize_ctda_condition(condition)) {
        bytes.push_back(std::to_integer<std::uint8_t>(byte));
    }
    d["_native"] = {{"ctda", std::move(bytes)},
                    {"cis1", condition.parameter1_string ? nlohmann::json(*condition.parameter1_string) : nlohmann::json(nullptr)},
                    {"cis2", condition.parameter2_string ? nlohmann::json(*condition.parameter2_string) : nlohmann::json(nullptr)}};
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
    try {
        const auto reject = [](const std::string& message) { throw std::invalid_argument(message); };
        if (!element.is_object()) reject("every condition must be an object");
        constexpr std::string_view keys[] = {"function", "operator", "param1", "param2", "value",
                                             "compareGlobal", "runOn", "reference", "flags", "_native", "_edited"};
        for (const auto& [key, value] : element.items()) {
            (void)value;
            if (std::find(std::begin(keys), std::end(keys), key) == std::end(keys)) reject("unknown condition field '" + key + "'");
        }
        if (!element.contains("function") || !element["function"].is_string()) reject("condition function must be a string");
        if (element.contains("value") && element.contains("compareGlobal")) reject("comparison value and global are mutually exclusive");
        fo4::CtdaCondition condition;
        nlohmann::json baseline;
        const bool preserved = element.contains("_native");
        auto edited = nlohmann::json::array();
        if (element.contains("_edited")) {
            edited = element["_edited"];
            if (!preserved || !edited.is_array()) reject("invalid edited fields");
            for (const auto& key : edited) {
                if (!key.is_string() || std::find(std::begin(keys), std::end(keys) - 2, key.get<std::string>()) == std::end(keys) - 2) reject("invalid edited field");
            }
        }
        if (preserved) {
            const auto& metadata = element["_native"];
            if (!metadata.is_object() || metadata.size() != 3 || !metadata.contains("ctda") ||
                !metadata.contains("cis1") || !metadata.contains("cis2") || !metadata["ctda"].is_array() ||
                metadata["ctda"].size() > 32) reject("invalid native preservation data");
            std::vector<std::byte> bytes;
            for (const auto& byte : metadata["ctda"]) {
                if (!byte.is_number_integer() || byte < 0 || byte > 255) reject("invalid native preservation byte");
                bytes.push_back(std::byte{byte.get<std::uint8_t>()});
            }
            condition = fo4::parse_ctda_condition(bytes);
            if (fo4::serialize_ctda_condition(condition) != bytes) reject("native payload cannot round-trip");
            const auto restoreString = [&](const char* key, std::optional<std::string>& target) {
                const auto& value = metadata[key];
                if (value.is_null()) return;
                if (!value.is_string()) reject("invalid preserved CIS string");
                target = value.get<std::string>();
                if (target->find('\0') != std::string::npos) reject("preserved CIS string contains a terminator");
            };
            restoreString("cis1", condition.parameter1_string);
            restoreString("cis2", condition.parameter2_string);
            baseline = nlohmann::json::parse(describeCondition(condition, plugin).dump());
            baseline.erase("_native");
        }
        const auto changed = [&](const char* key) {
            return !preserved || std::find(edited.begin(), edited.end(), key) != edited.end() ||
                   element.contains(key) != baseline.contains(key) ||
                   (element.contains(key) && element[key] != baseline[key]);
        };
        const auto fnName = element["function"].get<std::string>();
        const auto fnIndex = functionIndex(fnName);
        if (!preserved) {
            if (!fnIndex) reject("unknown function '" + fnName + "'");
            if (lower(fnName) == "geteventdata") reject("new GetEventData conditions are unsupported");
            condition.function = *fnIndex;
        } else {
            if (changed("function")) reject("changing a preserved condition function is unsupported; replace the row");
            if (condition.is_event_data() || lower(fnName) == "geteventdata" || !fnIndex) {
                for (const auto& [key, value] : element.items()) {
                    (void)value;
                    if (key != "_native" && key != "_edited" && changed(key.c_str())) reject("editing event or unknown-function conditions is unsupported");
                }
                if (element.size() != baseline.size() + 1 + element.contains("_edited")) reject("missing preserved condition fields");
                return condition;
            }
        }
        const auto text = [&](const char* key, std::string fallback = {}) {
            if (!element.contains(key)) return fallback;
            if (!element[key].is_string()) reject(std::string(key) + " must be a string");
            return element[key].get<std::string>();
        };
        if (changed("runOn")) {
            const auto value = runOnValue(text("runOn", "Subject"));
            if (!value) reject("unknown runOn value");
            condition.run_on = *value;
        }
        if (changed("reference")) {
            condition.reference = 0;
            if (element.contains("reference")) {
                const auto raw = resolveConditionFormId(text("reference"), plugin, resolveEditorId);
                if (!raw) reject("unresolved condition reference");
                condition.reference = raw->value();
            }
        }
        const auto applyParam = [&](const char* key, std::int32_t& target) {
            if (!changed(key)) return;
            target = 0;
            if (!element.contains(key)) return;
            const auto& value = element[key];
            if (value.is_number_integer()) {
                if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) reject("condition parameter is out of range");
                target = value.get<std::int32_t>();
            } else if (value.is_string()) {
                const auto source = value.get<std::string>();
                if (source.empty()) return;
                const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), target);
                if (error == std::errc{} && end == source.data() + source.size()) return;
                if (const auto raw = resolveConditionFormId(source, plugin, resolveEditorId)) {
                    const auto bits = raw->value();
                    std::memcpy(&target, &bits, sizeof(target));
                } else reject("unresolved condition parameter");
            } else reject("condition parameter must be an integer or string");
        };
        applyParam("param1", condition.param1);
        applyParam("param2", condition.param2);
        if (changed("operator")) {
            const auto op = text("operator", "==");
            std::uint8_t index = 0;
            if (op == "!=" || op == "<>" || op == "NotEqualTo") index = 1;
            else if (op == ">" || op == "GreaterThan") index = 2;
            else if (op == ">=" || op == "GreaterThanOrEqualTo") index = 3;
            else if (op == "<" || op == "LessThan") index = 4;
            else if (op == "<=" || op == "LessThanOrEqualTo") index = 5;
            else if (op != "==" && op != "=" && op != "EqualTo") reject("unknown comparison operator");
            condition.flags_byte = static_cast<std::uint8_t>((condition.flags_byte & 0x1Fu) | (index << 5));
        }
        if (changed("flags")) {
            std::uint32_t flags = 0;
            std::istringstream stream(text("flags"));
            std::string part;
            while (std::getline(stream, part, ',')) {
                part.erase(part.begin(), std::find_if(part.begin(), part.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                part.erase(std::find_if(part.rbegin(), part.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), part.end());
                if (part.empty()) continue;
                const auto flag = flagValue(part);
                if (!flag || (*flag & fo4::k_ctda_flag_use_global)) reject("unknown flags or UseGlobal outside compareGlobal");
                flags |= *flag;
            }
            condition.flags_byte = static_cast<std::uint8_t>((condition.flags_byte & (0xe0u | fo4::k_ctda_flag_use_global)) | flags);
        }
        if (changed("value") || changed("compareGlobal")) {
            if (element.contains("compareGlobal")) {
                const auto raw = resolveConditionFormId(text("compareGlobal"), plugin, resolveEditorId);
                if (!raw || raw->is_null()) reject("unresolved comparison global");
                condition.comparison_raw = raw->value();
                condition.flags_byte |= fo4::k_ctda_flag_use_global;
            } else {
                float value = 1.0f;
                if (element.contains("value")) {
                    if (!element["value"].is_number()) reject("comparison value must be a finite number");
                    value = element["value"].get<float>();
                }
                if (!std::isfinite(value)) reject("comparison value must be a finite number");
                std::memcpy(&condition.comparison_raw, &value, sizeof(value));
                condition.flags_byte &= static_cast<std::uint8_t>(~fo4::k_ctda_flag_use_global);
            }
        }
        return condition;
    } catch (const std::exception& error) {
        failure = error.what();
        return std::nullopt;
    }
}

std::optional<std::vector<fo4::CtdaCondition>> buildConditions(
    const nlohmann::json& document, const fo4::Plugin& plugin,
    std::span<const fo4::CtdaCondition> existing,
    const EditorIdResolver& resolveEditorId, std::string& failure) {
    failure.clear();
    if (!document.is_array()) {
        failure = "expected a condition array";
        return std::nullopt;
    }
    std::vector<nlohmann::json> seeds;
    for (const auto& condition : existing) seeds.push_back(describeCondition(condition, plugin)["_native"]);
    std::vector<bool> used(seeds.size(), false);
    std::vector<fo4::CtdaCondition> built;
    for (const auto& element : document) {
        if (element.is_object() && element.contains("_native")) {
            std::size_t match = 0;
            while (match < seeds.size() && (used[match] || seeds[match] != element["_native"])) ++match;
            if (match == seeds.size()) {
                failure = "native preservation data does not match an unused source row";
                return std::nullopt;
            }
            used[match] = true;
        }
        auto condition = buildCondition(element, plugin, resolveEditorId, failure);
        if (!condition) {
            failure = "#" + std::to_string(built.size() + 1) + ": " + failure;
            return std::nullopt;
        }
        built.push_back(std::move(*condition));
    }
    return built;
}

}  // namespace fo4ide::plugin
