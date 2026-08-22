#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>

namespace fo4ide::plugin {
namespace {

std::string listStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::setFormLinkListField(
    const nlohmann::json& arguments) {
    const auto pluginName = listStringArgument(arguments, 0);
    const auto formKeyText = listStringArgument(arguments, 1);
    const auto fieldPath = listStringArgument(arguments, 2);
    const auto targetText = listStringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }

    const auto path = fo4::schema::indexed_formlink_list_path(fieldPath);
    if (!path.has_value()) {
        return std::unexpected(Error{
            "FormLinkListPathInvalid",
            "Field is not a registered indexed FormLink list path: " + fieldPath
        });
    }

    fo4::FormKey recordKey;
    fo4::FormKey targetKey;
    try {
        recordKey = fo4::FormKey::parse(formKeyText);
        targetKey = fo4::FormKey::parse(targetText);
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
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    const auto* record = fo4::find_record(loaded->plugin, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + formKeyText
        });
    }
    if (record->header.signature != path->descriptor.record) {
        return std::unexpected(Error{
            "FormLinkListRecordMismatch",
            std::string(path->descriptor.path) + " can only be written on a " +
                path->descriptor.record.to_string() + " record"
        });
    }

    const auto* winner = graph_.winner(targetKey);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "FormLinkTargetMissing",
            "FormLink target does not resolve in the loaded environment: " + targetText
        });
    }
    if (!fo4::schema::formlink_list_target_allowed(
            path->descriptor, winner->signature)) {
        return std::unexpected(Error{
            "FormLinkTargetTypeMismatch",
            std::string(path->descriptor.path) + " does not accept " +
                winner->signature.to_string() + " targets"
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::schema::set_record_formlink_list_element(
            mutated, recordKey, path->descriptor, path->index, targetKey);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkListRecordMismatch",
                    "Record type does not match " + std::string(path->descriptor.path)
                });
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListIndexOutOfRange",
                    std::string(path->descriptor.path) + " has no element at index " +
                        std::to_string(path->index) +
                        "; insertion remains disabled until schema ordering is proven"
                });
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch",
                    "Target type is not valid for " + std::string(path->descriptor.path)
                });
            case fo4::schema::FormLinkListMutationStatus::unchanged:
                return nlohmann::json(
                    "No change for " + fieldPath + " in " + pluginName + ".");
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Updated " + fieldPath + " for " + formKeyText + " in " +
            pluginName + ".");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormLinkEncodingFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
