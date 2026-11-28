#include "RadiclePluginArmor.hpp"
#include "RadiclePluginKeywordCreation.hpp"
#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/environment/package_procedure.hpp>
#include <fo4/forms/record_flags.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/schema/preflight.hpp>
#include <fo4/schema/record_constraints.hpp>
#include <fo4/schema/scalar.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <expected>
#include <span>
#include <string>
#include <vector>
namespace fo4ide::plugin {
using namespace lifecycle;
namespace {
Result<fo4::FormKey> createRichSchemaRecordLocked(fo4::Plugin& mutated,
                                                  std::string_view signature,
                                                  const nlohmann::json& payload,
                                                  std::span<const fo4::LocalizedRowId> localized_ids) {
    const auto* editor = editorIdPayloadField(payload);
    if (editor == nullptr || !editor->is_string() ||
        editor->get_ref<const std::string&>().empty()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            std::string(signature) + " creation requires a non-empty string editorId"});
    }
    const auto editorId = support::encodeEditorId(editor->get_ref<const std::string&>());
    if (!editorId) return std::unexpected(editorId.error());

    const auto record = fo4::Signature::from_string(signature);
    std::vector<fo4::schema::SubrecordAuthoringConstraint> keywordList;
    bool keywordExplicit = false;
    keyword_creation::AuthoringRules keywordRules;
    const auto keywordRulesResult = keyword_creation::apply(signature, payload, keywordRules, keywordList, keywordExplicit);
    if (!keywordRulesResult) return std::unexpected(keywordRulesResult.error());
    std::vector<std::uint8_t> ruleStorage;
    auto explicitRules = explicitCreationRules(signature, payload, mutated.header.tes4_flags.is_localized(), ruleStorage);
    if (!explicitRules) return std::unexpected(explicitRules.error());
    if (keywordExplicit) *explicitRules = std::move(keywordList);

    const auto rules = *explicitRules
        ? std::span<const fo4::schema::SubrecordAuthoringConstraint>(**explicitRules)
        : std::span<const fo4::schema::SubrecordAuthoringConstraint>{};
    const auto created = !localized_ids.empty()
        ? fo4::create_record_with_localized_rows(mutated, record, *editorId, localized_ids, rules)
        : *explicitRules
            ? fo4::create_record(mutated, record, *editorId, rules)
            : fo4::create_record(mutated, record, *editorId);
    switch (created.status) {
        case fo4::RecordCreateStatus::created:
            break;
        case fo4::RecordCreateStatus::not_authorable:
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                std::string(signature) + " has no independent schema creation rule"});
        case fo4::RecordCreateStatus::no_default:
            return std::unexpected(Error{
                "RecordNoDefault",
                std::string(signature) + " requires a subrecord with no usable default"});
        case fo4::RecordCreateStatus::invalid_editor_id:
            return std::unexpected(Error{"InvalidEditorId", "Editor ID is not valid for this record"});
        case fo4::RecordCreateStatus::preflight_failed:
            return std::unexpected(Error{"RecordPreflightFailed", "Generated record failed schema validation"});
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(mutated, created.key)) {
        if (issue.severity != fo4::schema::PreflightSeverity::error) continue;
        return std::unexpected(Error{
            "RecordPreflightFailed",
            issue.path.empty() ? issue.message : issue.path + ": " + issue.message});
    }
    return created.key;
}

Result<fo4::FormKey> createRaceLocked(fo4::Plugin& mutated, const nlohmann::json& payload) {
    const auto description = payload.find("description");
    if (description == payload.end() || !description->is_string() ||
        !payload.contains("maleHeight") || !payload.contains("femaleHeight")) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "RACE creation requires description, maleHeight, and femaleHeight"});
    }
    bool valid = false;
    const auto editorId = optionalEditorIdField(payload, valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "RACE editorId must be a string or null"});
    const auto fullName = optionalStringField(payload, "fullName", valid);
    if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "RACE fullName must be a string or null"});

    float maleHeight = 0.0f;
    float femaleHeight = 0.0f;
    float accelerationRate = 0.0f;
    float decelerationRate = 0.0f;
    float injuredHealthPct = 0.0f;
    std::uint32_t flags = 0;
    std::uint32_t flags2 = 0;
    std::uint32_t size = 0;
    if (!lifecycleFloatField(payload, "maleHeight", 0.0f, maleHeight) ||
        !lifecycleFloatField(payload, "femaleHeight", 0.0f, femaleHeight) ||
        !lifecycleUnsignedField(payload, "flags", std::uint32_t{0}, flags) ||
        !lifecycleUnsignedField(payload, "flags2", std::uint32_t{0}, flags2) ||
        !lifecycleUnsignedField(payload, "size", std::uint32_t{0}, size) ||
        !lifecycleFloatField(payload, "accelerationRate", 0.0f, accelerationRate) ||
        !lifecycleFloatField(payload, "decelerationRate", 0.0f, decelerationRate) ||
        !lifecycleFloatField(payload, "injuredHealthPct", 0.0f, injuredHealthPct)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "RACE numeric fields must fit their current form-131 storage"});
    }

    const auto fail = [](std::string message) {
        return std::unexpected(Error{"RaceCreateFailed", std::move(message)});
    };
    // Constraint: the generic DATA default and the rows written below are the form-version 131 layout.
    if (mutated.header.form_version != 131) {
        return fail("RACE creation currently supports Fallout 4 form version 131 only");
    }
    const auto& descriptionText = description->get_ref<const std::string&>();
    if (descriptionText.find('\0') != std::string::npos ||
        (fullName.has_value() && fullName->find('\0') != std::string::npos) ||
        (editorId.has_value() && editorId->find('\0') != std::string::npos)) {
        return fail("RACE string fields cannot contain embedded NUL bytes");
    }
    const auto storedEditorId = editorId ? fo4::stored_editor_id(*editorId) : std::string{};
    const auto storedFull = fullName ? std::optional<std::string>{fo4::stored_inline_text(*fullName, "RACE FULL")} : std::nullopt;
    const auto storedDescription = fo4::stored_inline_text(descriptionText, "RACE DESC");

    const auto race = fo4::Signature::from_string("RACE");
    const auto constraint = fo4::schema::record_authoring_constraint(race);
    if (!constraint) return fail("RACE has no authoring rules in the field schema");
    const auto all = fo4::schema::authoring_subrecords(*constraint);
    const auto body = std::find_if(all.begin(), all.end(), [](const fo4::schema::SubrecordAuthoringConstraint& rule) {
        return rule.signature == fo4::Signature::from_string("NAM1");
    });
    if (body == all.end()) return fail("RACE authoring rules have no BodyData marker");
    // Constraint: the create stops before NAM1, because the marker run NAM1 MNAM FNAM NAM3 MNAM FNAM repeats MNAM and FNAM and one rule list cannot name a signature twice.
    const auto rules = all.first(static_cast<std::size_t>(body - all.begin()));

    const auto created = fo4::create_record(mutated, race, storedEditorId, rules);
    switch (created.status) {
        case fo4::RecordCreateStatus::created: break;
        case fo4::RecordCreateStatus::no_default:
            return fail(
                "RACE creation in localized plugins is disabled until required DESC string-table authoring is implemented");
        case fo4::RecordCreateStatus::invalid_editor_id:
            return fail("Editor ID is not valid for this record");
        case fo4::RecordCreateStatus::not_authorable:
        case fo4::RecordCreateStatus::preflight_failed:
            return fail("Generated RACE record failed schema validation");
    }
    const auto key = created.key;

    if (storedFull && !storedFull->empty()) {
        fo4::schema::LocalizedString name;
        name.text = *storedFull;
        const auto named = fo4::set_record_full_name(mutated, key, name);
        if (named != fo4::RecordNameStatus::updated && named != fo4::RecordNameStatus::unchanged) {
            return fail("The RACE name cannot be stored as a single FULL string");
        }
    }
    const auto written = [](fo4::ScalarMutationStatus status) {
        return status == fo4::ScalarMutationStatus::updated || status == fo4::ScalarMutationStatus::unchanged;
    };
    const auto descriptionField = fo4::schema::scalar_field_descriptor("RACE\\Description");
    if (!descriptionField) return fail("RACE Description row is missing from the field schema");
    if (!written(fo4::set_record_string(mutated, key, *descriptionField, 0, storedDescription))) {
        return fail("RACE is missing required DESC subrecord");
    }
    const std::pair<const char*, std::string> rows[] = {
        {"RACE\\MaleHeight", floatRowText(maleHeight)},
        {"RACE\\FemaleHeight", floatRowText(femaleHeight)},
        {"RACE\\Flags", std::to_string(flags)},
        {"RACE\\Flags2", std::to_string(flags2)},
        {"RACE\\Size", std::to_string(size)},
        {"RACE\\AccelerationRate", floatRowText(accelerationRate)},
        {"RACE\\DecelerationRate", floatRowText(decelerationRate)},
        {"RACE\\InjuredHealthPercent", floatRowText(injuredHealthPct)},
    };
    for (const auto& [path, text] : rows) {
        const auto field = fo4::schema::scalar_field_descriptor(path);
        if (!field) return fail(std::string("RACE row is missing from the field schema: ") + path);
        if (!written(fo4::set_record_scalar(mutated, key, *field, 0, text))) {
            return fail(std::string("RACE is missing required DATA subrecord for ") + path);
        }
    }

    // Constraint: BodyData and both behavior-graph containers are required by xEdit; the empty markers keep a deterministic order.
    const std::pair<const char*, std::size_t> markers[] = {
        {"NAM1", 0}, {"MNAM", 0}, {"FNAM", 0}, {"NAM3", 0}, {"MNAM", 1}, {"FNAM", 1}};
    for (const auto& [marker, occurrence] : markers) {
        if (fo4::set_record_subrecord_bytes(mutated, key, fo4::Signature::from_string(marker), occurrence, {}) !=
            fo4::SubrecordBytesStatus::appended) {
            return fail(std::string("RACE could not accept the ") + marker + " marker");
        }
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(mutated, key)) {
        if (issue.severity != fo4::schema::PreflightSeverity::error) continue;
        return fail("RACE authoring preflight failed: " + issue.message);
    }
    return key;
}

Result<std::string> qustLanguage(const nlohmann::json& payload) {
    std::string language = "en";
    if (const auto found = payload.find("language"); found != payload.end()) {
        if (!found->is_string()) {
            return std::unexpected(Error{"InvalidRecordCreationPayload", "QUST language must be a string"});
        }
        language = found->get<std::string>();
    }
    const bool valid = !language.empty() && std::all_of(language.begin(), language.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0;
    });
    if (!valid) return std::unexpected(Error{"InvalidStringLanguage", "QUST language must be an alphanumeric token"});
    return fo4::text::fold(language);
}

struct TravelLocationName {
    std::string_view name;
    fo4::env::PackageLocationType type;
};

constexpr TravelLocationName travelLocationNames[] = {
    {"reference", fo4::env::PackageLocationType::near_reference},
    {"cell", fo4::env::PackageLocationType::in_cell},
    {"near_package_start", fo4::env::PackageLocationType::near_package_start},
    {"near_editor_location", fo4::env::PackageLocationType::near_editor_location},
    {"near_self", fo4::env::PackageLocationType::near_self},
};

struct TravelSpeedName {
    std::string_view name;
    std::uint8_t speed;
};

constexpr TravelSpeedName travelSpeedNames[] = {
    {"walk", 0},
    {"jog", 1},
    {"run", 2},
    {"fast_walk", 3},
};

Error travelStatusError(fo4::env::TravelPackageStatus status) {
    return Error{
        status == fo4::env::TravelPackageStatus::invalid_editor_id ? "InvalidEditorId" : "InvalidRecordCreationPayload",
        fo4::env::travel_package_status_message(status)};
}

bool lifecycleBoolField(const nlohmann::json& object, const char* key, bool& value) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = false;
        return true;
    }
    if (!found->is_boolean()) return false;
    value = found->get<bool>();
    return true;
}

Result<fo4::env::TravelPackageOptions> travelPackageOptionsFromPayload(const nlohmann::json& payload) {
    const auto location = payload.find("location");
    if (location == payload.end() || !location->is_object()) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", "PACK creation requires a location object"});
    }
    fo4::env::TravelPackageOptions options;
    bool valid = true;
    const auto typeName = optionalStringField(*location, "type", valid);
    bool known = false;
    for (const auto& entry : travelLocationNames) {
        if (typeName && *typeName == entry.name) {
            options.location_type = entry.type;
            known = true;
        }
    }
    if (!valid || !known) return std::unexpected(travelStatusError(fo4::env::TravelPackageStatus::location_unsupported));

    const auto form = optionalStringField(*location, "form", valid);
    if (!valid) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", "PACK location form must be a FormKey string or null"});
    }
    if (form.has_value()) {
        try {
            options.location_form = fo4::FormKey::parse(*form);
        } catch (const std::exception& error) {
            return std::unexpected(Error{"InvalidFormKey", error.what()});
        }
    }
    if (!lifecycleSignedField(*location, "radius", std::int32_t{0}, options.radius)) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", "PACK location radius must fit a signed 32-bit integer"});
    }

    const auto speed = optionalStringField(payload, "speed", valid);
    if (!valid) return std::unexpected(travelStatusError(fo4::env::TravelPackageStatus::speed_invalid));
    if (speed.has_value()) {
        bool found = false;
        for (const auto& entry : travelSpeedNames) {
            if (*speed == entry.name) {
                options.preferred_speed = entry.speed;
                found = true;
            }
        }
        if (!found) return std::unexpected(travelStatusError(fo4::env::TravelPackageStatus::speed_invalid));
    }

    if (!lifecycleBoolField(payload, "preferPreferredPath", options.prefer_preferred_path) ||
        !lifecycleBoolField(payload, "useSoftRadius", options.use_soft_radius) ||
        !lifecycleBoolField(payload, "ignoreMarkerOrientation", options.ignore_marker_orientation)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PACK preferPreferredPath, useSoftRadius and ignoreMarkerOrientation must be booleans"});
    }
    return options;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::createRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto signatureText = lifecycleStringArgument(arguments, 1);
    const auto* payload = lifecycleObjectArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (signatureText.size() != 4) {
        return std::unexpected(Error{
            "InvalidRecordSignature",
            "Record signature must contain exactly four characters"
        });
    }
    const bool schemaCreation = payload != nullptr && payload->contains("schema") &&
        payload->at("schema").is_boolean() && payload->at("schema").get<bool>();
    if (!schemaCreation && !generatedLayoutCreationSupported(signatureText) &&
        signatureText != "ARMA" && signatureText != "ARMO" &&
        signatureText != "CELL" && signatureText != "EXPL" &&
        signatureText != "EYES" && signatureText != "FACT" &&
        signatureText != "HAZD" && signatureText != "LVLI" &&
        signatureText != "LVLN" && signatureText != "LVSP" &&
        signatureText != "PACK" && signatureText != "PERK" && signatureText != "PROJ" && signatureText != "RACE") {
        return std::unexpected(Error{
            "RecordCreationRequiresSchema",
            "Raw record creation is disabled until the schema pre-flight layer defines required fields and insertion order for " +
                signatureText
        });
    }
    if (payload == nullptr) {
        return std::unexpected(Error{
            "RecordCreationPayloadMissing",
            "Typed " + signatureText + " creation requires an object payload"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    // Light plugins are allowed: the small-file FormID range is proven from
    // TESFile::AdjustFormIDFileIndex (see Radicle/docs/ESL_FORMIDS.md, #26) and
    // fo4::create_local_record enforces the 0x001..0xFFF bounds itself.

    try {
        auto mutated = loaded->plugin;
        fo4::FormKey key;
        LocalizedStringAllocation strings;
        fo4::LocalizedRowId localizedNameId;
        std::span<const fo4::LocalizedRowId> localizedIds;

        if (schemaCreation) {
            const auto* editor = editorIdPayloadField(*payload);
            if (editor != nullptr && !editor->is_string()) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", "Schema editorId must be a string"});
            }
            const auto requested = editor == nullptr ? std::string{} : editor->get<std::string>();
            const auto editorId = support::encodeEditorId(requested);
            if (!editorId) return std::unexpected(editorId.error());
            const auto created = fo4::create_record(mutated, fo4::Signature::from_string(signatureText), *editorId);
            switch (created.status) {
                case fo4::RecordCreateStatus::created: key = created.key; break;
                case fo4::RecordCreateStatus::not_authorable:
                    return std::unexpected(Error{"RecordNotAuthorable", "Record has no independent schema creation rule"});
                case fo4::RecordCreateStatus::no_default:
                    return std::unexpected(Error{"RecordNoDefault", "Required subrecords have no usable default"});
                case fo4::RecordCreateStatus::invalid_editor_id:
                    return std::unexpected(Error{"InvalidEditorId", "Editor ID is not valid for this record"});
                case fo4::RecordCreateStatus::preflight_failed:
                    return std::unexpected(Error{"RecordPreflightFailed", "Generated record failed schema validation"});
            }
        } else if (signatureText == "CELL") {
            const auto worldspace = payload->find("worldspace");
            if (worldspace == payload->end() || !worldspace->is_string() ||
                !payload->contains("x") || !payload->contains("y")) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL creation requires worldspace FormKey, x, and y"
                });
            }

            fo4::forms::ExteriorCellCreation creation;
            try {
                creation.worldspace =
                    fo4::FormKey::parse(worldspace->get_ref<const std::string&>());
            } catch (const std::exception& error) {
                return std::unexpected(Error{"InvalidWorldspaceFormKey", error.what()});
            }
            if (!lifecycleSignedField(*payload, "x", std::int32_t{0}, creation.x) ||
                !lifecycleSignedField(*payload, "y", std::int32_t{0}, creation.y)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL x and y must fit signed 32-bit storage"
                });
            }
            bool editorValid = false;
            const auto editorId = optionalEditorIdField(*payload, editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL editorId must be a string or null"
                });
            }
            creation.editor_id = optionalStringView(editorId);
            key = fo4::forms::create_exterior_cell(mutated, creation);
        } else if (signatureText == "EYES") {
            const auto full = payload->find("fullText");
            const auto icon = payload->find("icon");
            if (full == payload->end() || !full->is_string() ||
                icon == payload->end() || !icon->is_string()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES creation requires string fields fullText and icon"
                });
            }

            bool editorValid = false;
            const auto editorId = optionalEditorIdField(*payload, editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES editorId must be a string or null"
                });
            }

            std::uint8_t dataFlags = 0;
            std::uint32_t recordFlags = 0;
            if (!lifecycleUnsignedField(*payload, "dataFlags", std::uint8_t{0}, dataFlags) ||
                !lifecycleUnsignedField(*payload, "recordFlags", std::uint32_t{0}, recordFlags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES dataFlags must be 0..255 and recordFlags must be an unsigned 32-bit value"
                });
            }

            const fo4::RecordFlags flags{recordFlags};
            if (flags.is_master() || flags.is_deleted() || flags.is_compressed() || flags.is_small_file()) {
                return std::unexpected(Error{
                    "EyesCreateFailed",
                    "new EYES records cannot be created with master, deleted, compressed, or small-file flags"
                });
            }
            const auto& fullText = full->get_ref<const std::string&>();
            const auto& iconText = icon->get_ref<const std::string&>();
            if (fullText.find('\0') != std::string::npos || iconText.find('\0') != std::string::npos) {
                return std::unexpected(Error{
                    "EyesCreateFailed",
                    "EYES string fields cannot contain embedded NUL bytes"
                });
            }
            const auto editorView = optionalStringView(editorId);
            const auto storedEditorId = editorView ? fo4::stored_editor_id(*editorView) : std::string{};
            const auto storedFull = fo4::stored_inline_text(fullText, "EYES FULL");
            const auto storedIcon = fo4::stored_inline_text(iconText, "EYES ICON");

            const auto created = fo4::create_record(mutated, fo4::Signature::from_string("EYES"), storedEditorId);
            switch (created.status) {
                case fo4::RecordCreateStatus::created: break;
                case fo4::RecordCreateStatus::no_default:
                    return std::unexpected(Error{
                        "EyesCreateFailed",
                        "EYES has no default FULL in a localized plugin; the inline FULL cannot be stored there"
                    });
                case fo4::RecordCreateStatus::invalid_editor_id:
                    return std::unexpected(Error{"EyesCreateFailed", "Editor ID is not valid for this record"});
                case fo4::RecordCreateStatus::not_authorable:
                case fo4::RecordCreateStatus::preflight_failed:
                    return std::unexpected(Error{"EyesCreateFailed", "Generated EYES record failed schema validation"});
            }
            key = created.key;
            if (!storedFull.empty()) {
                fo4::schema::LocalizedString name;
                name.text = storedFull;
                const auto named = fo4::set_record_full_name(mutated, key, name);
                if (named != fo4::RecordNameStatus::updated && named != fo4::RecordNameStatus::unchanged) {
                    return std::unexpected(Error{"EyesCreateFailed", "The EYES name cannot be stored as a single FULL string"});
                }
            }
            const auto textureField = fo4::schema::scalar_field_descriptor("EYES\\Texture");
            const auto flagsField = fo4::schema::scalar_field_descriptor("EYES\\Flags");
            if (!textureField || !flagsField) {
                return std::unexpected(Error{"EyesCreateFailed", "EYES Texture and Flags rows are missing from the field schema"});
            }
            if (!storedIcon.empty() &&
                fo4::set_record_string(mutated, key, *textureField, 0, storedIcon) == fo4::ScalarMutationStatus::subrecord_missing) {
                return std::unexpected(Error{"EyesCreateFailed", "EYES is missing required ICON subrecord"});
            }
            if (dataFlags != 0 &&
                fo4::set_record_scalar(mutated, key, *flagsField, 0, std::to_string(dataFlags)) ==
                    fo4::ScalarMutationStatus::subrecord_missing) {
                return std::unexpected(Error{"EyesCreateFailed", "EYES is missing required DATA subrecord"});
            }
            if (recordFlags != 0) fo4::set_record_flags(mutated, key, flags.value());
        } else if (signatureText == "LVSP") {
            std::uint8_t chanceNone = 0;
            std::uint8_t maxCount = 0;
            std::uint8_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "chanceNone", std::uint8_t{0}, chanceNone) ||
                !lifecycleUnsignedField(*payload, "maxCount", std::uint8_t{0}, maxCount) ||
                !lifecycleUnsignedField(*payload, "flags", std::uint8_t{0}, flags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP chanceNone, maxCount, and flags must be 0..255"
                });
            }

            const auto entriesField = payload->find("entries");
            if (entriesField == payload->end() || !entriesField->is_array()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP creation requires an entries array"
                });
            }

            std::vector<fo4::forms::LeveledSpellEntry> entries;
            entries.reserve(entriesField->size());
            for (const auto& item : *entriesField) {
                if (!item.is_object()) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "Every LVSP entry must be an object"
                    });
                }
                const auto spell = item.find("spell");
                if (spell == item.end() || !spell->is_string()) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "Every LVSP entry requires a spell FormKey string"
                    });
                }

                fo4::forms::LeveledSpellEntry entry;
                if (!lifecycleUnsignedField(item, "level", std::uint16_t{1}, entry.level) ||
                    !lifecycleUnsignedField(item, "count", std::uint16_t{1}, entry.count) ||
                    !lifecycleUnsignedField(item, "chanceNone", std::uint8_t{0}, entry.chance_none)) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "LVSP entry level/count must be 0..65535 and chanceNone must be 0..255"
                    });
                }
                try {
                    entry.spell = fo4::FormKey::parse(spell->get_ref<const std::string&>());
                } catch (const std::exception& error) {
                    return std::unexpected(Error{"InvalidLeveledSpellReference", error.what()});
                }

                const auto* winner = (*graph_).winner(entry.spell);
                if (winner == nullptr ||
                    (winner->signature != fo4::Signature::from_string("SPEL") &&
                     winner->signature != fo4::Signature::from_string("LVSP"))) {
                    return std::unexpected(Error{
                        "InvalidLeveledSpellReference",
                        "LVSP entry spell must resolve to a loaded SPEL or LVSP record: " +
                            entry.spell.to_string()
                    });
                }
                entries.push_back(std::move(entry));
            }

            bool editorValid = false;
            const auto editorId = optionalEditorIdField(*payload, editorValid);
            if (!editorValid ||
                (editorId.has_value() && editorId->find('\0') != std::string::npos)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP editorId must be a string without embedded NUL bytes or null"
                });
            }

            key = fo4::forms::create_leveled_spell(
                mutated, chanceNone, maxCount, flags, entries);
            if (editorId.has_value() &&
                !fo4::set_record_editor_id(mutated, key, *editorId)) {
                return std::unexpected(Error{
                    "RecordCreateFailed",
                    "Newly created LVSP record could not accept its editor ID"
                });
            }
        } else if (signatureText == "LVLI" || signatureText == "LVLN") {
            std::uint8_t chanceNone = 0;
            std::uint8_t maxCount = 0;
            std::uint8_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "chanceNone", std::uint8_t{0}, chanceNone) ||
                !lifecycleUnsignedField(*payload, "maxCount", std::uint8_t{0}, maxCount) ||
                !lifecycleUnsignedField(*payload, "flags", std::uint8_t{0}, flags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    signatureText + " chanceNone, maxCount, and flags must be 0..255"
                });
            }
            bool editorValid = false;
            const auto editorId = optionalEditorIdField(*payload, editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    signatureText + " editorId must be a string or null"
                });
            }
            key = fo4::forms::create_leveled_list(
                mutated, fo4::Signature::from_string(signatureText), optionalStringView(editorId),
                chanceNone, maxCount, flags);
        } else if (signatureText == "FACT") {
            bool valid = false;
            const auto editorId = optionalEditorIdField(*payload, valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT editorId must be a string or null"});
            const auto fullName = optionalStringField(*payload, "fullName", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT fullName must be a string or null"});
            std::uint32_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "flags", std::uint32_t{0}, flags)) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT flags must be an unsigned 32-bit value"});
            }
            key = fo4::forms::create_faction(
                mutated, optionalStringView(editorId), optionalStringView(fullName), flags);
        } else if (signatureText == "RACE") {
            const auto created = createRaceLocked(mutated, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else if (signatureText == "ARMO" || signatureText == "ARMA") {
            const auto created = signatureText == "ARMO" ? createArmorLocked(mutated, *payload)
                                                         : createArmorAddonLocked(mutated, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else if (signatureText == "PACK") {
            const auto* editor = editorIdPayloadField(*payload);
            if (editor == nullptr || !editor->is_string() ||
                editor->get_ref<const std::string&>().empty()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "PACK creation requires a non-empty string editorId"});
            }
            const auto editorId = support::encodeEditorId(editor->get_ref<const std::string&>());
            if (!editorId) return std::unexpected(editorId.error());
            const auto options = travelPackageOptionsFromPayload(*payload);
            if (!options) return std::unexpected(options.error());
            const auto created = fo4::env::create_travel_package(mutated, *editorId, *options);
            if (created.status != fo4::env::TravelPackageStatus::created) {
                return std::unexpected(travelStatusError(created.status));
            }
            key = created.key;
        } else if (generatedLayoutCreationSupported(signatureText)) {
            if (signatureText == "QUST" && mutated.header.tes4_flags.is_localized()) {
                const auto name = payload->find("name");
                if (name != payload->end() && name->is_string() && !name->get_ref<const std::string&>().empty()) {
                    const auto language = qustLanguage(*payload);
                    if (!language) return std::unexpected(language.error());
                    auto opened = openLocalizedStrings(*loaded, *language);
                    if (!opened) return std::unexpected(opened.error());
                    strings = std::move(*opened);
                    const auto id = allocateLocalizedString(strings, fo4::env::StringSource::normal,
                                                            name->get_ref<const std::string&>());
                    if (!id) return std::unexpected(id.error());
                    localizedNameId = {fo4::Signature::from_string("FULL"), *id};
                    localizedIds = std::span<const fo4::LocalizedRowId>(&localizedNameId, 1);
                }
            }
            const auto created = createRichSchemaRecordLocked(mutated, signatureText, *payload, localizedIds);
            if (!created) return std::unexpected(created.error());
            key = *created;
            const auto extras = applyCreationExtrasLocked(mutated, key, signatureText, *payload);
            if (!extras) return std::unexpected(extras.error());
        } else if (signatureText == "PERK") {
            const auto created = createPerkLocked(*loaded, mutated, *payload, strings);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else if (signatureText == "PROJ" || signatureText == "EXPL" || signatureText == "HAZD") {
            const auto created = signatureText == "PROJ" ? createProjectileLocked(mutated, *payload)
                : signatureText == "EXPL"                ? createExplosionLocked(mutated, *payload)
                                                         : createHazardLocked(mutated, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                "Raw record creation is disabled until the schema pre-flight layer defines required fields and insertion order for " +
                    signatureText
            });
        }

        if (const auto staged = stageLocalizedStringsAndPlugin(loaded->name, std::move(strings), std::move(mutated)); !staged) {
            return std::unexpected(staged.error());
        }
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"signature", signatureText},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const fo4::forms::CellError& error) {
        return std::unexpected(Error{"CellCreateFailed", error.what()});
    } catch (const fo4::forms::LeveledListError& error) {
        return std::unexpected(Error{
            signatureText == "LVSP" ? "LeveledSpellCreateFailed" : "LeveledListCreateFailed",
            error.what()});
    } catch (const fo4::forms::FactionError& error) {
        return std::unexpected(Error{"FactionCreateFailed", error.what()});
    } catch (const fo4::schema::ScalarError& error) {
        const auto code = signatureText == "RACE" ? "RaceCreateFailed"
            : (signatureText == "ARMO" || signatureText == "ARMA") ? "ArmorCreateFailed"
                                                                  : "RecordCreateFailed";
        return std::unexpected(Error{code, error.what()});
    } catch (const fo4::forms::ProjExplHazdError& error) {
        return std::unexpected(Error{"ProjExplHazdCreateFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        const auto code = signatureText == "CELL" ? "CellCreateFailed"
            : signatureText == "EYES"             ? "EyesCreateFailed"
            : signatureText == "RACE"             ? "RaceCreateFailed"
            : (signatureText == "ARMO" || signatureText == "ARMA") ? "ArmorCreateFailed"
            : signatureText == "LVSP"             ? "LeveledSpellCreateFailed"
            : (signatureText == "LVLI" || signatureText == "LVLN") ? "LeveledListCreateFailed"
            : signatureText == "FACT"             ? "FactionCreateFailed"
            : signatureText == "PACK"             ? "PackageCreateFailed"
                                                  : "ProjExplHazdCreateFailed";
        return std::unexpected(Error{code, error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("RecordCreateFailed", error));
    }
}

std::optional<Error> RadiclePluginBackend::requireReferenceCoverage(
    const fo4::FormKey& key,
    std::string_view operation) const {
    for (const auto& candidate : plugins_) {
        auto coverageProbe = candidate.plugin;
        try {
            fo4::rekey_form_links(coverageProbe, key, key);
        } catch (const std::exception& error) {
            return Error{
                "ReferenceCoverageIncomplete",
                "Cannot prove " + std::string(operation) + " safety while scanning " +
                    candidate.name + ": " + error.what()
            };
        }
    }
    return std::nullopt;
}

Result<nlohmann::json> RadiclePluginBackend::deleteRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto formKeyText = lifecycleStringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (key.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Only records owned by the target plugin can be deleted: " + formKeyText
        });
    }

    try {
        // The graph is only as complete as the schema scanner. Strictly walk
        // every loaded plugin first so unsupported VMAD or malformed NAVM/NAVI
        // cannot hide an inbound reference to the record being deleted.
        if (const auto error = requireReferenceCoverage(key, "delete")) {
            return std::unexpected(*error);
        }

        for (const auto& version : (*graph_).versions(key)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        if ((*graph_).has_references(key)) {
            return std::unexpected(Error{
                "RecordReferenced",
                "Cannot delete a record while loaded records still reference it: " + formKeyText
            });
        }
        for (const auto& removed : fo4::records_removed_by_delete(loaded->plugin, key)) {
            const bool ownedElsewhere = removed.owner() != loaded->plugin.key;
            for (const auto& version : (*graph_).versions(removed)) {
                if (ownedElsewhere || version.plugin == loaded->plugin.key) continue;
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete " + formKeyText + ": " + std::string(version.plugin.filename()) +
                        " overrides " + removed.to_string() + " inside it"
                });
            }
            const bool masterKeepsRecord = ownedElsewhere && std::ranges::any_of(
                (*graph_).versions(removed),
                [&](const auto& version) { return version.plugin == removed.owner(); });
            for (const auto& reference : (*graph_).referenced_by(removed)) {
                if (reference.plugin == loaded->plugin.key || masterKeepsRecord) continue;
                return std::unexpected(Error{
                    "RecordReferenced",
                    "Cannot delete " + formKeyText + ": " + reference.source.to_string() + " in " +
                        std::string(reference.plugin.filename()) + " refers to " + removed.to_string() + " inside it"
                });
            }
        }

        auto mutated = loaded->plugin;
        const auto status = fo4::delete_local_record(mutated, key);
        switch (status) {
            case fo4::RecordDeleteStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::RecordDeleteStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordDeleteStatus::referenced:
                return std::unexpected(Error{"RecordReferenced", "Record is still referenced: " + formKeyText});
            case fo4::RecordDeleteStatus::deleted:
                break;
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"deleted", formKeyText}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordDeleteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::renumberRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto oldFormKeyText = lifecycleStringArgument(arguments, 1);
    const auto newFormKeyText = lifecycleStringArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (oldFormKeyText.empty() || newFormKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Old and new record FormKeys are required"});
    }

    fo4::FormKey oldKey;
    fo4::FormKey newKey;
    try {
        oldKey = fo4::FormKey::parse(oldFormKeyText);
        newKey = fo4::FormKey::parse(newFormKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    // Light plugins are allowed; fo4::renumber_local_record enforces the proven
    // 0x001..0xFFF small-file range. See Radicle/docs/ESL_FORMIDS.md and #26.
    if (oldKey.owner() != loaded->plugin.key || newKey.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Renumbering requires both old and new FormKeys to be owned by " + pluginName
        });
    }

    try {
        if (const auto error = requireReferenceCoverage(oldKey, "renumber")) {
            return std::unexpected(*error);
        }

        for (const auto& version : (*graph_).versions(oldKey)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot renumber a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        for (const auto& reference : (*graph_).referenced_by(oldKey)) {
            if (reference.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordReferencedExternally",
                    "Cannot renumber a record referenced by another loaded plugin: " +
                        std::string(reference.plugin.filename())
                });
            }
        }

        auto mutated = loaded->plugin;
        if (newKey != oldKey && fo4::find_record(mutated, newKey) == nullptr) {
            if (const auto refusal = support::parentGroupRelabelRefusal(mutated, oldKey)) return std::unexpected(*refusal);
        }
        const auto status = fo4::renumber_local_record(mutated, oldKey, newKey.local());
        switch (status) {
            case fo4::RecordRenumberStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + oldFormKeyText});
            case fo4::RecordRenumberStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordRenumberStatus::target_exists:
                return std::unexpected(Error{"RecordFormIdExists", "Target FormKey already exists: " + newFormKeyText});
            case fo4::RecordRenumberStatus::unchanged:
                return nlohmann::json(nlohmann::json{
                    {"ok", true},
                    {"plugin", pluginName},
                    {"oldFormKey", oldFormKeyText},
                    {"newFormKey", newFormKeyText},
                    {"changed", false}
                }.dump());
            case fo4::RecordRenumberStatus::renumbered:
                break;
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"oldFormKey", oldFormKeyText},
            {"newFormKey", newFormKeyText},
            {"changed", true}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordRenumberFailed", error.what()});
    }
}

}
