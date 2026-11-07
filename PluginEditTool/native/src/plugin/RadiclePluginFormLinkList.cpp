#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/formid.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;

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

std::string listLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

// Mirrors WriteService.FindMutableRecord: resolve the target record by FormKey
// (contains ':') or, failing that, a case-insensitive EditorID match inside the
// same plugin.
const fo4::Record* resolveRecordInPlugin(const fo4::Plugin& plugin,
                                         std::string_view recordText,
                                         fo4::FormKey& outKey) {
    if (recordText.find(':') != std::string_view::npos) {
        try {
            outKey = fo4::FormKey::parse(recordText);
        } catch (const std::exception&) {
            return nullptr;
        }
        return fo4::find_record(plugin, outKey);
    }
    const auto wanted = listLower(recordText);
    for (const auto* record : fo4::collect_records(plugin)) {
        if (listLower(editorId(*record)) == wanted) {
            outKey = record->form_key;
            return record;
        }
    }
    return nullptr;
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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

    const auto* winner = (*graph_).winner(targetKey);
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

        stagePluginMutationLocked(*loaded, std::move(mutated));
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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

    const auto* winner = (*graph_).winner(targetKey);
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
        stagePluginMutationLocked(*loaded, std::move(mutated));
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Removed element from " + basePath + " in " + pluginName + ".");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

std::optional<fo4::FormKey> RadiclePluginBackend::resolveListValue(
    std::string_view value) const {
    if (value.find(':') != std::string_view::npos) {
        try {
            return fo4::FormKey::parse(value);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    const auto wanted = listLower(value);
    for (const auto& candidate : plugins_) {
        for (const auto* record : fo4::collect_records(candidate.plugin)) {
            if (listLower(editorId(*record)) == wanted) {
                return record->form_key;
            }
        }
    }
    return std::nullopt;
}

void RadiclePluginBackend::ensureOwnerMaster(fo4::Plugin& plugin,
                                             const fo4::PluginKey& owner) const {
    if (owner.empty() || owner == plugin.key) return;
    for (const auto& master : plugin.header.masters) {
        if (master.key == owner) return;
    }
    fo4::PluginKind kind =
        owner.looks_light() ? fo4::PluginKind::light : fo4::PluginKind::full;
    for (const auto& candidate : plugins_) {
        if (candidate.plugin.key == owner) {
            kind = fo4::classify_plugin_kind(candidate.plugin.header.tes4_flags, candidate.plugin.key);
            break;
        }
    }
    std::vector<fo4::MasterDescriptor> masters;
    masters.reserve(plugin.header.masters.size() + 1);
    for (const auto& master : plugin.header.masters) {
        masters.push_back({master.key, master.kind});
    }
    masters.push_back({owner, kind});
    plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(masters));
}

Result<nlohmann::json> RadiclePluginBackend::addListItem(
    const nlohmann::json& arguments) {
    const auto pluginName = listStringArgument(arguments, 0);
    const auto recordText = listStringArgument(arguments, 1);
    const auto field = listStringArgument(arguments, 2);
    const auto value = listStringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (field.empty()) {
        return std::unexpected(Error{"ListFieldMissing", "List field is empty"});
    }
    if (value.empty()) {
        return std::unexpected(Error{"ListValueMissing", "List value is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey recordKey;
    const auto* record = resolveRecordInPlugin(loaded->plugin, recordText, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound", "Record '" + recordText + "' not found in " + pluginName + "."});
    }

    const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
        record->header.signature, field);
    if (!descriptor.has_value()) {
        return std::unexpected(Error{
            "ListFieldUnsupported",
            "Field '" + field + "' is not a FormLink list on " +
                record->header.signature.to_string() +
                " records; add_list_item supports FormLink lists (e.g. Keywords, FormList Items)."});
    }

    const auto targetKey = resolveListValue(value);
    if (!targetKey.has_value()) {
        return std::unexpected(Error{
            "ListValueUnresolved",
            "'" + value + "' is not a FormKey and no loaded record has that EditorID."});
    }

    if (!descriptor->wildcard_target) {
        const auto* winner = (*graph_).winner(*targetKey);
        if (winner == nullptr) {
            return std::unexpected(Error{
                "FormLinkTargetMissing",
                "FormLink target does not resolve in the loaded environment: " + value});
        }
        if (!fo4::schema::formlink_list_target_allowed(*descriptor, winner->signature)) {
            return std::unexpected(Error{
                "FormLinkTargetTypeMismatch",
                field + " does not accept " + winner->signature.to_string() + " targets"});
        }
    }

    try {
        const auto currentSize =
            fo4::schema::record_formlink_list_size(loaded->plugin, recordKey, *descriptor);
        if (!currentSize.has_value()) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordText + "' not found in " + pluginName + "."});
        }
        auto mutated = loaded->plugin;
        ensureOwnerMaster(mutated, targetKey->owner());
        const auto status = fo4::schema::insert_record_formlink_list_element(
            mutated, recordKey, *descriptor, *currentSize, *targetKey);
        switch (status) {
            case fo4::schema::FormLinkListMutationStatus::updated:
                break;
            case fo4::schema::FormLinkListMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound", "Record '" + recordText + "' not found in " + pluginName + "."});
            case fo4::schema::FormLinkListMutationStatus::record_mismatch:
                return std::unexpected(Error{
                    "FormLinkListRecordMismatch", "Record type does not match " + field});
            case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
                return std::unexpected(Error{
                    "FormLinkTargetTypeMismatch", "Target type is not valid for " + field});
            case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
                return std::unexpected(Error{
                    "FormLinkListInsertionBoundaryUnavailable",
                    "Cannot determine a safe insertion boundary for " + field});
            case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
            case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
                return std::unexpected(Error{
                    "FormLinkListIndexOutOfRange", "Append index is outside " + field});
            case fo4::schema::FormLinkListMutationStatus::unchanged:
                return nlohmann::json("No change for " + field + " in " + pluginName + ".");
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        const auto count = *currentSize + 1;
        return nlohmann::json(
            "Added " + value + " to " + field + " on " + recordText + " in " +
            pluginName + " (now " + std::to_string(count) + " item(s)).");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormLinkEncodingFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeListItem(
    const nlohmann::json& arguments) {
    const auto pluginName = listStringArgument(arguments, 0);
    const auto recordText = listStringArgument(arguments, 1);
    const auto field = listStringArgument(arguments, 2);
    const auto value = listStringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (field.empty()) {
        return std::unexpected(Error{"ListFieldMissing", "List field is empty"});
    }
    if (value.empty()) {
        return std::unexpected(Error{"ListValueMissing", "List value is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey recordKey;
    const auto* record = resolveRecordInPlugin(loaded->plugin, recordText, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound", "Record '" + recordText + "' not found in " + pluginName + "."});
    }

    const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
        record->header.signature, field);
    if (!descriptor.has_value()) {
        return std::unexpected(Error{
            "ListFieldUnsupported",
            "Field '" + field + "' is not a FormLink list on " +
                record->header.signature.to_string() + " records."});
    }

    const auto targetKey = resolveListValue(value);
    if (!targetKey.has_value()) {
        return std::unexpected(Error{
            "ListValueUnresolved",
            "'" + value + "' is not a FormKey and no loaded record has that EditorID."});
    }

    try {
        const auto owner = targetKey->owner();
        const bool encodable = owner.empty() || owner == loaded->plugin.key ||
            std::any_of(loaded->plugin.header.masters.begin(),
                        loaded->plugin.header.masters.end(),
                        [&](const auto& master) { return master.key == owner; });
        if (!encodable) {
            const auto size = fo4::schema::record_formlink_list_size(
                loaded->plugin, recordKey, *descriptor).value_or(0);
            return std::unexpected(Error{
                "ListItemNotFound",
                "No entry matching " + value + " found in " + field + " (" +
                    std::to_string(size) + " item(s))."});
        }
        auto mutated = loaded->plugin;
        const auto removed = fo4::schema::remove_record_formlink_list_elements_matching(
            mutated, recordKey, *descriptor, *targetKey);
        if (removed == 0) {
            const auto size =
                fo4::schema::record_formlink_list_size(loaded->plugin, recordKey, *descriptor)
                    .value_or(0);
            return std::unexpected(Error{
                "ListItemNotFound",
                "No entry matching " + value + " found in " + field + " (" +
                    std::to_string(size) + " item(s))."});
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        const auto newSize =
            fo4::schema::record_formlink_list_size(loaded->plugin, recordKey, *descriptor)
                .value_or(0);
        return nlohmann::json(
            "Removed " + std::to_string(removed) + " entr(y/ies) matching " + value +
            " from " + field + " (now " + std::to_string(newSize) + ") on " +
            recordText + ". SavePlugin to persist.");
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormLinkListWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormLinkEncodingFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
