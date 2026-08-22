#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <utility>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::setFieldValue(
    const nlohmann::json& arguments) {
    const auto fieldPath = stringArgument(arguments, 2);
    if (fo4::schema::indexed_formlink_list_path(fieldPath).has_value() ||
        fo4::schema::indexed_nested_formlink_list_path(fieldPath).has_value()) {
        return setFormLinkListField(arguments);
    }
    if (fieldPath.starts_with("ARMO\\") || fieldPath.starts_with("ARMA\\")) {
        return setArmorField(arguments);
    }

    const auto field = fo4::schema::scalar_field_descriptor(fieldPath);
    if (!field.has_value()) {
        return setField(arguments);
    }

    const auto pluginName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto value = stringArgument(arguments, 3);
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

    std::scoped_lock lock(mutex_);
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
        const auto status = fo4::set_record_scalar(mutated, key, *field, value);
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
            case fo4::ScalarMutationStatus::updated:
            case fo4::ScalarMutationStatus::unchanged:
                break;
        }

        if (status == fo4::ScalarMutationStatus::updated) {
            fo4::write_plugin_file(mutated, loaded->path);
            loaded->plugin = std::move(mutated);
            {
                std::error_code ec;
                loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
            }
            rebuildGraphLocked();
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

}  // namespace fo4ide::plugin
