#include "RadiclePluginSupport.hpp"
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "RadiclePluginArmor.hpp"
#include "RadiclePluginFields.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <iterator>
#include <string>
#include <utility>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {
namespace {

bool isNullFormLink(std::string_view value) {
    if (value.empty() || value.size() != 4) return value.empty();
    return std::tolower(static_cast<unsigned char>(value[0])) == 'n' &&
           std::tolower(static_cast<unsigned char>(value[1])) == 'u' &&
           std::tolower(static_cast<unsigned char>(value[2])) == 'l' &&
           std::tolower(static_cast<unsigned char>(value[3])) == 'l';
}

bool isRecordFullNamePath(std::string_view path) {
    if (path.size() != 9 || path.substr(4) != "\\Full") return false;
    for (const char ch : path.substr(0, 4)) {
        if (!std::isupper(static_cast<unsigned char>(ch)) && !std::isdigit(static_cast<unsigned char>(ch)) && ch != '_') return false;
    }
    return true;
}

bool isTypedLvspSetting(std::string_view path) {
    return path == "LVSP\\ChanceNone" || path == "LVSP\\MaxCount" || path == "LVSP\\Flags";
}

// Constraint: PET clients must keep EyesWriteFailed for refusals and InvalidFieldValue for DATA flags outside 0..255.
Result<nlohmann::json> asEyesWrite(Result<nlohmann::json> result, std::string_view scalarFailure) {
    if (result) return result;
    auto error = result.error();
    if (error.type == "ScalarFieldWriteFailed") {
        error.type = std::string(scalarFailure);
    } else if (error.type == "FieldWriteFailed" || error.type == "ScalarFieldMissing" ||
               error.type == "ScalarFieldRecordMismatch" || error.type.starts_with("NameField")) {
        error.type = "EyesWriteFailed";
    }
    return std::unexpected(std::move(error));
}

}

Result<nlohmann::json> RadiclePluginBackend::setFieldValue(
    const nlohmann::json& arguments) {
    const auto fieldPath = support::stringArgument(arguments, 2);
    if (fieldPath == "EYES\\Icon" || fieldPath == "EYES\\DataFlags") {
        const bool icon = fieldPath == "EYES\\Icon";
        auto rewritten = arguments;
        rewritten[2] = icon ? "EYES\\Texture" : "EYES\\Flags";
        return asEyesWrite(setFieldValue(rewritten), icon ? "EyesWriteFailed" : "InvalidFieldValue");
    }
    if (isRecordFullNamePath(fieldPath)) {
        auto result = setRecordFullName(support::stringArgument(arguments, 0), support::stringArgument(arguments, 1),
                                        std::string_view(fieldPath).substr(0, 4), support::stringArgument(arguments, 3));
        return fieldPath == "EYES\\Full" ? asEyesWrite(std::move(result), "EyesWriteFailed") : result;
    }
    if (fo4::schema::indexed_formlink_list_path(fieldPath).has_value() ||
        fo4::schema::indexed_nested_formlink_list_path(fieldPath).has_value()) {
        return setFormLinkListField(arguments);
    }
    if (findArmorModelPath(fieldPath) != nullptr) {
        return setArmorField(arguments);
    }

    const auto address = semanticFieldAddress(fieldPath);
    if (address && fo4::schema::formlink_field_descriptor(address->path).has_value()) {
        return setFormLinkField(arguments);
    }

    const auto field = fo4::schema::scalar_field_descriptor(address ? address->path : fieldPath);
    if (!field.has_value() || isTypedLvspSetting(fieldPath)) {
        return setField(arguments);
    }

    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto value = support::stringArgument(arguments, 3);
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
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + formKeyText
        });
    }
    if (record->header.signature != field->record) {
        return std::unexpected(Error{
            "ScalarFieldRecordMismatch",
            std::string(field->path) + " can only be written on a " +
                field->record.to_string() + " record"
        });
    }

    try {
        auto mutated = loaded->plugin;
        fo4::ScalarMutationStatus status;
        if (field->scalar.storage == fo4::schema::ScalarStorage::zstring ||
            field->scalar.storage == fo4::schema::ScalarStorage::fixed_string) {
            const auto stored = fo4::text::utf8_to_windows1252(value);
            if (!stored) {
                return std::unexpected(Error{"ScalarFieldWriteFailed",
                    "Text must be valid UTF-8 representable in Windows-1252"});
            }
            status = fo4::set_record_string(mutated, key, *field, address->index, *stored, true);
        } else {
            status = fo4::set_record_scalar(mutated, key, *field, address->index, value, true);
        }
        switch (status) {
            case fo4::ScalarMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case fo4::ScalarMutationStatus::subrecord_missing:
                return std::unexpected(Error{
                    "ScalarFieldMissing",
                    field->record.to_string() + " has no " +
                        field->scalar.subrecord.to_string() +
                        " subrecord; Radicle will not guess a missing subrecord insertion position"
                });
            case fo4::ScalarMutationStatus::next_alias_id_too_low:
                return std::unexpected(Error{
                    "ScalarFieldWriteFailed",
                    "QUST Next Alias ID must stay above every existing alias ID"
                });
            case fo4::ScalarMutationStatus::updated:
            case fo4::ScalarMutationStatus::unchanged:
                break;
        }

        if (status == fo4::ScalarMutationStatus::updated) {
            stagePluginMutationLocked(*loaded, std::move(mutated));
        }
        return nlohmann::json(
            "Updated " + std::string(field->path) + " for " + formKeyText +
            " in " + pluginName + ".");
    } catch (const fo4::schema::ScalarError& error) {
        return std::unexpected(Error{"ScalarFieldWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("FieldWriteFailed", error));
    }
}

Result<nlohmann::json> RadiclePluginBackend::setRecordName(const nlohmann::json& arguments) {
    return setRecordFullName(support::stringArgument(arguments, 0), support::stringArgument(arguments, 1), {},
                             support::stringArgument(arguments, 2));
}

Result<nlohmann::json> RadiclePluginBackend::setRecordFullName(
    const std::string& pluginName, const std::string& formKeyText, std::string_view expectedSignature,
    const std::string& value) {
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (value.empty() || value.find('\0') != std::string::npos) {
        return std::unexpected(Error{"InvalidFieldValue", "A record name must be non-empty text without NUL characters"});
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

    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + formKeyText});
    }
    const auto signature = record->header.signature;
    const auto fieldPath = signature.to_string() + "\\Full";
    if (!expectedSignature.empty() && fo4::Signature::from_string(expectedSignature) != signature) {
        return std::unexpected(Error{
            "ScalarFieldRecordMismatch",
            std::string(expectedSignature) + "\\Full can only be written on a " + std::string(expectedSignature) + " record"});
    }
    const auto unsupported = Error{"NameFieldUnsupported", signature.to_string() + " records have no FULL name"};
    const auto notInsertable = Error{
        "NameFieldNotInsertable",
        signature.to_string() + " has no FULL and Radicle cannot place one for this record without guessing its position"};
    const auto opaque = Error{"NameFieldOpaque", "The existing FULL is not a readable string; refusing to overwrite it"};

    // Constraint: copy_as_override copies FULL byte for byte, so an override of a localized master's record in a plain plugin holds that master's string ID where text belongs; a new name replaces it.
    const auto* owner = findPlugin(key.owner().filename());
    const auto carried = owner != nullptr && owner != loaded && owner->plugin.header.tes4_flags.is_localized() &&
                                 !loaded->plugin.header.tes4_flags.is_localized()
                             ? fo4::CarriedStringId::replace
                             : fo4::CarriedStringId::refuse;

    try {
        // Constraint: a record that cannot take the name is refused before a string ID is allocated for it.
        switch (fo4::record_full_name(loaded->plugin, *record).state) {
            case fo4::RecordNameState::not_named_type: return std::unexpected(unsupported);
            case fo4::RecordNameState::absent_not_insertable: return std::unexpected(notInsertable);
            case fo4::RecordNameState::opaque:
                if (carried == fo4::CarriedStringId::refuse) return std::unexpected(opaque);
                break;
            case fo4::RecordNameState::absent:
            case fo4::RecordNameState::present:
                break;
        }
        auto mutated = loaded->plugin;
        fo4::schema::LocalizedString name;
        LocalizedStringAllocation strings;
        const bool idForm = loaded->plugin.header.tes4_flags.is_localized() &&
                            fo4::schema::is_localized_field(signature, fo4::Signature::from_string("FULL"));
        if (idForm) {
            auto opened = openLocalizedStrings(*loaded, "en");
            if (!opened) return std::unexpected(opened.error());
            strings = std::move(*opened);
            const auto id = allocateLocalizedString(strings, fo4::env::StringSource::normal, value);
            if (!id) return std::unexpected(id.error());
            name.id = *id;
        } else {
            const auto stored = fo4::text::utf8_to_windows1252(value);
            if (!stored) {
                return std::unexpected(Error{"InvalidFieldValue", "Text must be valid UTF-8 representable in Windows-1252"});
            }
            name.text = *stored;
        }

        const auto status = fo4::set_record_full_name(mutated, key, name, carried);
        switch (status) {
            case fo4::RecordNameStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + formKeyText});
            case fo4::RecordNameStatus::not_named_type:
                return std::unexpected(unsupported);
            case fo4::RecordNameStatus::not_insertable:
                return std::unexpected(notInsertable);
            case fo4::RecordNameStatus::opaque_existing:
                return std::unexpected(opaque);
            case fo4::RecordNameStatus::form_mismatch:
                return std::unexpected(Error{"NameFieldFormMismatch", "The name form does not match how this plugin stores FULL"});
            case fo4::RecordNameStatus::invalid_name:
                return std::unexpected(Error{"InvalidFieldValue", "The name cannot be stored as a single FULL string"});
            case fo4::RecordNameStatus::unchanged:
                return nlohmann::json("Unchanged " + fieldPath + " for " + formKeyText + " in " + pluginName + ".");
            case fo4::RecordNameStatus::updated:
                break;
        }

        if (idForm) {
            if (const auto staged = stageLocalizedStringsAndPlugin(loaded->name, std::move(strings), std::move(mutated)); !staged) {
                return std::unexpected(staged.error());
            }
        } else {
            stagePluginMutationLocked(*loaded, std::move(mutated));
        }
        return nlohmann::json("Updated " + fieldPath + " for " + formKeyText + " in " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setFormLinkField(
    const nlohmann::json& arguments) {
    const auto fieldPath = support::stringArgument(arguments, 2);
    const auto address = semanticFieldAddress(fieldPath);
    const auto field = fo4::schema::formlink_field_descriptor(address ? address->path : fieldPath);
    if (!field.has_value()) return setField(arguments);
    if (!arguments.is_array() || arguments.size() <= 3 || !arguments[3].is_string()) {
        return std::unexpected(Error{
            "InvalidFormLinkValue",
            "FormLink value must be a string or NULL"
        });
    }

    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto value = arguments[3].get<std::string>();
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

    fo4::FormKey target;
    if (!isNullFormLink(value)) {
        try {
            target = fo4::FormKey::parse(value);
        } catch (const std::exception& error) {
            return std::unexpected(Error{"InvalidFormLinkValue", error.what()});
        }
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + formKeyText
        });
    }
    if (record->header.signature != field->record) {
        return std::unexpected(Error{
            "FormLinkFieldRecordMismatch",
            std::string(field->path) + " can only be written on a " +
                field->record.to_string() + " record"
        });
    }

    if (!target.owner().empty()) {
        const auto* target_record = (*graph_).winner(target);
        if (target_record == nullptr) {
            return std::unexpected(Error{
                "FormLinkTargetUnresolved",
                "FormLink target is not loaded: " + value
            });
        }
        if (!fo4::schema::formlink_field_target_allowed(
                *field, target_record->signature)) {
            return std::unexpected(Error{
                "FormLinkTargetTypeMismatch",
                value + " is a " + target_record->signature.to_string() + ", not an allowed target for " +
                    std::string(field->path)
            });
        }
    } else if (!field->nullable) {
        return std::unexpected(Error{
            "FormLinkNullNotAllowed",
            std::string(field->path) + " does not allow NULL"
        });
    }

    try {
        auto mutated = loaded->plugin;
        support::ensureOwnerMaster(mutated, target.owner(), plugins_);
        const auto status = fo4::set_record_formlink_field(mutated, key, *field, address->index, target);
        switch (status) {
            case fo4::FormLinkFieldMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case fo4::FormLinkFieldMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkFieldRecordMismatch",
                    std::string(field->path) + " does not apply to this record"
                });
            case fo4::FormLinkFieldMutationStatus::subrecord_missing:
                return std::unexpected(Error{
                    "FormLinkFieldMissing",
                    std::string(field->path) + " has no " + field->subrecord.to_string() +
                        " subrecord; Radicle will not guess a missing subrecord insertion position"
                });
            case fo4::FormLinkFieldMutationStatus::updated:
            case fo4::FormLinkFieldMutationStatus::unchanged:
                break;
        }

        if (status == fo4::FormLinkFieldMutationStatus::updated) {
            stagePluginMutationLocked(*loaded, std::move(mutated));
        }
        return nlohmann::json(
            "Updated " + std::string(field->path) + " for " + formKeyText +
            " in " + pluginName + ".");
    } catch (const fo4::schema::FormLinkFieldError& error) {
        return std::unexpected(Error{"FormLinkFieldWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("FieldWriteFailed", error));
    }
}

}  // namespace fo4ide::plugin
