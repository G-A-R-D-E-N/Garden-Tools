#include <fo4ide/plugin/PluginValidation.hpp>

#include "RadiclePluginEffectRows.hpp"
#include "RadiclePluginValidateInternal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fo4/plugin/container.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/record_constraints.hpp>
#include <fo4/schema/vmad.hpp>


namespace fo4ide::plugin {
namespace {

using detail::editorIdOf;
using detail::FindingSink;
using detail::findSubrecord;
using detail::hexFormId;
using detail::sameName;

bool isStartGameEnabled(const fo4::Record& record, bool& known) {
    const auto* dnam = findSubrecord(record, fo4::Signature::from_string("DNAM"));
    if (dnam == nullptr || dnam->data.size() < sizeof(std::uint16_t)) {
        known = false;
        return false;
    }
    std::uint16_t flags = 0;
    std::memcpy(&flags, dnam->data.data(), sizeof(flags));
    known = true;
    return (flags & 0x0001u) != 0u;
}

struct RequiredSubrecord {
    fo4::Signature record;
    fo4::Signature subrecord;
};

const std::vector<RequiredSubrecord>& requiredSubrecords() {
    static const std::vector<RequiredSubrecord> table = [] {
        static constexpr std::string_view types[] = {"SPEL", "ALCH", "ENCH", "INGR", "MGEF", "QUST"};
        std::vector<RequiredSubrecord> rows;
        for (const auto type : types) {
            const auto signature = fo4::Signature::from_string(type);
            const auto constraint = fo4::schema::record_authoring_constraint(signature);
            if (!constraint.has_value()) continue;
            for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
                if (rule.minimum == 0) continue;
                rows.push_back({signature, rule.signature});
            }
        }
        return rows;
    }();
    return table;
}

bool hasEffectList(fo4::Signature signature) {
    static const std::vector<fo4::Signature> types = {
        fo4::Signature::from_string("SPEL"),
        fo4::Signature::from_string("ALCH"),
        fo4::Signature::from_string("ENCH"),
        fo4::Signature::from_string("INGR"),
    };
    return std::find(types.begin(), types.end(), signature) != types.end();
}

void checkMasters(const fo4::Plugin& plugin,
                  const std::vector<ProfileEntry>& profile,
                  FindingSink& sink) {
    const std::string pluginName(plugin.key.filename());
    std::size_t selfIndex = profile.size();
    for (std::size_t index = 0; index < profile.size(); ++index) {
        if (sameName(profile[index].name, pluginName)) {
            selfIndex = index;
            break;
        }
    }

    const auto& masters = plugin.header.masters;
    for (std::size_t masterIndex = 0; masterIndex < masters.size(); ++masterIndex) {
        const std::string masterName(masters[masterIndex].key.filename());
        std::size_t found = profile.size();
        for (std::size_t index = 0; index < profile.size(); ++index) {
            if (sameName(profile[index].name, masterName)) {
                found = index;
                break;
            }
        }
        if (found == profile.size()) {
            sink.add("MASTER_MISSING", Severity::error, {}, {}, "TES4", "MAST",
                     "Declared master " + masterName + " (index " +
                         std::to_string(masterIndex) +
                         ") is not in the active profile, so the game cannot load this plugin");
            continue;
        }
        if (selfIndex != profile.size() && found > selfIndex) {
            sink.add("MASTER_ORDER", Severity::error, {}, {}, "TES4", "MAST",
                     "Declared master " + masterName + " loads at position " +
                         std::to_string(found) + ", after " + pluginName + " at position " +
                         std::to_string(selfIndex) +
                         "; a master must load first or every FormID from it resolves wrong");
        }
    }

    if (selfIndex == profile.size()) {
        sink.add("MASTER_ORDER_UNCHECKED", Severity::info, {}, {}, "TES4", "MAST",
                 pluginName +
                     " is not in the active profile, so master load order was not compared");
    }
}

void checkHedr(const fo4::Plugin& plugin, FindingSink& sink) {
    const auto expected = fo4::expected_hedr_record_count(plugin);
    const auto stored = fo4::written_hedr_record_count(plugin).value_or(plugin.header.record_count);
    if (stored != expected) {
        sink.add("HEDR_RECORD_COUNT_MISMATCH", Severity::warning, {}, {}, "TES4", "HEDR",
                 "HEDR numRecords is " + std::to_string(stored) +
                     " but the file holds " + std::to_string(expected) +
                     " records and groups; repair_hedr_count rewrites it, and so does a save "
                     "that adds or removes a record");
    }

    const auto masterCount = plugin.header.masters.size();
    std::uint32_t highest = 0;
    bool sawOwn = false;
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        const auto raw = record->header.form_id;
        if (raw.is_created() || raw.is_runtime_light()) continue;
        if (static_cast<std::size_t>(raw.index_byte()) != masterCount) continue;
        sawOwn = true;
        highest = std::max(highest, raw.local_24());
    }
    if (sawOwn && plugin.header.next_form_id <= highest) {
        sink.add("HEDR_NEXT_OBJECT_ID", Severity::error, {}, {}, "TES4", "HEDR",
                 "HEDR nextObjectId 0x" + hexFormId(plugin.header.next_form_id) +
                     " is not above the highest own local FormID 0x" + hexFormId(highest) +
                     ", so the next created record would collide");
    }
}

void checkRecordFormIds(const fo4::Plugin& plugin, FindingSink& sink) {
    const auto masterCount = static_cast<std::uint8_t>(
        std::min<std::size_t>(plugin.header.masters.size(), 0xFDu));
    std::unordered_set<std::uint32_t> seen;

    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        const auto raw = record->header.form_id;
        const auto formKey = record->form_key.to_string();
        const auto recordType = record->header.signature.to_string();
        const auto editorId = editorIdOf(*record);

        // On disk the index byte always goes through the MAST table, so 0xFE and 0xFF are past it too.
        if (raw.index_byte() > masterCount) {
            sink.add("FORM_ID_MASTER_OUT_OF_RANGE", Severity::error, formKey, editorId,
                     recordType, "FORMID",
                     "FormID index byte " + std::to_string(raw.index_byte()) + " is past the " +
                         std::to_string(plugin.header.masters.size()) +
                         " declared masters, so it names a plugin this file does not list");
        }

        if (seen.count(raw.value()) != 0) {
            sink.add("DUPLICATE_FORM_ID", Severity::error, formKey, editorId, recordType,
                     "FORMID",
                     "FormID 0x" + hexFormId(raw.value()) +
                         " appears more than once; only one copy can win");
            continue;
        }
        seen.insert(raw.value());
    }
}

void checkRecordShape(const fo4::Plugin& plugin, const fo4::Record& record, FindingSink& sink) {
    const auto recordType = record.header.signature.to_string();
    const auto formKey = record.form_key.to_string();
    const auto editorId = editorIdOf(record);

    for (const auto& required : requiredSubrecords()) {
        if (required.record != record.header.signature) continue;
        if (fo4::is_partial_form(record)) break;
        if (findSubrecord(record, required.subrecord) != nullptr) continue;
        sink.add("REQUIRED_SUBRECORD_MISSING", Severity::error, formKey, editorId, recordType,
                 required.subrecord.to_string(),
                 recordType + " has no " + required.subrecord.to_string() +
                     " subrecord, which this record type requires");
    }

    if (hasEffectList(record.header.signature)) {
        std::size_t effectCount = 0;
        std::string failure;
        try {
            effectCount = effects::read(plugin, record).size();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        if (!failure.empty()) {
            sink.add("EFFECT_LIST_UNREADABLE", Severity::warning, formKey, editorId, recordType,
                     "EFID", "Effect list could not be decoded: " + failure);
        } else if (effectCount == 0) {
            sink.add("EFFECT_LIST_EMPTY", Severity::warning, formKey, editorId, recordType, "EFID",
                     recordType +
                         " declares no magic effects, so using it has no effect at all");
        }
    }

    if (record.header.signature != fo4::Signature::from_string("QUST")) return;
    const auto* vmad = findSubrecord(record, fo4::Signature::from_string("VMAD"));
    if (vmad == nullptr) return;
    try {
        if (fo4::read_vmad_scripts(vmad->data).scripts.empty()) return;
    } catch (const std::exception&) {
        return;
    }

    bool known = false;
    const bool enabled = isStartGameEnabled(record, known);
    if (!known) {
        if (fo4::is_partial_form(record)) return;
        sink.add("QUEST_START_GAME_UNKNOWN", Severity::warning, formKey, editorId, recordType,
                 "DNAM",
                 "QUST DNAM is missing or shorter than 2 bytes, so Start Game Enabled cannot be read");
        return;
    }
    if (!enabled) {
        sink.add("QUEST_SCRIPT_NOT_START_ENABLED", Severity::info, formKey, editorId, recordType,
                 "DNAM",
                 "QUST carries a VMAD script but Start Game Enabled (DNAM bit 0) is clear, so the "
                 "quest does not start on game load; it runs only once SetStage or Start() starts it");
    }
}

}

std::string_view severityName(Severity severity) {
    switch (severity) {
        case Severity::error: return "error";
        case Severity::warning: return "warning";
        case Severity::info: return "info";
    }
    return "info";
}

bool parseSeverity(std::string_view text, Severity& out) {
    const auto value = detail::asciiLower(text);
    if (value == "error") {
        out = Severity::error;
        return true;
    }
    if (value == "warning") {
        out = Severity::warning;
        return true;
    }
    if (value == "info") {
        out = Severity::info;
        return true;
    }
    return false;
}

nlohmann::json ValidationReport::toJson() const {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& finding : findings) {
        rows.push_back({
            {"code", finding.code},
            {"severity", std::string(severityName(finding.severity))},
            {"formKey", finding.formKey},
            {"editorId", finding.editorId},
            {"recordType", finding.recordType},
            {"field", finding.field},
            {"message", finding.message},
        });
    }
    return nlohmann::json{
        {"errors", errors},
        {"warnings", warnings},
        {"infos", infos},
        {"clean", clean()},
        {"findings", std::move(rows)},
    };
}

ContractParse parseContract(std::string_view textOrPath) {
    ContractParse result;
    std::string text(textOrPath);
    std::error_code ec;
    if (!text.empty() && std::filesystem::exists(std::filesystem::path(text), ec) && !ec) {
        std::ifstream file{std::filesystem::path(text), std::ios::binary};
        if (!file) {
            result.error = "Could not open contract file: " + text;
            return result;
        }
        text.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    }
    if (text.empty()) {
        result.error = "Contract is empty";
        return result;
    }
    try {
        result.value = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        result.error = "Contract is neither an existing file nor valid JSON (parse error at byte " +
                       std::to_string(error.byte) + ")";
        return result;
    }
    if (!result.value.is_object()) {
        result.error = "Contract must be a JSON object";
        return result;
    }
    const auto allowed = [](const nlohmann::json& object, std::initializer_list<std::string_view> keys) {
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) return false;
        }
        return true;
    };
    const auto string = [](const nlohmann::json& object, const char* key, bool required) {
        const auto it = object.find(key);
        if (it == object.end()) return !required;
        return it->is_string() && !it->get_ref<const std::string&>().empty();
    };
    const auto identifiers = [&](const nlohmann::json& entry) {
        return string(entry, "formKey", false) && string(entry, "editorId", false) &&
               (entry.contains("formKey") || entry.contains("editorId"));
    };
    bool valid = allowed(result.value, {"records", "globals", "vmad", "requiredFields"});
    for (auto section = result.value.begin(); valid && section != result.value.end(); ++section) {
        if (!section->is_array()) { valid = false; break; }
        for (const auto& entry : *section) {
            if (!entry.is_object()) { valid = false; break; }
            if (section.key() == "records") {
                valid = allowed(entry, {"formKey", "editorId", "type"}) && identifiers(entry) &&
                        string(entry, "type", false);
            } else if (section.key() == "globals") {
                valid = allowed(entry, {"formKey", "editorId", "type"}) && identifiers(entry) &&
                        string(entry, "type", true);
            } else if (section.key() == "requiredFields") {
                valid = allowed(entry, {"formKey", "editorId", "field"}) && identifiers(entry) &&
                        string(entry, "field", true);
            } else {
                valid = allowed(entry, {"record", "script", "properties"}) &&
                        string(entry, "record", true) && string(entry, "script", true);
                if (valid && entry.contains("properties")) {
                    const auto& properties = entry["properties"];
                    if (!properties.is_array()) valid = false;
                    else for (const auto& property : properties) {
                        if (!property.is_object() || !allowed(property, {"name", "formKey"}) ||
                            !string(property, "name", true) || !string(property, "formKey", false)) {
                            valid = false;
                            break;
                        }
                    }
                }
            }
            if (!valid) break;
        }
    }
    if (!valid) result.error = "Contract has an invalid section or assertion";
    return result;
}

ValidationReport validatePlugin(const ValidationOptions& options) {
    ValidationReport report;
    detail::FindingSink sink(options, report);

    if (options.plugin == nullptr) {
        sink.add("PLUGIN_INPUT_MISSING", Severity::error, {}, {}, {}, {},
                 "No plugin was supplied to the validator");
        return report;
    }

    const auto& plugin = *options.plugin;

    if (options.profile != nullptr) {
        checkMasters(plugin, *options.profile, sink);
    } else {
        sink.add("MASTER_CHECK_SKIPPED", Severity::info, {}, {}, "TES4", "MAST",
                 "No active profile was supplied, so master presence and load order "
                 "were not checked");
    }

    checkHedr(plugin, sink);
    checkRecordFormIds(plugin, sink);
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        checkRecordShape(plugin, *record, sink);
    }

    detail::validateScripts(plugin, options, sink);
    detail::validateContract(plugin, options, sink);

    std::stable_sort(report.findings.begin(), report.findings.end(),
                     [](const ValidationFinding& left, const ValidationFinding& right) {
                         if (left.severity != right.severity) return left.severity > right.severity;
                         if (left.code != right.code) return left.code < right.code;
                         return left.formKey < right.formKey;
                     });
    return report;
}

}
