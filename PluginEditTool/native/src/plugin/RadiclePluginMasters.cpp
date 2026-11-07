#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fo4/plugin/master_usage.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/onam.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::unexpected<Error> readOnlyError(std::string_view name) {
    return std::unexpected(Error{
        "PluginReadOnly",
        "Refusing to modify protected or game-Data plugin: " + std::string(name)
    });
}

std::string incompleteUsageReason(const fo4::Plugin& plugin, const fo4::MasterUsage& usage) {
    std::vector<std::string> reasons;
    if (usage.records_with_incomplete_vmad != 0) {
        reasons.push_back(std::to_string(usage.records_with_incomplete_vmad) +
                          " record(s) carry VMAD data Radicle cannot fully scan");
    }
    if (usage.undecodable_records != 0) {
        std::string names;
        for (const auto* record : fo4::collect_records(plugin)) {
            if (!record->undecodable || record->header.signature == fo4::sig_tes4) continue;
            if (!names.empty()) names += ", ";
            names += record->header.signature.to_string() + " " + record->form_key.to_string();
        }
        reasons.push_back(std::to_string(usage.undecodable_records) +
                          " undecodable record(s) cannot be scanned: " + names);
    }
    std::string reason;
    for (const auto& part : reasons) {
        if (!reason.empty()) reason += "; ";
        reason += part;
    }
    return reason + "; no masters were removed";
}

}

Result<nlohmann::json> RadiclePluginBackend::reorderMasters(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    const auto orderText = support::stringArgument(arguments, 1);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (orderText.empty()) return std::unexpected(Error{"MasterOrderMissing", "Master order is empty"});

    nlohmann::json requestedJson;
    try {
        requestedJson = nlohmann::json::parse(orderText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidMasterOrder", error.what()});
    }
    if (!requestedJson.is_array()) {
        return std::unexpected(Error{"InvalidMasterOrder", "Master order must be a JSON array"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }
    if (requestedJson.size() != loaded->plugin.header.masters.size()) {
        return std::unexpected(Error{"InvalidMasterOrder", "Master order must contain every existing master exactly once"});
    }

    std::vector<fo4::MasterDescriptor> requested;
    requested.reserve(requestedJson.size());
    std::set<fo4::PluginKey> seen;
    for (const auto& value : requestedJson) {
        if (!value.is_string() || value.get<std::string>().empty()) {
            return std::unexpected(Error{"InvalidMasterOrder", "Master names must be non-empty strings"});
        }
        auto key = fo4::PluginKey::from_filename(value.get<std::string>());
        if (!seen.insert(key).second) {
            return std::unexpected(Error{"InvalidMasterOrder", "Master order contains a duplicate: " + std::string(key.filename())});
        }

        const auto current = std::find_if(
            loaded->plugin.header.masters.begin(), loaded->plugin.header.masters.end(),
            [&](const auto& master) { return master.key == key; });
        if (current == loaded->plugin.header.masters.end()) {
            return std::unexpected(Error{"InvalidMasterOrder", "Master order contains an unknown master: " + std::string(key.filename())});
        }
        requested.push_back({std::move(key), current->kind});
    }

    for (const auto& master : loaded->plugin.header.masters) {
        if (!seen.contains(master.key)) {
            return std::unexpected(Error{"InvalidMasterOrder", "Master order omitted: " + std::string(master.key.filename())});
        }
    }

    try {
        auto remapped = fo4::remap_plugin_masters(loaded->plugin, std::move(requested));
        stagePluginMutationLocked(*loaded, std::move(remapped));
        return nlohmann::json(
            "Reordered masters in " + name + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MasterReorderFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::cleanUnusedMasters(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    bool apply = true;
    if (arguments.is_array() && arguments.size() > 1 && !support::tryBoolArgument(arguments, 1, apply)) {
        return std::unexpected(Error{"InvalidApplyFlag", "Apply flag must be boolean"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    try {
        const auto usage = fo4::scan_master_usage(loaded->plugin);
        if (!usage.complete()) {
            return std::unexpected(Error{"MasterUsageIncomplete", incompleteUsageReason(loaded->plugin, usage)});
        }
        auto kept = fo4::used_masters(loaded->plugin, usage);
        std::vector<std::string> removed;
        for (const auto& master : loaded->plugin.header.masters) {
            const bool keep = std::any_of(kept.begin(), kept.end(),
                                          [&](const auto& item) { return item.key == master.key; });
            if (!keep) removed.emplace_back(master.key.filename());
        }
        if (removed.empty()) {
            return nlohmann::json("No unused masters in " + name + ".");
        }
        std::string list;
        for (const auto& item : removed) list += (list.empty() ? "" : ", ") + item;
        if (!apply) {
            return nlohmann::json("DRY RUN: would remove unused master(s) from " + name + ": " + list + ".");
        }
        auto remapped = fo4::remap_plugin_masters(loaded->plugin, std::move(kept));
        stagePluginMutationLocked(*loaded, std::move(remapped));
        return nlohmann::json(
            "Removed unused master(s) from " + name + ": " + list + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CleanMastersFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::repairHedrCount(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk", "Plugin was modified on disk after it was loaded; reload it before writing: " + loaded->path.string()});
    }
    try {
        auto repaired = loaded->plugin;
        fo4::repair_hedr_record_count(repaired);
        if (repaired.header.record_count == loaded->plugin.header.record_count) {
            return nlohmann::json("HEDR count in " + name + " is already up to date.");
        }
        const auto count = repaired.header.record_count;
        stagePluginMutationLocked(*loaded, std::move(repaired));
        return nlohmann::json("Repaired HEDR count in " + name + " (" + std::to_string(count) + "). SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RepairHedrCountFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::updateOnam(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);
    if (!fo4::is_master_plugin(loaded->plugin.header.tes4_flags, loaded->plugin.key)) {
        return nlohmann::json(name + " is not master-flagged; the game ignores ONAM there.");
    }

    try {
        auto rebuilt = loaded->plugin;
        fo4::rebuild_tes4_onam(rebuilt);
        if (rebuilt.header.overrides == loaded->plugin.header.overrides) {
            return nlohmann::json("ONAM in " + name + " is already up to date.");
        }
        const auto count = rebuilt.header.overrides.size();
        stagePluginMutationLocked(*loaded, std::move(rebuilt));
        return nlohmann::json("Rebuilt ONAM in " + name + " (" + std::to_string(count) +
                              " override(s)). SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"UpdateOnamFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setLight(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    bool light = false;
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (!support::tryBoolArgument(arguments, 1, light)) {
        return std::unexpected(Error{"InvalidLightFlag", "Light flag must be boolean"});
    }

    OpLock lock(*this, OpLock::Mode::Lifecycle);
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);

    try {
        const auto lowered = light ? fo4::lowered_light_next_form_id(loaded->plugin) : std::nullopt;
        auto mutated = fo4::set_plugin_light_flag(loaded->plugin, light);
        loaded->plugin = std::move(mutated);
        dropBorrowedEnvironmentLocked();
        loaded->dirty = true;
        ++mutationEpoch_;

        std::vector<fo4::MasterDescriptor> known;
        known.reserve(plugins_.size());
        for (const auto& item : plugins_) {
            known.push_back({
                item.plugin.key,
                fo4::classify_plugin_kind(item.plugin.header.tes4_flags, item.plugin.key),
            });
        }
        for (auto& item : plugins_) fo4::apply_master_kinds(item.plugin, known);
        markGraphDirtyLocked();

        const auto loweredNote = lowered
            ? "; lowered HEDR nextFormID to " + std::format("0x{:03X}", *lowered)
            : std::string{};
        return nlohmann::json(
            light ? "Set the ESL flag on " + name + loweredNote + ". Save Plugin to persist it."
                  : "Cleared the ESL flag on " + name + ". Save Plugin to persist it.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetLightFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::saveMastersPlugin(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (!isEditable(*loaded)) return readOnlyError(loaded->name);
    // setLight() mutates the cache and persists here, so this is the choke
    // point that must refuse to clobber externally modified files.
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before saving: " +
                loaded->path.string()
        });
    }

    if (const auto persisted = persistPluginLocked(*loaded, loaded->path); !persisted) {
        return std::unexpected(persisted.error());
    }
    return nlohmann::json("Saved " + name + ".");
}

}
