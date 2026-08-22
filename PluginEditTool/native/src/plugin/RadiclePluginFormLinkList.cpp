#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
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

bool listIndexArgument(const nlohmann::json& args,
                       std::size_t index,
                       std::size_t& value) {
    if (!args.is_array() || index >= args.size()) return false;
    std::uint64_t parsed = 0;
    if (args[index].is_number_unsigned()) {
        parsed = args[index].get<std::uint64_t>();
    } else if (args[index].is_number_integer()) {
        const auto signedValue = args[index].get<std::int64_t>();
        if (signedValue < 0) return false;
        parsed = static_cast<std::uint64_t>(signedValue);
    } else {
        return false;
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
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
                        std::to_string(path->index)
                });
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch",
                    "Target type is not valid for " + std::string(path->descriptor.path)
                });
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{
                    "FormLinkListInsertionBoundaryUnavailable",
                    "This operation does not permit list insertion"
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

Result<nlohmann::json> RadiclePluginBackend::insertFormLinkListElement(
    const nlohmann::json& arguments) {
    const auto pluginName = listStringArgument(arguments, 0);
    const auto formKeyText = listStringArgument(arguments, 1);
    const auto basePath = listStringArgument(arguments, 2);
    std::size_t index = 0;
    const auto targetText = listStringArgument(arguments, 4);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!listIndexArgument(arguments, 3, index)) {
        return std::unexpected(Error{"FormLinkListIndexInvalid", "List insertion index must be a non-negative integer"});
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
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, recordKey);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
    const auto descriptor = fo4::schema::formlink_list_descriptor(record->header.signature, basePath);
    if (!descriptor.has_value()) {
        return std::unexpected(Error{"FormLinkListPathInvalid", "Field is not a registered FormLink list: " + basePath});
    }
    const auto* winner = graph_.winner(targetKey);
    if (winner == nullptr) return std::unexpected(Error{"FormLinkTargetMissing", "FormLink target does not resolve: " + targetText});
    if (!fo4::schema::formlink_list_target_allowed(*descriptor, winner->signature)) {
        return std::unexpected(Error{"FormLinkTargetTypeMismatch", basePath + " does not accept " + winner->signature.to_string() + " targets"});
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::schema::insert_record_formlink_list_element(
            mutated, recordKey, *descriptor, index, targetKey);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{"FormLinkListRecordMismatch", "Record type does not match " + basePath});
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{"FormLinkListIndexOutOfRange", "Insertion index is outside " + basePath});
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{"FormLinkTargetTypeMismatch", "Target type is not valid for " + basePath});
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{
                    "FormLinkListInsertionBoundaryUnavailable",
                    "Cannot insert into " + basePath + " without an existing neighboring element; empty-list anchors are not modeled yet"
                });
            case fo4::schema::FormLinkListMutationStatus::unchanged:
                return nlohmann::json("No change for " + basePath + " in " + pluginName + ".");
        }
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json("Inserted element into " + basePath + " in " + pluginName + ".");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormLinkEncodingFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeFormLinkListElement(
    const nlohmann::json& arguments) {
    const auto pluginName = listStringArgument(arguments, 0);
    const auto formKeyText = listStringArgument(arguments, 1);
    const auto basePath = listStringArgument(arguments, 2);
    std::size_t index = 0;
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!listIndexArgument(arguments, 3, index)) {
        return std::unexpected(Error{"FormLinkListIndexInvalid", "List removal index must be a non-negative integer"});
    }

    fo4::FormKey recordKey;
    try {
        recordKey = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, recordKey);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
    const auto descriptor = fo4::schema::formlink_list_descriptor(record->header.signature, basePath);
    if (!descriptor.has_value()) {
        return std::unexpected(Error{"FormLinkListPathInvalid", "Field is not a registered FormLink list: " + basePath});
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::schema::remove_record_formlink_list_element(
            mutated, recordKey, *descriptor, index);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{"FormLinkListRecordMismatch", "Record type does not match " + basePath});
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{"FormLinkListIndexOutOfRange", "Removal index is outside " + basePath});
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{"FormLinkTargetTypeMismatch", "Unexpected target-type validation failure"});
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{"FormLinkListInsertionBoundaryUnavailable", "Unexpected insertion-boundary failure"});
            case fo4::schema::FormLinkListMutationStatus::unchanged:
                return nlohmann::json("No change for " + basePath + " in " + pluginName + ".");
        }
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json("Removed element from " + basePath + " in " + pluginName + ".");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
