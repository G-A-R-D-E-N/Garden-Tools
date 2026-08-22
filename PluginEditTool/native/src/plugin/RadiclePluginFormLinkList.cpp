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

    const auto simplePath = fo4::schema::indexed_formlink_list_path(fieldPath);
    const auto nestedPath = fo4::schema::indexed_nested_formlink_list_path(fieldPath);
    if (!simplePath.has_value() && !nestedPath.has_value()) {
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

    if (simplePath.has_value() &&
        record->header.signature != simplePath->descriptor.record) {
        return std::unexpected(Error{
            "FormLinkListRecordMismatch",
            std::string(simplePath->descriptor.path) + " can only be written on a " +
                simplePath->descriptor.record.to_string() + " record"
        });
    }
    if (nestedPath.has_value() &&
        record->header.signature != nestedPath->descriptor.record) {
        return std::unexpected(Error{
            "FormLinkListRecordMismatch",
            std::string(nestedPath->descriptor.container_path) + " can only be written on a " +
                nestedPath->descriptor.record.to_string() + " record"
        });
    }

    const auto* winner = graph_.winner(targetKey);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "FormLinkTargetMissing",
            "FormLink target does not resolve in the loaded environment: " + targetText
        });
    }
    const bool targetAllowed = simplePath.has_value()
        ? fo4::schema::formlink_list_target_allowed(
              simplePath->descriptor, winner->signature)
        : fo4::schema::nested_formlink_list_target_allowed(
              nestedPath->descriptor, winner->signature);
    if (!targetAllowed) {
        return std::unexpected(Error{
            "FormLinkTargetTypeMismatch",
            fieldPath + " does not accept " + winner->signature.to_string() + " targets"
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = simplePath.has_value()
            ? fo4::schema::set_record_formlink_list_element(
                  mutated, recordKey, simplePath->descriptor,
                  simplePath->index, targetKey)
            : fo4::schema::set_record_nested_formlink_list_element(
                  mutated, recordKey, nestedPath->descriptor,
                  nestedPath->container_index, nestedPath->index, targetKey);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkListRecordMismatch",
                    "Record type does not match " + fieldPath
                });
            case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListContainerIndexOutOfRange",
                    "Nested FormLink container index is outside " + fieldPath
                });
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListIndexOutOfRange",
                    "FormLink list has no requested element: " + fieldPath
                });
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch",
                    "Target type is not valid for " + fieldPath
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
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (!listIndexArgument(arguments, 3, index)) {
        return std::unexpected(Error{
            "FormLinkListIndexInvalid",
            "List insertion index must be a non-negative integer"
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
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
    }

    const auto simpleDescriptor =
        fo4::schema::formlink_list_descriptor(record->header.signature, basePath);
    const auto nestedPath = fo4::schema::scoped_nested_formlink_list_path(basePath);
    if (!simpleDescriptor.has_value() && !nestedPath.has_value()) {
        return std::unexpected(Error{
            "FormLinkListPathInvalid",
            "Field is not a registered FormLink list: " + basePath
        });
    }
    if (nestedPath.has_value() &&
        nestedPath->descriptor.record != record->header.signature) {
        return std::unexpected(Error{
            "FormLinkListRecordMismatch",
            "Record type does not match " + basePath
        });
    }

    const auto* winner = graph_.winner(targetKey);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "FormLinkTargetMissing",
            "FormLink target does not resolve: " + targetText
        });
    }
    const bool targetAllowed = simpleDescriptor.has_value()
        ? fo4::schema::formlink_list_target_allowed(
              *simpleDescriptor, winner->signature)
        : fo4::schema::nested_formlink_list_target_allowed(
              nestedPath->descriptor, winner->signature);
    if (!targetAllowed) {
        return std::unexpected(Error{
            "FormLinkTargetTypeMismatch",
            basePath + " does not accept " + winner->signature.to_string() + " targets"
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = simpleDescriptor.has_value()
            ? fo4::schema::insert_record_formlink_list_element(
                  mutated, recordKey, *simpleDescriptor, index, targetKey)
            : fo4::schema::insert_record_nested_formlink_list_element(
                  mutated, recordKey, nestedPath->descriptor,
                  nestedPath->container_index, index, targetKey);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkListRecordMismatch", "Record type does not match " + basePath});
            case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListContainerIndexOutOfRange",
                    "Nested FormLink container index is outside " + basePath
                });
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListIndexOutOfRange", "Insertion index is outside " + basePath});
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch", "Target type is not valid for " + basePath});
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{
                    "FormLinkListInsertionBoundaryUnavailable",
                    "Cannot determine a safe insertion boundary for " + basePath
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
        return nlohmann::json(
            "Inserted element into " + basePath + " in " + pluginName + ".");
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
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (!listIndexArgument(arguments, 3, index)) {
        return std::unexpected(Error{
            "FormLinkListIndexInvalid",
            "List removal index must be a non-negative integer"
        });
    }

    fo4::FormKey recordKey;
    try {
        recordKey = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
    }

    const auto simpleDescriptor =
        fo4::schema::formlink_list_descriptor(record->header.signature, basePath);
    const auto nestedPath = fo4::schema::scoped_nested_formlink_list_path(basePath);
    if (!simpleDescriptor.has_value() && !nestedPath.has_value()) {
        return std::unexpected(Error{
            "FormLinkListPathInvalid",
            "Field is not a registered FormLink list: " + basePath
        });
    }
    if (nestedPath.has_value() &&
        nestedPath->descriptor.record != record->header.signature) {
        return std::unexpected(Error{
            "FormLinkListRecordMismatch",
            "Record type does not match " + basePath
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = simpleDescriptor.has_value()
            ? fo4::schema::remove_record_formlink_list_element(
                  mutated, recordKey, *simpleDescriptor, index)
            : fo4::schema::remove_record_nested_formlink_list_element(
                  mutated, recordKey, nestedPath->descriptor,
                  nestedPath->container_index, index);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkListRecordMismatch", "Record type does not match " + basePath});
            case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListContainerIndexOutOfRange",
                    "Nested FormLink container index is outside " + basePath
                });
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListIndexOutOfRange", "Removal index is outside " + basePath});
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch", "Unexpected target-type validation failure"});
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{
                    "FormLinkListInsertionBoundaryUnavailable",
                    "Unexpected insertion-boundary failure"
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
        return nlohmann::json(
            "Removed element from " + basePath + " in " + pluginName + ".");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
