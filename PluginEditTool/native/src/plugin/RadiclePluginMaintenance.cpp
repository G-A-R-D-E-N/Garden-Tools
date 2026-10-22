#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/environment/environment.hpp>
#include <fo4/forms/copy_as_new.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/maintenance.hpp>
#include <fo4/plugin/record_flags.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_link_mixed.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;
using support::topLevelRecord;

namespace {

constexpr std::uint32_t kInitiallyDisabled = 0x00000800u;

std::string maintenanceStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool maintenanceBoolArgument(const nlohmann::json& args,
                             std::size_t index,
                             bool fallback = false) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return fallback;
    return args[index].get<bool>();
}

std::size_t maintenanceLimitArgument(const nlohmann::json& args,
                                     std::size_t index,
                                     std::size_t fallback) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return fallback;
    const auto value = args[index].get<std::int64_t>();
    return value > 0 ? static_cast<std::size_t>(value) : fallback;
}

std::string maintenanceLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

fo4::Record* mutableRecord(fo4::Plugin& plugin, const fo4::FormKey& key) {
    fo4::Record* found = nullptr;
    fo4::visit_records(plugin, [&](fo4::Record& record) {
        if (found == nullptr && record.header.signature != fo4::sig_tes4 && record.form_key == key) {
            found = &record;
        }
    });
    return found;
}

}  // namespace

Result<fo4::FormKey> RadiclePluginBackend::copyRecordAsNewLocked(
    fo4::Plugin& mutated,
    const LoadedPlugin& source,
    const fo4::FormKey& key,
    std::optional<std::string_view> newEditorId) const {
    fo4::forms::RecordCopyNewOptions options;
    if (newEditorId.has_value() && !newEditorId->empty()) options.editor_id = std::string(*newEditorId);
    const auto result = fo4::forms::copy_record_as_new(mutated, source.plugin, key, options);
    if (result.status == fo4::forms::RecordCopyNewStatus::created) {
        if (newEditorId.has_value() && newEditorId->empty()) {
            if (auto* created = mutableRecord(mutated, result.new_record)) {
                std::erase_if(created->subrecords,
                              [](const fo4::Subrecord& subrecord) { return subrecord.signature == fo4::sig_edid; });
                created->original_range.clear();
            }
        }
        return result.new_record;
    }
    const auto targetName = std::string(mutated.key.filename());
    if (result.status == fo4::forms::RecordCopyNewStatus::unsupported_signature) {
        return std::unexpected(Error{
            "CopyAsNewUnsupportedType",
            "CopyAsNewRecord does not copy this record type (xEdit refuses it): " + key.to_string()
        });
    }
    if (result.status == fo4::forms::RecordCopyNewStatus::parent_cell_missing) {
        return std::unexpected(Error{
            "TargetParentCellMissing",
            "The placed reference's parent CELL is not present in " + targetName +
            "; copy or override the parent CELL into the target first"
        });
    }
    if (result.status == fo4::forms::RecordCopyNewStatus::parent_dial_missing) {
        return std::unexpected(Error{
            "TargetParentDialMissing",
            "The INFO's parent QUST/DIAL hierarchy is not present in " + targetName
        });
    }
    return std::unexpected(Error{
        "RecordNotFound",
        "Record is not present in " + source.name + ": " + key.to_string()
    });
}

Result<nlohmann::json> RadiclePluginBackend::invokeLegacyBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    if (method == "CopyAsNewRecord") {
        const auto sourcePluginName = maintenanceStringArgument(arguments, 0);
        const auto recordId = maintenanceStringArgument(arguments, 1);
        const auto targetPluginName = maintenanceStringArgument(arguments, 2);
        const auto requestedEditorId = maintenanceStringArgument(arguments, 3);
        if (recordId.empty() || targetPluginName.empty()) {
            return std::unexpected(Error{
                "CopyAsNewArgumentsMissing",
                "CopyAsNewRecord requires source record and target plugin"
            });
        }

        OpLock lock(*this, OpLock::Mode::Write, {targetPluginName, sourcePluginName});
        auto* target = findPlugin(targetPluginName);
        if (target == nullptr) {
            return std::unexpected(Error{
                "TargetPluginNotLoaded",
                "Target plugin is not loaded yet: " + targetPluginName
            });
        }
        if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

        const LoadedPlugin* source = nullptr;
        fo4::FormKey sourceKey;
        bool keyResolved = false;
        if (recordId.find(':') != std::string::npos) {
            try {
                sourceKey = fo4::FormKey::parse(recordId);
                keyResolved = true;
            } catch (const std::exception& error) {
                return std::unexpected(Error{"InvalidFormKey", error.what()});
            }
        }

        if (!sourcePluginName.empty()) {
            source = findPlugin(sourcePluginName);
            if (source == nullptr) {
                return std::unexpected(Error{
                    "SourcePluginNotLoaded",
                    "Source plugin is not loaded: " + sourcePluginName
                });
            }
            if (!keyResolved) {
                const auto wanted = maintenanceLower(recordId);
                fo4::visit_records(source->plugin, [&](const fo4::Record& record) {
                    if (!keyResolved && maintenanceLower(editorId(record)) == wanted) {
                        sourceKey = record.form_key;
                        keyResolved = true;
                    }
                });
            }
        } else {
            if (keyResolved) {
                for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
                    if (fo4::find_record(it->plugin, sourceKey) == nullptr) continue;
                    source = &*it;
                    break;
                }
            } else {
                const auto wanted = maintenanceLower(recordId);
                for (auto it = plugins_.rbegin(); it != plugins_.rend() && source == nullptr; ++it) {
                    fo4::visit_records(it->plugin, [&](const fo4::Record& record) {
                        if (source == nullptr && maintenanceLower(editorId(record)) == wanted) {
                            source = &*it;
                            sourceKey = record.form_key;
                            keyResolved = true;
                        }
                    });
                }
            }
        }

        if (!keyResolved || source == nullptr) {
            return std::unexpected(Error{
                "SourceRecordNotFound",
                "Could not find source record '" + recordId + "'"
            });
        }
        const auto* sourceRecord = topLevelRecord(source->plugin, sourceKey);
        if (sourceRecord == nullptr) {
            if (fo4::find_record(source->plugin, sourceKey) == nullptr) {
                return std::unexpected(Error{
                    "SourceRecordNotFound",
                    "Could not find " + sourceKey.to_string() + " in " + source->name
                });
            }
            try {
                auto mutated = target->plugin;
                const auto* nestedSourceRecord = fo4::find_record(source->plugin, sourceKey);
                fo4::FormKey newKey;
                if (nestedSourceRecord != nullptr &&
                    nestedSourceRecord->header.signature == fo4::Signature::from_string("INFO")) {
                    const auto result = fo4::forms::copy_info_as_new(
                        mutated, source->plugin, sourceKey);
                    switch (result.status) {
                        case fo4::forms::InfoCopyNewStatus::source_missing:
                            return std::unexpected(Error{
                                "SourceRecordNotFound",
                                "Could not find " + sourceKey.to_string() + " in " + source->name});
                        case fo4::forms::InfoCopyNewStatus::unsupported_signature:
                            return std::unexpected(Error{
                                "SourceContainmentUnsupported",
                                "CopyAsNewRecord does not support this nested INFO source"});
                        case fo4::forms::InfoCopyNewStatus::parent_dial_missing:
                            return std::unexpected(Error{
                                "TargetParentDialMissing",
                                "The INFO's parent QUST/DIAL hierarchy is not present in " +
                                    targetPluginName});
                        case fo4::forms::InfoCopyNewStatus::created:
                            newKey = result.new_info;
                            break;
                    }
                } else {
                    const auto result = fo4::forms::copy_placed_reference_as_new(
                        mutated, source->plugin, sourceKey);
                    switch (result.status) {
                        case fo4::forms::PlacedReferenceCopyNewStatus::source_missing:
                            return std::unexpected(Error{
                                "SourceRecordNotFound",
                                "Could not find " + sourceKey.to_string() + " in " + source->name});
                        case fo4::forms::PlacedReferenceCopyNewStatus::unsupported_signature:
                            return std::unexpected(Error{
                                "SourceContainmentUnsupported",
                                "CopyAsNewRecord supports contained REFR/ACHR placed references in a CELL only; "
                                "other contained records (DIAL and other unproven contained families) are not duplicated"});
                        case fo4::forms::PlacedReferenceCopyNewStatus::parent_cell_missing:
                            return std::unexpected(Error{
                                "TargetParentCellMissing",
                                "The placed reference's parent CELL is not present in " + targetPluginName +
                                "; copy or override the parent CELL into the target first"});
                        case fo4::forms::PlacedReferenceCopyNewStatus::created:
                            newKey = result.new_reference;
                            break;
                    }
                }

                auto* allocated = mutableRecord(mutated, newKey);
                const auto originalEditorId =
                    allocated != nullptr ? editorId(*allocated) : std::string{};
                const auto newEditorId = !requestedEditorId.empty()
                    ? requestedEditorId
                    : (originalEditorId.empty() ? std::string{} : originalEditorId + "DUP");
                if (!newEditorId.empty() &&
                    !fo4::set_record_editor_id(mutated, newKey, newEditorId)) {
                    return std::unexpected(Error{
                        "CopyAsNewEditorIdFailed",
                        "Could not set EditorID on the duplicated record"});
                }

                stagePluginMutationLocked(*target, std::move(mutated));
                return nlohmann::json(
                    "Copied " + sourceKey.to_string() + " into " + targetPluginName +
                    " as NEW record " + newKey.to_string() +
                    (newEditorId.empty() ? std::string{} : " ('" + newEditorId + "')."));
            } catch (const std::exception& error) {
                return std::unexpected(Error{"CopyAsNewFailed", error.what()});
            }
        }

        try {
            auto mutated = target->plugin;
            const auto originalEditorId = editorId(*sourceRecord);
            const auto newEditorId = !requestedEditorId.empty()
                ? requestedEditorId
                : (originalEditorId.empty() ? std::string{} : originalEditorId + "DUP");
            const auto newKey = copyRecordAsNewLocked(
                mutated, *source, sourceKey,
                newEditorId.empty() ? std::nullopt : std::optional<std::string_view>{newEditorId});
            if (!newKey) return std::unexpected(newKey.error());

            stagePluginMutationLocked(*target, std::move(mutated));
            return nlohmann::json(
                "Copied " + sourceKey.to_string() + " into " + targetPluginName +
                " as NEW record " + newKey->to_string() +
                (newEditorId.empty() ? std::string{} : " ('" + newEditorId + "')."));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"CopyAsNewFailed", error.what()});
        }
    }

    if (method == "CleanPlugin") {
        const auto pluginName = maintenanceStringArgument(arguments, 0);
        if (pluginName.empty()) {
            return std::unexpected(Error{"PluginMissing", "CleanPlugin requires a plugin name"});
        }

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
        }
        if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

        try {
            auto mutated = loaded->plugin;
            std::size_t undeleted = 0;
            std::size_t skipped = 0;
            std::vector<fo4::FormKey> references;
            fo4::visit_records(mutated, [&](fo4::Record& record) {
                if (record.header.signature == fo4::sig_tes4 || !record.header.flags.is_deleted()) {
                    return;
                }
                if (record.undecodable) {
                    ++skipped;
                    return;
                }
                if (fo4::is_placed_reference_signature(record.header.signature)) {
                    references.push_back(record.form_key);
                    return;
                }
                const auto bits = (record.header.flags.value() & ~fo4::RecordFlags::deleted) |
                                  kInitiallyDisabled;
                record.header.flags = fo4::RecordFlags{bits};
                record.original_range.clear();
                record.decompressed_payload.clear();
                record.header.data_size = 0;
                record.uncompressed_size = 0;
                ++undeleted;
            });

            std::size_t injected = 0;
            std::size_t incomplete = 0;
            if (!references.empty()) {
                std::vector<fo4::Plugin> order;
                order.reserve(plugins_.size());
                for (const auto& item : plugins_) order.push_back(item.plugin);
                const auto loadOrder = fo4::env::Environment::from_plugins(std::move(order));
                for (const auto& key : references) {
                    switch (fo4::undelete_and_disable_reference(mutated, loadOrder, key)) {
                        case fo4::UndeleteStatus::undeleted:
                            ++undeleted;
                            break;
                        case fo4::UndeleteStatus::injected_record:
                            ++injected;
                            break;
                        case fo4::UndeleteStatus::incomplete_reference:
                            ++incomplete;
                            break;
                        case fo4::UndeleteStatus::not_deleted:
                        case fo4::UndeleteStatus::record_missing:
                        case fo4::UndeleteStatus::header_record:
                        case fo4::UndeleteStatus::not_a_reference:
                            break;
                    }
                }
            }

            std::string notes;
            if (skipped != 0) notes += ", skipped " + std::to_string(skipped) + " undecodable";
            if (injected != 0) notes += ", refused " + std::to_string(injected) + " injected";
            if (incomplete != 0) notes += ", refused " + std::to_string(incomplete) + " incomplete";
            if (undeleted == 0) {
                return nlohmann::json(
                    "Cleaned " + pluginName + ": undeleted + disabled 0 record(s)" + notes + ".");
            }
            mutated.original_range.clear();
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "Cleaned " + pluginName + ": undeleted + disabled " +
                std::to_string(undeleted) + " record(s)" + notes + ".");
        } catch (const std::exception& error) {
            return std::unexpected(Error{"CleanPluginFailed", error.what()});
        }
    }

    if (method == "RemoveIdenticalToMaster") {
        const auto pluginName = maintenanceStringArgument(arguments, 0);
        const bool apply = maintenanceBoolArgument(arguments, 1, false);
        const auto limit = maintenanceLimitArgument(arguments, 2, 2000);
        if (pluginName.empty()) {
            return std::unexpected(Error{
                "PluginMissing",
                "RemoveIdenticalToMaster requires a plugin name"
            });
        }

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
        }
        if (apply) {
            if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
        }

        std::size_t currentIndex = plugins_.size();
        for (std::size_t index = 0; index < plugins_.size(); ++index) {
            if (&plugins_[index] == loaded) {
                currentIndex = index;
                break;
            }
        }
        if (currentIndex == plugins_.size()) {
            return std::unexpected(Error{"PluginStateInvalid", "Loaded plugin index could not be resolved"});
        }

        struct ItmRecord {
            fo4::FormKey key;
            std::string editorId;
            std::string type;
            std::string previousPlugin;
        };
        std::vector<ItmRecord> identical;
        std::size_t scanned = 0;

        try {
            fo4::visit_records(loaded->plugin, [&](const fo4::Record& record) {
                if (record.header.signature != fo4::sig_tes4 && !record.form_key.owner().empty()) ++scanned;
            });
            std::vector<const fo4::Plugin*> loadOrder;
            for (std::size_t index = 0; index <= currentIndex; ++index) {
                loadOrder.push_back(&plugins_[index].plugin);
            }
            for (const auto& item : fo4::find_identical_to_master(loadOrder, currentIndex, limit)) {
                const auto* record = fo4::find_record(loaded->plugin, item.key);
                identical.push_back({
                    .key = item.key,
                    .editorId = record == nullptr ? std::string{} : editorId(*record),
                    .type = item.signature.to_string(),
                    .previousPlugin = std::string(item.previous.filename()),
                });
            }

            if (identical.empty()) {
                return nlohmann::json(
                    "No identical-to-master records in " + pluginName + " (" +
                    std::to_string(scanned) + " record(s) checked).");
            }

            if (!apply) {
                std::string preview =
                    "DRY RUN: " + std::to_string(identical.size()) +
                    " identical-to-master record(s) in " + pluginName + " of " +
                    std::to_string(scanned) + " checked.";
                const auto shown = std::min<std::size_t>(identical.size(), 50);
                for (std::size_t index = 0; index < shown; ++index) {
                    const auto& item = identical[index];
                    preview += "\n  " + item.type + " " + item.key.to_string();
                    if (!item.editorId.empty()) preview += " '" + item.editorId + "'";
                    preview += " -- identical to " + item.previousPlugin;
                }
                if (identical.size() > shown) {
                    preview += "\n  ... and " + std::to_string(identical.size() - shown) + " more";
                }
                preview += "\nRe-run with apply=true to remove them.";
                return nlohmann::json(std::move(preview));
            }

            auto mutated = loaded->plugin;
            std::vector<fo4::FormKey> keys;
            for (const auto& item : identical) keys.push_back(item.key);
            const auto removed = fo4::remove_records(mutated, keys);
            if (removed != 0) {
                stagePluginMutationLocked(*loaded, std::move(mutated));
            }
            return nlohmann::json(
                "Removed " + std::to_string(removed) +
                " identical-to-master record(s) from " + pluginName + ".");
        } catch (const std::exception& error) {
            return std::unexpected(Error{"RemoveIdenticalToMasterFailed", error.what()});
        }
    }

    return invokeBackend(method, arguments);
}

}  // namespace fo4ide::plugin
