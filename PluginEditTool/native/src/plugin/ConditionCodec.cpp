#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
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

constexpr std::int64_t kParamMin = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t kParamMax = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kRunOnReference = 2;

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

enum class ParamKind { Form, Integer, Float };

ParamKind paramKind(const std::string& name, std::size_t slot, std::uint8_t ctdaFlags,
                    std::uint32_t runOn) {
    if (name.empty()) return ParamKind::Form;
    const auto declared = ConditionFunctions::paramsOf(name);
    if (slot >= declared.size()) return ParamKind::Integer;
    const auto type = lower(declared[slot]);
    if (type == "float") return ParamKind::Float;
    if (type == "reference" || type == "actor" || type == "package") {
        if ((ctdaFlags & fo4::k_ctda_flag_aliases) != 0) {
            const auto currentPackage =
                slot == 0 && runOn == 5 && lower(name) == "getiscurrentpackage";
            return currentPackage ? ParamKind::Form : ParamKind::Integer;
        }
        if ((ctdaFlags & fo4::k_ctda_flag_pack_data) != 0) return ParamKind::Integer;
    }
    static const std::vector<std::string> kIntegerTypes = {
        "integer", "string", "alias", "event", "packdata", "queststage", "alignment",
        "axis", "castingsource", "crimetype", "criticalstage", "formtype", "miscstat",
        "sex", "wardstate"};
    return std::find(kIntegerTypes.begin(), kIntegerTypes.end(), type) != kIntegerTypes.end()
               ? ParamKind::Integer
               : ParamKind::Form;
}

float bitsFloat(std::int32_t raw) {
    const auto bits = static_cast<std::uint32_t>(raw);
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

nlohmann::json describeParam(std::int32_t value, ParamKind kind, const fo4::Plugin& plugin) {
    switch (kind) {
        case ParamKind::Integer:
            return value;
        case ParamKind::Float: {
            const auto number = bitsFloat(value);
            if (std::isfinite(number)) return number;
            std::ostringstream out;
            out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<std::uint32_t>(value);
            return out.str();
        }
        case ParamKind::Form:
            break;
    }
    return formIdString(static_cast<std::uint32_t>(value), plugin);
}

bool isHexBits(const std::string& text) {
    return text.size() > 2 && text.size() <= 10 && text.compare(0, 2, "0x") == 0 &&
           std::all_of(text.begin() + 2, text.end(),
                       [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

nlohmann::json cisText(const std::optional<std::string>& bytes) {
    return bytes ? nlohmann::json(fo4::text::windows1252_to_utf8(*bytes)) : nlohmann::json(nullptr);
}

std::optional<std::uint32_t> runOnFromText(const std::string& text) {
    if (const auto value = runOnValue(text)) return value;
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size()) return value;
    return std::nullopt;
}

}  // namespace

std::optional<std::string> conditionCisFromUtf8(std::string_view utf8) {
    auto bytes = fo4::text::utf8_to_windows1252(utf8);
    if (!bytes || bytes->find('\0') != std::string::npos) return std::nullopt;
    return bytes;
}

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
    if (condition.is_event_data()) {
        if (condition.param1 != 0) d["param1"] = condition.param1;
        if (condition.param2 != 0) d["param2"] = condition.param2;
    } else {
        const auto name = functionName(condition.function);
        const auto flags = condition.flags();
        if (condition.param1 != 0) {
            d["param1"] = describeParam(condition.param1,
                                        paramKind(name, 0, flags, condition.run_on), plugin);
        }
        if (condition.param2 != 0) {
            d["param2"] = describeParam(condition.param2,
                                        paramKind(name, 1, flags, condition.run_on), plugin);
        }
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
    const auto cis1 = cisText(condition.parameter1_string);
    const auto cis2 = cisText(condition.parameter2_string);
    if (!cis1.is_null()) d["cis1"] = cis1;
    if (!cis2.is_null()) d["cis2"] = cis2;
    d["_native"] = {{"ctda", std::move(bytes)}, {"cis1", cis1}, {"cis2", cis2}};
    return d;
}

std::optional<fo4::RawFormId> resolveConditionFormId(std::string_view text,
                                                     const fo4::Plugin& plugin,
                                                     const EditorIdResolver& resolveEditorId) {
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
    try {
        const auto reject = [](const std::string& message) { throw std::invalid_argument(message); };
        if (!element.is_object()) reject("every condition must be an object");
        constexpr std::string_view keys[] = {"function", "operator", "param1", "param2", "value",
                                             "compareGlobal", "runOn", "reference", "flags", "cis1", "cis2", "_native", "_edited"};
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
                metadata["ctda"].size() != 32) reject("invalid native preservation data");
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
                const auto stored = conditionCisFromUtf8(value.get<std::string>());
                if (!stored) reject("preserved CIS string is not valid UTF-8 text representable in Windows-1252");
                target = *stored;
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
            const auto value = runOnFromText(text("runOn", "Subject"));
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
        const auto runOnReference = condition.run_on == kRunOnReference;
        if (!runOnReference && changed("reference") && condition.reference != 0) reject("reference requires runOn Reference");
        if (!runOnReference && changed("runOn") && !changed("reference")) condition.reference = 0;
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
        const auto applyParam = [&](const char* key, std::size_t slot, std::int32_t& target) {
            if (!changed(key)) return;
            target = 0;
            if (!element.contains(key)) return;
            const auto& value = element[key];
            const auto kind = condition.is_event_data()
                                  ? ParamKind::Integer
                                  : paramKind(fnName, slot, condition.flags(), condition.run_on);
            if (kind == ParamKind::Float) {
                float number = 0.0f;
                if (value.is_number()) {
                    number = value.get<float>();
                } else if (value.is_string()) {
                    const auto source = value.get<std::string>();
                    if (source.empty()) return;
                    if (isHexBits(source)) {
                        target = static_cast<std::int32_t>(std::strtoul(source.c_str() + 2, nullptr, 16));
                        return;
                    }
                    const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), number);
                    if (error != std::errc{} || end != source.data() + source.size()) reject(std::string(key) + " " + source + " is not a number");
                } else reject(std::string(key) + " must be a number or numeric string");
                if (!std::isfinite(number)) reject(std::string(key) + " must be a finite number");
                std::memcpy(&target, &number, sizeof(number));
                return;
            }
            if (value.is_number_unsigned()) {
                const auto unsignedValue = value.get<std::uint64_t>();
                if (unsignedValue > kParamMax) reject(std::string(key) + " " + value.dump() + " is outside [-2147483648, 4294967295]");
                target = static_cast<std::int32_t>(static_cast<std::uint32_t>(unsignedValue));
            } else if (value.is_number_integer()) {
                if (value < kParamMin || value > kParamMax) reject(std::string(key) + " " + value.dump() + " is outside [-2147483648, 4294967295]");
                target = static_cast<std::int32_t>(static_cast<std::uint32_t>(value.get<std::int64_t>()));
            } else if (value.is_number_float()) {
                const auto number = value.get<double>();
                if (kind != ParamKind::Integer || std::trunc(number) != number || number < kParamMin || number > kParamMax) {
                    reject(std::string(key) + " must be an integer or string");
                }
                target = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(number)));
            } else if (value.is_string()) {
                const auto source = value.get<std::string>();
                if (source.empty()) return;
                if (isHexBits(source)) {
                    target = static_cast<std::int32_t>(std::strtoul(source.c_str() + 2, nullptr, 16));
                    return;
                }
                std::int64_t integer = 0;
                const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), integer);
                if (error == std::errc{} && end == source.data() + source.size()) {
                    if (integer < kParamMin || integer > kParamMax) reject(std::string(key) + " " + source + " is outside [-2147483648, 4294967295]");
                    target = static_cast<std::int32_t>(static_cast<std::uint32_t>(integer));
                    return;
                }
                if (const auto raw = resolveConditionFormId(source, plugin, resolveEditorId)) {
                    const auto bits = raw->value();
                    std::memcpy(&target, &bits, sizeof(target));
                } else reject("unresolved condition parameter");
            } else reject("condition parameter must be an integer or string");
        };
        applyParam("param1", 0, condition.param1);
        applyParam("param2", 1, condition.param2);
        const auto declared = ConditionFunctions::paramsOf(fnName);
        const auto applyCis = [&](const char* key, std::size_t slot, std::optional<std::string>& target) {
            if (!changed(key)) return;
            const auto listed = std::find(edited.begin(), edited.end(), key) != edited.end();
            if (preserved && !listed && !element.contains(key)) return;
            target.reset();
            if (!element.contains(key)) return;
            if (!element[key].is_string()) reject(std::string(key) + " must be a string");
            const auto stored = conditionCisFromUtf8(element[key].get<std::string>());
            if (!stored) reject(std::string(key) + " is not valid UTF-8 text representable in Windows-1252");
            if (slot >= declared.size() || ConditionFunctions::editorFor(declared[slot]).first != "text") {
                reject(std::string(key) + " is only valid when parameter " + std::to_string(slot + 1) + " is String or Event");
            }
            target = *stored;
        };
        applyCis("cis1", 0, condition.parameter1_string);
        applyCis("cis2", 1, condition.parameter2_string);
        if (changed("value") || changed("compareGlobal")) {
            if (element.contains("compareGlobal")) {
                const auto raw = resolveConditionFormId(text("compareGlobal"), plugin, resolveEditorId);
                if (!raw) reject("unresolved comparison global");
                condition.comparison_raw = raw->value();
                condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte | fo4::k_ctda_flag_use_global);
            } else {
                float value = 0.0f;
                if (element.contains("value")) {
                    if (!element["value"].is_number()) reject("comparison value must be a finite number");
                    const auto number = element["value"].get<double>();
                    if (!std::isfinite(number) || std::fabs(number) > std::numeric_limits<float>::max()) {
                        reject("comparison value must be a finite number");
                    }
                    value = static_cast<float>(number);
                }
                std::memcpy(&condition.comparison_raw, &value, sizeof(value));
                condition.flags_byte = static_cast<std::uint8_t>(condition.flags_byte & ~fo4::k_ctda_flag_use_global);
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
