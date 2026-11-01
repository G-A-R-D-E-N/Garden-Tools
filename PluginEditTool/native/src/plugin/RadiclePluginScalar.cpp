#include "RadiclePluginSupport.hpp"
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "RadiclePluginFields.hpp"

#include <cctype>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
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

bool isTypedLvspSetting(std::string_view path) {
    return path == "LVSP\\ChanceNone" || path == "LVSP\\MaxCount" || path == "LVSP\\Flags";
}

}

Result<nlohmann::json> RadiclePluginBackend::setFieldValue(
    const nlohmann::json& arguments) {
    const auto fieldPath = support::stringArgument(arguments, 2);
    if (fo4::schema::indexed_formlink_list_path(fieldPath).has_value() ||
        fo4::schema::indexed_nested_formlink_list_path(fieldPath).has_value()) {
        return setFormLinkListField(arguments);
    }
    if (fieldPath.starts_with("ARMO\\") || fieldPath.starts_with("ARMA\\")) {
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
            status = fo4::set_record_string(mutated, key, *field, address->index, *stored);
        } else {
            status = fo4::set_record_scalar(mutated, key, *field, address->index, value);
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
        ensureOwnerMaster(mutated, target.owner());
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
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
