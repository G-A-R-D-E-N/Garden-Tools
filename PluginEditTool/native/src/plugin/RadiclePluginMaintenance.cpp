#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
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

std::string recordEditorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

const fo4::Record* topLevelRecord(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr && record->form_key == key) return record;
        }
    }
    return nullptr;
}

bool eraseTopLevelRecord(fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (auto it = group.children.begin(); it != group.children.end(); ++it) {
            const auto* record = std::get_if<fo4::Record>(&*it);
            if (record == nullptr || record->form_key != key) continue;
            group.children.erase(it);
            plugin.original_range.clear();
            plugin.original_structure_fingerprint = 0;
            return true;
        }
    }
    return false;
}

bool sameSemanticRecord(const fo4::Record& left, const fo4::Record& right) {
    if (left.header.signature != right.header.signature ||
        left.header.flags != right.header.flags ||
        left.subrecords.size() != right.subrecords.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.subrecords.size(); ++index) {
        if (left.subrecords[index].signature != right.subrecords[index].signature ||
            left.subrecords[index].data != right.subrecords[index].data) {
            return false;
        }
    }
    return true;
}

bool previousVersionNormalizesEqual(const fo4::Plugin& current,
                                    const fo4::Plugin& previous,
                                    const fo4::FormKey& key) {
    const auto* currentRecord = topLevelRecord(current, key);
    if (currentRecord == nullptr) return false;

    auto normalized = current;
    const auto copied = fo4::copy_record_as_override(normalized, previous, key, true);
    if (copied != fo4::RecordCopyStatus::replaced &&
        copied != fo4::RecordCopyStatus::already_exists) {
        return false;
    }
    const auto* previousNormalized = topLevelRecord(normalized, key);
    return previousNormalized != nullptr && sameSemanticRecord(*currentRecord, *previousNormalized);
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::invokeLegacyBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    if (method == "CleanPlugin") {
        const auto pluginName = maintenanceStringArgument(arguments, 0);
        if (pluginName.empty()) {
            return std::unexpected(Error{"PluginMissing", "CleanPlugin requires a plugin name"});
        }

        std::scoped_lock lock(mutex_);
        auto* loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
        }
        if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

        try {
            auto mutated = loaded->plugin;
            std::size_t undeleted = 0;
            fo4::visit_records(mutated, [&](fo4::Record& record) {
                if (record.header.signature == fo4::sig_tes4 || !record.header.flags.is_deleted()) return;
                const auto bits = (record.header.flags.value() & ~fo4::RecordFlags::deleted) |
                                  kInitiallyDisabled;
                record.header.flags = fo4::RecordFlags{bits};
                record.original_range.clear();
                record.decompressed_payload.clear();
                record.header.data_size = 0;
                record.uncompressed_size = 0;
                ++undeleted;
            });
            if (undeleted == 0) {
                return nlohmann::json("Cleaned " + pluginName + ": undeleted + disabled 0 record(s).");
            }
            mutated.original_range.clear();
            fo4::write_plugin_file(mutated, loaded->path);
            loaded->plugin = std::move(mutated);
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
            rebuildGraphLocked();
            return nlohmann::json(
                "Cleaned " + pluginName + ": undeleted + disabled " +
                std::to_string(undeleted) + " record(s).");
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

        std::scoped_lock lock(mutex_);
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
            for (const auto& group : loaded->plugin.groups) {
                if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
                for (const auto& child : group.children) {
                    const auto* record = std::get_if<fo4::Record>(&child);
                    if (record == nullptr || record->form_key.owner().empty()) continue;
                    ++scanned;

                    const LoadedPlugin* previous = nullptr;
                    for (std::size_t index = currentIndex; index-- > 0;) {
                        if (fo4::find_record(plugins_[index].plugin, record->form_key) == nullptr) continue;
                        previous = &plugins_[index];
                        break;
                    }
                    if (previous == nullptr) continue;
                    if (!previousVersionNormalizesEqual(
                            loaded->plugin, previous->plugin, record->form_key)) {
                        continue;
                    }

                    identical.push_back({
                        .key = record->form_key,
                        .editorId = recordEditorId(*record),
                        .type = record->header.signature.to_string(),
                        .previousPlugin = previous->name,
                    });
                    if (identical.size() >= limit) break;
                }
                if (identical.size() >= limit) break;
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
            std::size_t removed = 0;
            for (const auto& item : identical) {
                if (eraseTopLevelRecord(mutated, item.key)) ++removed;
            }
            if (removed != 0) {
                fo4::write_plugin_file(mutated, loaded->path);
                loaded->plugin = std::move(mutated);
                std::error_code ec;
                loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
                rebuildGraphLocked();
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
