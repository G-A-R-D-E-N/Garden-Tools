#include "RadiclePluginArmor.hpp"
#include "RadiclePluginLifecycleFields.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/preflight.hpp>
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
using namespace lifecycle;
namespace {

constexpr ArmorModelPath kArmorModelPaths[] = {
    {"ARMO\\MaleWorldModel", "ARMO\\MaleWorldModel"},
    {"ARMO\\MaleIcon", "ARMO\\MaleIcon"},
    {"ARMO\\MaleMessageIcon", "ARMO\\MaleMessageIcon"},
    {"ARMO\\FemaleWorldModel", "ARMO\\FemaleWorldModel"},
    {"ARMO\\FemaleIcon", "ARMO\\FemaleIcon"},
    {"ARMO\\FemaleMessageIcon", "ARMO\\FemaleMessageIcon"},
    {"ARMA\\MaleBipedModel", "ARMA\\BipedMaleModel"},
    {"ARMA\\FemaleBipedModel", "ARMA\\BipedFemaleModel"},
    {"ARMA\\MaleFirstPersonModel", "ARMA\\FirstPersonMaleModel"},
    {"ARMA\\FemaleFirstPersonModel", "ARMA\\FirstPersonFemaleModel"},
};

struct PayloadString {
    const char* key;
    const char* row;
};

struct StringRow {
    const char* row;
    std::optional<std::string> value;
};

struct ScalarRow {
    const char* row;
    std::string text;
};

std::string armorStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

enum class StringRowWrite { written, record_missing, refused };

// Constraint: a missing optional subrecord is added through the authoring order, which refuses a record holding an unlisted subrecord such as BOD2.
StringRowWrite writeStringRow(fo4::Plugin& plugin,
                              const fo4::FormKey& key,
                              const fo4::schema::ScalarFieldDescriptor& field,
                              std::string_view stored) {
    auto status = fo4::set_record_string(plugin, key, field, 0, stored);
    if (status == fo4::ScalarMutationStatus::subrecord_missing) {
        if (fo4::add_record_subrecord(plugin, key, field.scalar.subrecord) != fo4::SubrecordAddStatus::added) {
            return StringRowWrite::refused;
        }
        status = fo4::set_record_string(plugin, key, field, 0, stored);
    }
    switch (status) {
        case fo4::ScalarMutationStatus::updated:
        case fo4::ScalarMutationStatus::unchanged: return StringRowWrite::written;
        case fo4::ScalarMutationStatus::record_missing: return StringRowWrite::record_missing;
        default: return StringRowWrite::refused;
    }
}

Result<fo4::FormKey> createArmorRecordLocked(fo4::Plugin& mutated,
                                             std::string_view signature,
                                             const std::optional<std::string>& editorId,
                                             const std::vector<StringRow>& strings,
                                             const std::vector<ScalarRow>& scalars) {
    const auto fail = [](std::string message) {
        return std::unexpected(Error{"ArmorCreateFailed", std::move(message)});
    };
    const auto hasNul = [](const std::optional<std::string>& text) {
        return text.has_value() && text->find('\0') != std::string::npos;
    };
    bool nul = hasNul(editorId);
    for (const auto& row : strings) nul = nul || hasNul(row.value);
    if (nul) return fail("ARMO/ARMA string fields cannot contain embedded NUL bytes");

    const auto storedEditorId = editorId ? fo4::stored_editor_id(*editorId) : std::string{};
    std::vector<std::optional<std::string>> stored;
    for (const auto& row : strings) {
        stored.push_back(row.value ? std::optional<std::string>{fo4::stored_inline_text(*row.value, "ARMO/ARMA string field")}
                                   : std::nullopt);
    }

    const auto name = std::string(signature);
    const auto created = fo4::create_record(mutated, fo4::Signature::from_string(signature), storedEditorId);
    switch (created.status) {
        case fo4::RecordCreateStatus::created: break;
        case fo4::RecordCreateStatus::no_default:
            return fail(name + " has a required subrecord with no usable default");
        case fo4::RecordCreateStatus::invalid_editor_id:
            return fail("Editor ID is not valid for this record");
        case fo4::RecordCreateStatus::not_authorable:
        case fo4::RecordCreateStatus::preflight_failed:
            return fail("Generated " + name + " record failed schema validation");
    }
    const auto key = created.key;

    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (!stored[i]) continue;
        const auto field = fo4::schema::scalar_field_descriptor(strings[i].row);
        if (!field) return fail(std::string(strings[i].row) + " row is missing from the field schema");
        if (writeStringRow(mutated, key, *field, *stored[i]) != StringRowWrite::written) {
            return fail(name + " could not accept " + strings[i].row);
        }
    }
    for (const auto& row : scalars) {
        const auto field = fo4::schema::scalar_field_descriptor(row.row);
        if (!field) return fail(std::string(row.row) + " row is missing from the field schema");
        const auto status = fo4::set_record_scalar(mutated, key, *field, 0, row.text);
        if (status != fo4::ScalarMutationStatus::updated && status != fo4::ScalarMutationStatus::unchanged) {
            return fail(name + " is missing the subrecord for " + row.row);
        }
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(mutated, key)) {
        if (issue.severity != fo4::schema::PreflightSeverity::error) continue;
        return fail(name + " authoring preflight failed: " + issue.message);
    }
    return key;
}

Result<std::vector<StringRow>> payloadStringRows(const nlohmann::json& payload,
                                                 std::string_view signature,
                                                 std::initializer_list<PayloadString> keys) {
    std::vector<StringRow> rows;
    for (const auto& item : keys) {
        bool valid = false;
        auto value = optionalStringField(payload, item.key, valid);
        if (!valid) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload",
                std::string(signature) + " " + item.key + " must be a string or null"});
        }
        rows.push_back({item.row, std::move(value)});
    }
    return rows;
}

}

const ArmorModelPath* findArmorModelPath(std::string_view field) {
    const auto* found = std::find_if(std::begin(kArmorModelPaths), std::end(kArmorModelPaths),
        [&](const ArmorModelPath& candidate) { return candidate.field == field; });
    return found == std::end(kArmorModelPaths) ? nullptr : found;
}

Result<fo4::FormKey> createArmorLocked(fo4::Plugin& mutated, const nlohmann::json& payload) {
    bool valid = false;
    const auto editorId = optionalEditorIdField(payload, valid);
    if (!valid) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO editorId must be a string or null"});
    }
    auto strings = payloadStringRows(payload, "ARMO",
        {{"maleWorldModel", "ARMO\\MaleWorldModel"},
         {"maleIcon", "ARMO\\MaleIcon"},
         {"maleMessageIcon", "ARMO\\MaleMessageIcon"},
         {"femaleWorldModel", "ARMO\\FemaleWorldModel"},
         {"femaleIcon", "ARMO\\FemaleIcon"},
         {"femaleMessageIcon", "ARMO\\FemaleMessageIcon"}});
    if (!strings) return std::unexpected(strings.error());

    std::int32_t value = 0;
    float weight = 0.0f;
    std::uint32_t health = 0;
    if (!lifecycleSignedField(payload, "value", std::int32_t{0}, value) ||
        !lifecycleFloatField(payload, "weight", 0.0f, weight) ||
        !lifecycleUnsignedField(payload, "health", std::uint32_t{0}, health)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "ARMO value must be int32, weight must be finite f32, and health must be uint32"});
    }
    return createArmorRecordLocked(mutated, "ARMO", editorId, *strings,
        {{"ARMO\\Value", std::to_string(value)},
         {"ARMO\\Weight", floatRowText(weight)},
         {"ARMO\\Health", std::to_string(health)}});
}

Result<fo4::FormKey> createArmorAddonLocked(fo4::Plugin& mutated, const nlohmann::json& payload) {
    bool valid = false;
    const auto editorId = optionalEditorIdField(payload, valid);
    if (!valid) {
        return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA editorId must be a string or null"});
    }
    auto strings = payloadStringRows(payload, "ARMA",
        {{"maleBipedModel", "ARMA\\BipedMaleModel"},
         {"femaleBipedModel", "ARMA\\BipedFemaleModel"},
         {"maleFirstPersonModel", "ARMA\\FirstPersonMaleModel"},
         {"femaleFirstPersonModel", "ARMA\\FirstPersonFemaleModel"}});
    if (!strings) return std::unexpected(strings.error());

    std::uint8_t malePriority = 0;
    std::uint8_t femalePriority = 0;
    std::uint8_t maleSlider = 0;
    std::uint8_t femaleSlider = 0;
    std::uint8_t detection = 0;
    float weaponAdjust = 0.0f;
    if (!lifecycleUnsignedField(payload, "malePriority", std::uint8_t{0}, malePriority) ||
        !lifecycleUnsignedField(payload, "femalePriority", std::uint8_t{0}, femalePriority) ||
        !lifecycleUnsignedField(payload, "maleWeightSlider", std::uint8_t{0}, maleSlider) ||
        !lifecycleUnsignedField(payload, "femaleWeightSlider", std::uint8_t{0}, femaleSlider) ||
        !lifecycleUnsignedField(payload, "detectionSoundValue", std::uint8_t{0}, detection) ||
        !lifecycleFloatField(payload, "weaponAdjust", 0.0f, weaponAdjust)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "ARMA priority/slider/detection values must be uint8 and weaponAdjust must be finite f32"});
    }
    // Constraint: the weight-slider rows are flag bytes whose only defined bits are 0x01 and 0x02.
    if ((maleSlider & ~0x03u) != 0 || (femaleSlider & ~0x03u) != 0) {
        return std::unexpected(Error{
            "ArmorCreateFailed", "ARMA weight-slider flags may use only bits 0x01 and 0x02"});
    }
    return createArmorRecordLocked(mutated, "ARMA", editorId, *strings,
        {{"ARMA\\MalePriority", std::to_string(malePriority)},
         {"ARMA\\FemalePriority", std::to_string(femalePriority)},
         {"ARMA\\WeightSliderMale", std::to_string(maleSlider)},
         {"ARMA\\WeightSliderFemale", std::to_string(femaleSlider)},
         {"ARMA\\DetectionSoundValue", std::to_string(detection)},
         {"ARMA\\WeaponAdjust", floatRowText(weaponAdjust)}});
}

Result<nlohmann::json> RadiclePluginBackend::setArmorField(
    const nlohmann::json& arguments) {
    const auto pluginName = armorStringArgument(arguments, 0);
    const auto formKeyText = armorStringArgument(arguments, 1);
    const auto field = armorStringArgument(arguments, 2);
    const auto value = armorStringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (value.find('\0') != std::string::npos) {
        return std::unexpected(Error{
            "InvalidFieldValue",
            "Armor model and icon fields cannot contain embedded NUL bytes"
        });
    }

    const auto* path = findArmorModelPath(field);
    if (path == nullptr) {
        return std::unexpected(Error{
            "FieldWriteUnsupported",
            "Field is not a supported ARMO or ARMA model path: " + field
        });
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
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto row = fo4::schema::scalar_field_descriptor(path->row);
        if (!row) {
            return std::unexpected(Error{"FieldWriteFailed", "Armor row is missing from the field schema: " + field});
        }
        switch (writeStringRow(mutated, key, *row, fo4::stored_inline_text(value, field))) {
            case StringRowWrite::written: break;
            case StringRowWrite::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case StringRowWrite::refused:
                return std::unexpected(Error{
                    "ArmorWriteFailed",
                    "Cannot insert a missing armor field around unmodeled subrecords or into an invalid authoring layout: " + field
                });
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));

        return nlohmann::json(
            "Updated " + field + " for " + formKeyText + " in " + pluginName + ".");
    } catch (const fo4::schema::ScalarError& error) {
        return std::unexpected(Error{"ArmorWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"ArmorWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
