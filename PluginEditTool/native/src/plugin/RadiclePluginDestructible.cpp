#include "RadiclePluginSupport.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/formid.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/plugin/destructible_stage.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
namespace {

using nlohmann::json;

struct StageField {
    std::string_view key;
    std::string_view suffix;
};

constexpr std::array<StageField, 6> kStageFields{{
    {"healthPercent", "DestructionStageHealthPercent"},
    {"index", "DestructionStageIndex"},
    {"modelDamageStage", "DestructionStageModelDamageStage"},
    {"flags", "DestructionStageFlags"},
    {"selfDamage", "DestructionStageSelfDamagePerSecond"},
    {"debrisCount", "DestructionStageDebrisCount"},
}};

constexpr std::string_view kNoCarrier =
    "the record carries no destructible group, so its destructible rows cannot be addressed";
constexpr std::string_view kMissingRecord = "plugin has no such record";
constexpr std::string_view kMismatchedRecord = "record type cannot carry destructible stages";

struct Outcome {
    std::string status;
    bool changed{false};
};

std::unexpected<Error> failure(std::string type, std::string message) {
    return std::unexpected(Error{std::move(type), std::move(message)});
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

std::string hexUpper(std::span<const std::byte> bytes) {
    static constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned>(byte);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0Fu]);
    }
    return out;
}

json refusalGroup(std::string_view status, std::string_view text) {
    return json{{"status", std::string(status)}, {"error", std::string(text)}, {"header", nullptr},
                {"resistances", nullptr}, {"stages", nullptr}};
}

std::string carrierSignature(const fo4::Record& record) {
    return record.header.signature.to_string();
}

bool isCarrier(const fo4::Record& record) {
    const auto carrier = carrierSignature(record);
    return fo4::schema::scalar_field_descriptor(
               record.header.signature, carrier + "\\DestructionStageHealthPercent")
        .has_value();
}

std::string scalarText(const fo4::Plugin& plugin, const fo4::Record& record,
                       std::string_view suffix, std::size_t index) {
    const auto path = carrierSignature(record) + "\\" + std::string(suffix);
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field) return {};
    return fo4::record_scalar_text(plugin, record, *field, index).value_or(std::string{});
}

// Constraint: member payloads are spans into plugin storage, so a group must be rendered before any later edit of the plugin.
Result<json> groupJson(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    fo4::DestructibleGroupView view;
    switch (fo4::read_destructible_group(plugin, key, view)) {
        case fo4::DestructibleGroupReadStatus::present: break;
        case fo4::DestructibleGroupReadStatus::record_missing:
            return failure("RecordMissing", std::string(kMissingRecord));
        case fo4::DestructibleGroupReadStatus::record_mismatch:
            return failure("RecordMismatch", std::string(kMismatchedRecord));
        case fo4::DestructibleGroupReadStatus::header_missing:
            return refusalGroup("HeaderMissing",
                                "The record has no DEST header, so it has no destructible group.");
        case fo4::DestructibleGroupReadStatus::interleaved_stages:
            return refusalGroup("Interleaved",
                                "A destructible stage member sits outside every DSTD..DSTF run, so the group cannot be read.");
        case fo4::DestructibleGroupReadStatus::malformed_stages:
            return refusalGroup("Malformed",
                                "The DEST stage count and the DSTD runs disagree, so the group cannot be read.");
    }
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return failure("RecordMissing", std::string(kMissingRecord));

    json resistances = json::array();
    for (const auto& row : view.resistances) {
        resistances.push_back({{"damageType", row.damage_type.to_string()}, {"value", row.value}});
    }
    json stages = json::array();
    for (std::size_t stage = 0; stage < view.stages.size(); ++stage) {
        json members = json::array();
        for (const auto& member : view.stages[stage].members) {
            members.push_back({{"signature", member.signature.to_string()},
                               {"size", member.data.size()},
                               {"hex", hexUpper(member.data)}});
        }
        json values = json::object();
        for (const auto& field : kStageFields) {
            values[std::string(field.key)] = scalarText(plugin, *record, field.suffix, stage);
        }
        stages.push_back({{"members", std::move(members)}, {"values", std::move(values)}});
    }
    return json{{"status", "Present"},
                {"error", nullptr},
                {"header", {{"health", view.health}, {"stageCount", view.stage_count}, {"flags", view.flags}}},
                {"resistances", std::move(resistances)},
                {"stages", std::move(stages)}};
}

Result<json> snapshotObject(const fo4::Plugin& plugin, std::string_view name,
                            const std::filesystem::path& path, bool dirty,
                            const fo4::FormKey* key, std::string_view status) {
    json out{{"plugin", std::string(name)}, {"path", fo4::io::utf8(path)}, {"dirty", dirty}};
    if (!status.empty()) out["status"] = std::string(status);
    if (key != nullptr) {
        auto group = groupJson(plugin, *key);
        if (!group) return std::unexpected(group.error());
        out["group"] = std::move(*group);
    }
    return out;
}

json dumped(const json& value) {
    return json(value.dump(-1, ' ', false, json::error_handler_t::replace));
}

Result<fo4::FormKey> parseRecordKey(const std::string& text) {
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return failure("InvalidFormKey", error.what());
    }
}

Result<bool> mapHeader(fo4::DestructibleHeaderStatus status, std::string& name) {
    using Status = fo4::DestructibleHeaderStatus;
    switch (status) {
        case Status::updated: name = "Updated"; return true;
        case Status::header_present: name = "Present"; return false;
        case Status::header_missing: name = "Missing"; return false;
        case Status::unknown_order: name = "UnknownOrder"; return false;
        case Status::too_many_stages: name = "TooManyStages"; return false;
        case Status::record_missing: return failure("RecordMissing", std::string(kMissingRecord));
        case Status::record_mismatch: return failure("RecordMismatch", std::string(kMismatchedRecord));
    }
    return failure("DestructibleFailed", "unexpected destructible header status");
}

Result<bool> mapStage(fo4::DestructibleStageMutationStatus status, std::string& name) {
    using Status = fo4::DestructibleStageMutationStatus;
    switch (status) {
        case Status::updated: name = "Updated"; return true;
        case Status::header_missing: name = "HeaderMissing"; return false;
        case Status::index_out_of_range: name = "IndexOutOfRange"; return false;
        case Status::too_many_stages: name = "TooManyStages"; return false;
        case Status::interleaved_stages: name = "Interleaved"; return false;
        case Status::malformed_stages: name = "Malformed"; return false;
        case Status::record_missing: return failure("RecordMissing", std::string(kMissingRecord));
        case Status::record_mismatch: return failure("RecordMismatch", std::string(kMismatchedRecord));
    }
    return failure("DestructibleFailed", "unexpected destructible stage status");
}

Result<bool> mapMember(fo4::DestructibleStageMemberStatus status, std::string& name) {
    using Status = fo4::DestructibleStageMemberStatus;
    switch (status) {
        case Status::updated: name = "Updated"; return true;
        case Status::header_missing: name = "HeaderMissing"; return false;
        case Status::stage_index_out_of_range: name = "StageIndexOutOfRange"; return false;
        case Status::not_optional_member: name = "NotOptional"; return false;
        case Status::no_default: name = "NoDefault"; return false;
        case Status::member_present: name = "Present"; return false;
        case Status::member_absent: name = "Absent"; return false;
        case Status::model_required: name = "ModelRequired"; return false;
        case Status::interleaved_stages: name = "Interleaved"; return false;
        case Status::malformed_stages: name = "Malformed"; return false;
        case Status::record_missing: return failure("RecordMissing", std::string(kMissingRecord));
        case Status::record_mismatch: return failure("RecordMismatch", std::string(kMismatchedRecord));
    }
    return failure("DestructibleFailed", "unexpected destructible stage member status");
}

Result<fo4::Signature> memberSignature(const json& arguments, std::size_t index) {
    const auto text = support::stringArgument(arguments, index);
    if (text.size() != 4) {
        return failure("InvalidMember", "member must be a four-character subrecord code");
    }
    for (const char ch : text) {
        if (ch < 0x20 || ch > 0x7E) {
            return failure("InvalidMember", "member must be a four-character subrecord code");
        }
    }
    return fo4::Signature::from_string(text);
}

// Constraint: a destructible row is addressed as "<record signature>\<row>", so a record that is not a destructible carrier must be refused before any write.
Result<bool> writeScalar(fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view suffix,
                         std::size_t index, const std::string& value) {
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return failure("RecordMissing", std::string(kMissingRecord));
    if (!isCarrier(*record)) return failure("NotDestructible", std::string(kNoCarrier));
    const auto field = fo4::schema::scalar_field_descriptor(
        carrierSignature(*record) + "\\" + std::string(suffix));
    if (!field) return failure("NotDestructible", std::string(kNoCarrier));
    if (fo4::record_scalar_text(plugin, *record, *field, index).value_or(std::string{}) == value) {
        return false;
    }
    switch (fo4::set_record_scalar(plugin, key, *field, index, value)) {
        case fo4::ScalarMutationStatus::updated: return true;
        case fo4::ScalarMutationStatus::unchanged: return false;
        case fo4::ScalarMutationStatus::record_missing:
            return failure("RecordMissing", std::string(kMissingRecord));
        case fo4::ScalarMutationStatus::subrecord_missing:
            return failure("FieldMissing", "the record does not carry " + std::string(suffix));
        case fo4::ScalarMutationStatus::next_alias_id_too_low:
            return failure("FieldWriteFailed", "the destructible row refused the value");
    }
    return failure("FieldWriteFailed", "unexpected scalar mutation status");
}

// Constraint: a link is addressed as "<form ID>:<plugin>", and a bare form ID must name the opened plugin, and a null link must be refused because the damage type row is not nullable.
Result<fo4::FormKey> parseLink(std::string_view text, std::string_view pluginName) {
    const auto whole = trimmed(text);
    if (whole.empty()) return failure("InvalidFormLinkValue", "a damage type link is required");
    const auto colon = whole.find(':');
    auto owner = colon == std::string::npos ? std::string(pluginName) : trimmed(whole.substr(colon + 1));
    if (owner.empty()) owner = std::string(pluginName);
    auto digits = trimmed(whole.substr(0, colon));
    const auto first = digits.find_first_not_of("0xX");
    digits = first == std::string::npos ? std::string{} : digits.substr(first);
    std::uint32_t local = 0;
    const auto* end = digits.data() + digits.size();
    const auto [stop, error] = std::from_chars(digits.data(), end, local, 16);
    if (digits.empty() || digits.size() > 8 || error != std::errc{} || stop != end) {
        return failure("InvalidFormLinkValue", "not a form ID: " + std::string(text));
    }
    return fo4::FormKey{fo4::PluginKey::from_filename(owner), fo4::LocalFormId{local}};
}

Result<Outcome> setDamageType(fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view pluginName,
                              std::size_t index, std::string_view text) {
    const auto target = parseLink(text, pluginName);
    if (!target) return std::unexpected(target.error());
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return failure("RecordMissing", std::string(kMissingRecord));
    if (!isCarrier(*record)) return failure("NotDestructible", std::string(kNoCarrier));
    const auto field = fo4::schema::formlink_field_descriptor(
        carrierSignature(*record) + "\\DestructionDamageType");
    if (!field) return failure("NotDestructible", std::string(kNoCarrier));
    {
        fo4::DestructibleGroupView view;
        if (fo4::read_destructible_group(plugin, key, view) != fo4::DestructibleGroupReadStatus::present ||
            index >= view.resistances.size()) {
            return failure("IndexOutOfRange", "the record carries no damage-type link at that resistance");
        }
        if (view.resistances[index].damage_type == *target) return Outcome{"Unchanged", false};
    }
    switch (fo4::set_record_formlink_field(plugin, key, *field, index, *target)) {
        case fo4::FormLinkFieldMutationStatus::updated: return Outcome{"Updated", true};
        case fo4::FormLinkFieldMutationStatus::unchanged: return Outcome{"Unchanged", false};
        case fo4::FormLinkFieldMutationStatus::record_missing:
        case fo4::FormLinkFieldMutationStatus::record_mismatch:
        case fo4::FormLinkFieldMutationStatus::subrecord_missing:
            return failure("FieldMissing", "the record does not carry a damage-type link on that resistance");
    }
    return failure("FieldWriteFailed", "unexpected form link mutation status");
}

Result<Outcome> setHeader(fo4::Plugin& plugin, const fo4::FormKey& key, const json& arguments) {
    const auto health = support::integerArgument<std::int32_t>(arguments, 3);
    const auto flags = support::integerArgument<std::int32_t>(arguments, 4);
    if (!health) return failure("InvalidHealth", "the DEST health must be a whole number");
    if (!flags || *flags < 0 || *flags > 255) {
        return failure("InvalidFlags", "the DEST flags byte is a u8");
    }
    const auto healthWrite = writeScalar(plugin, key, "DestructionHealth", 0, std::to_string(*health));
    if (!healthWrite) return std::unexpected(healthWrite.error());
    const auto flagsWrite = writeScalar(plugin, key, "DestructionFlags", 0, std::to_string(*flags));
    if (!flagsWrite) return std::unexpected(flagsWrite.error());
    return Outcome{*healthWrite || *flagsWrite ? "Updated" : "Unchanged", *healthWrite || *flagsWrite};
}

Result<Outcome> applyAction(fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view pluginName,
                            std::string_view action, const json& arguments) {
    Outcome outcome;
    const auto finish = [&outcome](Result<bool> changed) -> Result<Outcome> {
        if (!changed) return std::unexpected(changed.error());
        outcome.changed = *changed;
        return outcome;
    };
    const auto index = support::integerArgument<std::size_t>(arguments, 3);

    if (action == "insertHeader") return finish(mapHeader(fo4::insert_destructible_header(plugin, key), outcome.status));
    if (action == "removeHeader") return finish(mapHeader(fo4::remove_destructible_header(plugin, key), outcome.status));
    if (action == "setHeader") return setHeader(plugin, key, arguments);

    if (!index) return failure("InvalidIndex", "the index must be a non-negative whole number");
    if (action == "insertStage") return finish(mapStage(fo4::insert_destructible_stage(plugin, key, *index), outcome.status));
    if (action == "removeStage") return finish(mapStage(fo4::remove_destructible_stage(plugin, key, *index), outcome.status));
    if (action == "insertMember" || action == "removeMember") {
        const auto member = memberSignature(arguments, 4);
        if (!member) return std::unexpected(member.error());
        return finish(mapMember(action == "insertMember"
                                    ? fo4::insert_destructible_stage_member(plugin, key, *index, *member)
                                    : fo4::remove_destructible_stage_member(plugin, key, *index, *member),
                                outcome.status));
    }
    if (action == "setStageValue") {
        const auto fieldKey = support::stringArgument(arguments, 4);
        for (const auto& field : kStageFields) {
            if (field.key != fieldKey) continue;
            const auto written = writeScalar(plugin, key, field.suffix, *index, support::stringArgument(arguments, 5));
            if (!written) return std::unexpected(written.error());
            return Outcome{*written ? "Updated" : "Unchanged", *written};
        }
        return failure("UnknownField", "unknown destructible stage field: " + fieldKey);
    }
    if (action == "setResistanceValue") {
        const auto written = writeScalar(plugin, key, "DestructionResistanceValue", *index,
                                         support::stringArgument(arguments, 4));
        if (!written) return std::unexpected(written.error());
        return Outcome{*written ? "Updated" : "Unchanged", *written};
    }
    if (action == "setResistanceDamageType") {
        return setDamageType(plugin, key, pluginName, *index, support::stringArgument(arguments, 4));
    }
    return failure("UnknownAction", "unknown destructible action: " + std::string(action));
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::destructibleRead(const nlohmann::json& arguments) const {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return failure("PluginMissing", "Plugin name is empty");
    const auto keyText = support::stringArgument(arguments, 1);

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) return failure("PluginNotLoaded", "Plugin is not loaded: " + name);
    try {
        fo4::FormKey key;
        if (!keyText.empty()) {
            const auto parsed = parseRecordKey(keyText);
            if (!parsed) return std::unexpected(parsed.error());
            key = *parsed;
        }
        const auto snapshot = snapshotObject(loaded->plugin, loaded->name, loaded->path, loaded->dirty,
                                             keyText.empty() ? nullptr : &key, {});
        if (!snapshot) return std::unexpected(snapshot.error());
        return dumped(*snapshot);
    } catch (const std::exception& error) {
        return failure("DestructibleReadFailed", error.what());
    }
}

Result<nlohmann::json> RadiclePluginBackend::destructibleEdit(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    const auto action = support::stringArgument(arguments, 2);
    if (name.empty()) return failure("PluginMissing", "Plugin name is empty");
    const auto key = parseRecordKey(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) return failure("PluginNotLoaded", "Plugin is not loaded: " + name);
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    try {
        auto mutated = loaded->plugin;
        const auto outcome = applyAction(mutated, *key, loaded->name, action, arguments);
        if (!outcome) return std::unexpected(outcome.error());
        if (outcome->changed) stagePluginMutationLocked(*loaded, std::move(mutated));
        const auto snapshot = snapshotObject(loaded->plugin, loaded->name, loaded->path, loaded->dirty,
                                             &*key, outcome->status);
        if (!snapshot) return std::unexpected(snapshot.error());
        return dumped(*snapshot);
    } catch (const std::exception& error) {
        return failure("DestructibleEditFailed", error.what());
    }
}

Result<nlohmann::json> RadiclePluginBackend::destructibleCreate(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    const auto signatureText = trimmed(support::stringArgument(arguments, 1));
    const auto editorText = trimmed(support::stringArgument(arguments, 2));
    if (name.empty()) return failure("PluginMissing", "Plugin name is empty");
    if (signatureText.size() != 4) {
        return failure("InvalidRecordSignature", "signature must be four printable ASCII characters");
    }
    for (const char ch : signatureText) {
        if (ch < 0x20 || ch > 0x7E) {
            return failure("InvalidRecordSignature", "signature must be four printable ASCII characters");
        }
    }
    if (editorText.empty()) return failure("InvalidEditorId", "an editor ID is required");
    const auto editorId = support::encodeEditorId(editorText);
    if (!editorId) return std::unexpected(editorId.error());

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) return failure("PluginNotLoaded", "Plugin is not loaded: " + name);
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    try {
        auto mutated = loaded->plugin;
        const auto created = fo4::create_record(mutated, fo4::Signature::from_string(signatureText), *editorId);
        switch (created.status) {
            case fo4::RecordCreateStatus::created: break;
            case fo4::RecordCreateStatus::not_authorable:
                return failure("RecordNotAuthorable", "record type has no authoring schema");
            case fo4::RecordCreateStatus::no_default:
                return failure("RecordNoDefault", "a required subrecord has no registered default payload");
            case fo4::RecordCreateStatus::invalid_editor_id:
                return failure("InvalidEditorId", "editor ID cannot be stored for this record type");
            case fo4::RecordCreateStatus::preflight_failed:
                return failure("RecordPreflightFailed", "the default record failed authoring preflight");
        }
        if (fo4::insert_destructible_header(mutated, created.key) != fo4::DestructibleHeaderStatus::updated) {
            return failure("DestructibleHeaderRefused",
                           "the new " + signatureText + " record did not accept a DEST header");
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        auto snapshot = snapshotObject(loaded->plugin, loaded->name, loaded->path, loaded->dirty,
                                       &created.key, "Updated");
        if (!snapshot) return std::unexpected(snapshot.error());
        (*snapshot)["formId"] = support::localHex(created.key.local().value());
        return dumped(*snapshot);
    } catch (const std::exception& error) {
        return failure("DestructibleCreateFailed", error.what());
    }
}

}  // namespace fo4ide::plugin
