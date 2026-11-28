#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

namespace {

constexpr std::string_view k_itemsPath = "FLST\\Items";

constexpr std::string_view k_mismatch =
    "The FLST entry tools address FLST\\Items only; the record must be an FLST";

Result<fo4::schema::FormLinkListDescriptor> itemsDescriptor() {
    const auto descriptor = fo4::schema::formlink_list_descriptor(
        fo4::Signature::from_string("FLST"), k_itemsPath);
    if (!descriptor.has_value()) {
        return std::unexpected(Error{
            "FormListUnsupported",
            "The schema does not register the FLST Items list"});
    }
    return *descriptor;
}

std::string indexLabel(std::size_t index) {
    return "Index " + std::to_string(index);
}

Error entryError(std::size_t index, std::string message) {
    return Error{"FormListEntryUnresolved", indexLabel(index) + ": " + std::move(message)};
}

Error indexError(std::size_t index, std::size_t size) {
    return Error{
        "FormListEntryIndexOutOfRange",
        indexLabel(index) + " is outside the list's " + std::to_string(size) + " entr" +
            (size == 1 ? "y" : "ies")};
}

Error statusError(fo4::schema::FormLinkListMutationStatus status, std::size_t index) {
    switch (status) {
        case fo4::schema::FormLinkListMutationStatus::record_missing:
            return Error{"RecordNotFound", "The FLST is not present in this plugin"};
        case fo4::schema::FormLinkListMutationStatus::record_mismatch:
            return Error{"FormListRecordMismatch", std::string(k_mismatch)};
        case fo4::schema::FormLinkListMutationStatus::container_index_out_of_range:
        case fo4::schema::FormLinkListMutationStatus::index_out_of_range:
            return Error{
                "FormListEntryIndexOutOfRange",
                indexLabel(index) + " is not an entry of this list"};
        case fo4::schema::FormLinkListMutationStatus::target_type_mismatch:
            return Error{
                "FormListEntryTypeRefused",
                indexLabel(index) + " names a record type FLST\\Items does not accept"};
        case fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
            return Error{
                "FormListEntryInsertionUnavailable",
                indexLabel(index) + " has no safe insertion boundary in this record"};
        case fo4::schema::FormLinkListMutationStatus::updated:
        case fo4::schema::FormLinkListMutationStatus::unchanged:
            break;
    }
    return Error{"FormListEntryWriteFailed", "The list write reported no result"};
}

}

Result<nlohmann::json> RadiclePluginBackend::getFormListEntries(
    const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (!key) return std::unexpected(key.error());
    const auto descriptor = itemsDescriptor();
    if (!descriptor) return std::unexpected(descriptor.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    const auto* record = fo4::find_record(loaded->plugin, *key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (record->header.signature != fo4::Signature::from_string("FLST")) {
        return std::unexpected(Error{
            "FormListRecordMismatch",
            key->to_string() + " is " + record->header.signature.to_string() + ", not FLST"});
    }

    const auto values =
        fo4::schema::record_formlink_list_values(loaded->plugin, *key, *descriptor);
    if (!values.has_value()) {
        return std::unexpected(Error{
            "FormListReadFailed",
            "The FLST Items list could not be read from " + pluginName});
    }

    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : *values) entries.push_back(support::formKeyJson(entry));
    return nlohmann::json{
        {"formKey", key->to_string()},
        {"count", entries.size()},
        {"entries", std::move(entries)}
    };
}

Result<nlohmann::json> RadiclePluginBackend::addFormListEntry(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto entryText = support::stringArgument(arguments, 2);
    const auto indexValue = support::argumentAt(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (!key) return std::unexpected(key.error());
    if (entryText.empty()) {
        return std::unexpected(Error{
            "FormListEntryMissing",
            "entry must be a FormKey ('000801:Fallout4.esm') or a loaded EditorID"});
    }
    std::optional<std::size_t> requestedIndex;
    if (!indexValue.is_null()) {
        const auto parsed = support::integerValue<std::uint32_t>(indexValue);
        if (!parsed) {
            return std::unexpected(Error{
                "InvalidFormListEntryIndex",
                "index must be a non-negative integer when given"});
        }
        requestedIndex = static_cast<std::size_t>(*parsed);
    }
    const auto descriptor = itemsDescriptor();
    if (!descriptor) return std::unexpected(descriptor.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, *key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (record->header.signature != fo4::Signature::from_string("FLST")) {
        return std::unexpected(Error{
            "FormListRecordMismatch",
            key->to_string() + " is " + record->header.signature.to_string() + ", not FLST"});
    }

    const auto target = resolveListValue(entryText);
    if (!target.has_value()) {
        return std::unexpected(Error{
            "FormListEntryUnresolved",
            "'" + entryText + "' is not a FormKey and no loaded record has that EditorID"});
    }
    if ((*graph_).winner(*target) == nullptr) {
        return std::unexpected(Error{
            "FormListEntryUnresolved",
            "'" + entryText + "' does not resolve in the loaded environment"});
    }

    const auto size = fo4::schema::record_formlink_list_size(loaded->plugin, *key, *descriptor);
    if (!size.has_value()) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    const auto position = requestedIndex.value_or(*size);
    if (position > *size) {
        return std::unexpected(Error{
            "FormListEntryIndexOutOfRange",
            indexLabel(position) + " is past the list's " + std::to_string(*size) +
                " entr" + (*size == 1 ? "y" : "ies")});
    }

    try {
        auto mutated = loaded->plugin;
        support::ensureOwnerMaster(mutated, target->owner(), plugins_);
        const auto status = fo4::schema::insert_record_formlink_list_element(
            mutated, *key, *descriptor, position, *target);
        if (status != fo4::schema::FormLinkListMutationStatus::updated) {
            return std::unexpected(statusError(status, position));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("FormListEntryWriteFailed", error));
    }

    return nlohmann::json(
        "Added " + target->to_string() + " at index " + std::to_string(position) + " of " +
        key->to_string() + " in " + pluginName + " (now " + std::to_string(*size + 1) +
        " entries). SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::setFormListEntry(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto index = support::integerArgument<std::uint32_t>(arguments, 2);
    const auto entryText = support::stringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (!key) return std::unexpected(key.error());
    if (!index) {
        return std::unexpected(Error{
            "InvalidFormListEntryIndex",
            "index must be a non-negative integer"});
    }
    if (entryText.empty()) {
        return std::unexpected(Error{
            "FormListEntryMissing",
            "entry must be a FormKey ('000801:Fallout4.esm') or a loaded EditorID"});
    }
    const auto descriptor = itemsDescriptor();
    if (!descriptor) return std::unexpected(descriptor.error());
    const auto position = static_cast<std::size_t>(*index);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, *key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (record->header.signature != fo4::Signature::from_string("FLST")) {
        return std::unexpected(Error{
            "FormListRecordMismatch",
            key->to_string() + " is " + record->header.signature.to_string() + ", not FLST"});
    }
    const auto size = fo4::schema::record_formlink_list_size(loaded->plugin, *key, *descriptor);
    if (!size.has_value()) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (position >= *size) return std::unexpected(indexError(position, *size));

    const auto target = resolveListValue(entryText);
    if (!target.has_value()) {
        return std::unexpected(entryError(
            position,
            "'" + entryText + "' is not a FormKey and no loaded record has that EditorID"));
    }
    if ((*graph_).winner(*target) == nullptr) {
        return std::unexpected(entryError(
            position, "'" + entryText + "' does not resolve in the loaded environment"));
    }

    try {
        auto mutated = loaded->plugin;
        support::ensureOwnerMaster(mutated, target->owner(), plugins_);
        const auto status = fo4::schema::set_record_formlink_list_element(
            mutated, *key, *descriptor, position, *target);
        if (status == fo4::schema::FormLinkListMutationStatus::unchanged) {
            return nlohmann::json(
                "No change for entry " + std::to_string(position) + " of " + key->to_string() +
                " in " + pluginName + ".");
        }
        if (status != fo4::schema::FormLinkListMutationStatus::updated) {
            return std::unexpected(statusError(status, position));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("FormListEntryWriteFailed", error));
    }

    return nlohmann::json(
        "Set entry " + std::to_string(position) + " of " + key->to_string() + " to " +
        target->to_string() + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeFormListEntry(
    const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto index = support::integerArgument<std::uint32_t>(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (!key) return std::unexpected(key.error());
    if (!index) {
        return std::unexpected(Error{
            "InvalidFormListEntryIndex",
            "index must be a non-negative integer"});
    }
    const auto descriptor = itemsDescriptor();
    if (!descriptor) return std::unexpected(descriptor.error());
    const auto position = static_cast<std::size_t>(*index);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    const auto* record = fo4::find_record(loaded->plugin, *key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (record->header.signature != fo4::Signature::from_string("FLST")) {
        return std::unexpected(Error{
            "FormListRecordMismatch",
            key->to_string() + " is " + record->header.signature.to_string() + ", not FLST"});
    }
    const auto size = fo4::schema::record_formlink_list_size(loaded->plugin, *key, *descriptor);
    if (!size.has_value()) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + key->to_string()});
    }
    if (position >= *size) return std::unexpected(indexError(position, *size));

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::schema::remove_record_formlink_list_element(
            mutated, *key, *descriptor, position);
        if (status != fo4::schema::FormLinkListMutationStatus::updated) {
            return std::unexpected(statusError(status, position));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const fo4::schema::FormLinkListError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormListEntryWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(support::exceptionError("FormListEntryWriteFailed", error));
    }

    return nlohmann::json(
        "Removed entry " + std::to_string(position) + " of " + key->to_string() + " in " +
        pluginName + " (now " + std::to_string(*size - 1) + " entries). SavePlugin to persist.");
}

}
