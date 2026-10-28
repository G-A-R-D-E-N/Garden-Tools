#include <fo4ide/app/AppInteropTarget.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

bool boolArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return false;
    return args[index].get<bool>();
}

std::optional<nlohmann::json> pendingPluginRows(const nlohmann::json& raw) {
    if (raw.is_array()) return raw;
    if (!raw.is_string()) return std::nullopt;
    try {
        auto parsed = nlohmann::json::parse(raw.get<std::string>());
        if (!parsed.is_array()) return std::nullopt;
        return parsed;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::filesystem::file_time_type fileStamp(const std::filesystem::path& path) {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type::min() : stamp;
}

Error withRollback(const Error& original, const Result<void>& restored) {
    if (restored) return original;
    return Error{
        original.type,
        original.message + "; rollback failed: " + restored.error().type + ": " +
            restored.error().message
    };
}

Error combinedRestoreError(const Error& backend, const Error& root) {
    return Error{
        backend.type,
        backend.message + "; trusted-root restore failed: " + root.type + ": " + root.message
    };
}

}

AppInteropTarget::AppInteropTarget(settings::Store& store,
                                   settings::SharedSettings& settings,
                                   platform::Services& platform,
                                   plugin::IPluginBackend& plugins)
    : store_(store), settings_(settings), platform_(platform), plugins_(plugins) {}

Result<nlohmann::json> AppInteropTarget::invoke(std::string_view method,
                                                 const nlohmann::json& args) {
    if (method == "BrowseForMo2Folder") return browseForMo2Folder();
    if (method == "OpenMo2Profile") return openMo2Profile(args);
    if (method == "LoadEnvironment") return loadEnvironment(args);
    if (method == "RefreshTree" || method == "GetPlugins") return getPlugins();
    if (method == "GetMo2Status") return getMo2Status();
    if (method == "ReloadMo2Profile") return reloadMo2Profile(args);
    if (method == "GetChildren") return pluginOperation(method, args, nlohmann::json("[]"));
    if (method == "OpenRecord") return pluginOperation(method, args, nlohmann::json(""));
    if (method == "ScanConflicts") return pluginOperation(method, args, nlohmann::json("Native plugin backend is not connected yet."));
    if (method == "ScanBrokenRefs") return pluginOperation(method, args, nlohmann::json("Native plugin backend is not connected yet."));
    return std::unexpected(Error{"UnknownMethod", "Unknown appInterop RPC method: " + std::string(method)});
}

Result<nlohmann::json> AppInteropTarget::browseForMo2Folder() {
    platform::FileDialogOptions options;
    options.title = "Select your Mod Organizer 2 instance folder";
    const auto configured = settings_.snapshot().mo2InstancePath;
    if (!configured.empty() && std::filesystem::is_directory(configured)) {
        options.initialPath = configured;
    }

    auto selected = platform_.pickFolder(options);
    if (!selected || selected->empty()) return nlohmann::json("");
    if (!std::filesystem::is_directory(*selected / "profiles")) return nlohmann::json("");
    return nlohmann::json(selected->string());
}

Result<nlohmann::json> AppInteropTarget::openMo2Profile(const nlohmann::json& args) {
    const auto instance = std::filesystem::path(support::stringArgument(args, 0));
    if (instance.empty()) return std::unexpected(Error{"Mo2InstanceMissing", "MO2 instance path is empty"});
    auto loaded = loadProfile(instance, boolArgument(args, 1));
    if (!loaded) return std::unexpected(loaded.error());
    return nlohmann::json(nullptr);
}

Result<nlohmann::json> AppInteropTarget::loadEnvironment(const nlohmann::json& args) {
    const auto configured = settings_.snapshot().mo2InstancePath;
    if (configured.empty()) {
        return std::unexpected(Error{"EnvironmentUnavailable", "No MO2 instance is configured"});
    }
    auto loaded = loadProfile(configured, boolArgument(args, 0));
    if (!loaded) return std::unexpected(loaded.error());
    return nlohmann::json(nullptr);
}

Result<void> AppInteropTarget::rejectPendingReloadUnlessDiscarded(bool discardPending) const {
    if (discardPending || !plugins_.capabilities().available) return {};
    auto raw = plugins_.invoke("appInterop.GetPlugins", nlohmann::json::array());
    if (!raw) return std::unexpected(raw.error());
    auto rows = pendingPluginRows(*raw);
    if (!rows) {
        return std::unexpected(Error{
            "PendingStatusUnavailable",
            "Could not inspect pending plugin edits before reloading the MO2 profile"
        });
    }
    std::string pendingList;
    for (const auto& row : *rows) {
        if (!row.is_object() || !row.value("Pending", false)) continue;
        auto name = row.value("Key", std::string{});
        if (name.empty()) name = row.value("Name", std::string{});
        if (name.empty()) continue;
        if (!pendingList.empty()) pendingList += ", ";
        pendingList += name;
    }
    if (pendingList.empty()) return {};
    return std::unexpected(Error{
        "PluginHasPendingChanges",
        "Pending plugin edits would be discarded by reloading the MO2 profile; "
        "save or discard them first: " + pendingList
    });
}

Result<void> AppInteropTarget::restoreLoadedProfile(
    const std::optional<mo2::ProfileSnapshot>& previous,
    const std::filesystem::path& previousRoot) {
    if (!plugins_.capabilities().available) return {};

    Result<nlohmann::json> restoredBackend = nlohmann::json(nullptr);
    if (previous) {
        restoredBackend = plugins_.invoke(
            "environment.LoadMo2", backendEnvironment(*previous, true));
    } else {
        restoredBackend = plugins_.invoke(
            "environment.LoadMo2",
            nlohmann::json{
                {"plugins", nlohmann::json::array()},
                {"discardPending", true}
            });
    }
    auto restoredRoot = plugins_.setTrustedNewPluginRoot(previousRoot);
    if (restoredBackend && restoredRoot) return {};
    if (!restoredBackend && !restoredRoot) {
        return std::unexpected(combinedRestoreError(restoredBackend.error(), restoredRoot.error()));
    }
    if (!restoredBackend) return std::unexpected(restoredBackend.error());
    return std::unexpected(restoredRoot.error());
}

Result<void> AppInteropTarget::loadProfile(const std::filesystem::path& instancePath,
                                           bool discardPending) {
    auto blocked = rejectPendingReloadUnlessDiscarded(discardPending);
    if (!blocked) return std::unexpected(blocked.error());

    auto loaded = mo2::readProfile(instancePath);
    if (!loaded) return std::unexpected(loaded.error());

    std::optional<mo2::ProfileSnapshot> previous;
    {
        std::scoped_lock lock(mutex_);
        previous = snapshot_;
    }
    const auto previousRoot = previous ? previous->instance.overwriteFolder
                                       : std::filesystem::path{};

    if (plugins_.capabilities().available) {
        auto backendLoad = plugins_.invoke(
            "environment.LoadMo2", backendEnvironment(*loaded, discardPending));
        if (!backendLoad) return std::unexpected(backendLoad.error());
        auto outputRoot = plugins_.setTrustedNewPluginRoot(loaded->instance.overwriteFolder);
        if (!outputRoot) {
            return std::unexpected(
                withRollback(outputRoot.error(), restoreLoadedProfile(previous, previousRoot)));
        }
    }

    auto saved = settings_.update([&](settings::Settings& current) {
        const auto previousSettings = current;
        current.mo2InstancePath = loaded->instance.instancePath;
        if (!loaded->instance.gameDataFolder.empty()) current.dataFolder = loaded->instance.gameDataFolder;
        auto stored = store_.save(current);
        if (!stored) current = previousSettings;
        return stored;
    });
    if (!saved) {
        if (!plugins_.capabilities().available) return std::unexpected(saved.error());
        return std::unexpected(
            withRollback(saved.error(), restoreLoadedProfile(previous, previousRoot)));
    }

    auto watched = captureWatchFiles(*loaded);
    const auto loadedAt = std::chrono::system_clock::now();
    std::scoped_lock lock(mutex_);
    snapshot_ = std::move(*loaded);
    watchedFiles_ = std::move(watched);
    loadedAt_ = loadedAt;
    return {};
}

Result<nlohmann::json> AppInteropTarget::getPlugins() const {
    if (plugins_.capabilities().available) {
        auto result = plugins_.invoke("appInterop.GetPlugins", nlohmann::json::array());
        if (result) return result;
    }

    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    if (!snapshot_) return rows.dump();

    for (const auto& plugin : snapshot_->plugins) {
        rows.push_back({
            {"Key", plugin.name},
            {"ConflictStatus", 0},
            {"IsRecordNode", false},
            {"HasChildren", plugins_.capabilities().available},
            {"FilePath", plugin.resolved ? plugin.path.string() : std::string{}},
            {"Pending", false}
        });
    }
    return rows.dump();
}

Result<nlohmann::json> AppInteropTarget::getMo2Status() const {
    std::optional<mo2::ProfileSnapshot> current;
    std::vector<WatchedFile> watched;
    std::chrono::system_clock::time_point loadedAt;
    {
        std::scoped_lock lock(mutex_);
        current = snapshot_;
        watched = watchedFiles_;
        loadedAt = loadedAt_;
    }

    auto pending = pendingPluginNames();
    if (!current) {
        return nlohmann::json{
            {"loaded", false},
            {"configuredInstance", settings_.snapshot().mo2InstancePath.string()},
            {"stale", false},
            {"staleConfig", false},
            {"staleLoadOrder", false},
            {"pendingPlugins", std::move(pending)}
        };
    }

    const auto loadedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        loadedAt.time_since_epoch()).count();
    return nlohmann::json{
        {"loaded", true},
        {"instancePath", current->instance.instancePath.string()},
        {"profile", current->instance.profile},
        {"dataFolder", current->instance.gameDataFolder.string()},
        {"overwriteFolder", current->instance.overwriteFolder.string()},
        {"pluginCount", current->plugins.size()},
        {"missingPluginCount", current->missingPlugins.size()},
        {"warnings", current->warnings},
        {"loadedAtUnixMs", loadedAtMs},
        {"stale", isSnapshotStale(watched)},
        {"staleConfig", isWatchKindStale(watched, WatchKind::config)},
        {"staleLoadOrder", isWatchKindStale(watched, WatchKind::loadOrder)},
        {"nativePluginBackend", plugins_.capabilities().available},
        {"pendingPlugins", std::move(pending)}
    };
}

nlohmann::json AppInteropTarget::pendingPluginNames() const {
    nlohmann::json names = nlohmann::json::array();
    if (!plugins_.capabilities().available) return names;
    auto raw = plugins_.invoke("appInterop.GetPlugins", nlohmann::json::array());
    if (!raw) return names;
    auto rows = pendingPluginRows(*raw);
    if (!rows) return names;
    for (const auto& row : *rows) {
        if (!row.is_object() || !row.value("Pending", false)) continue;
        auto name = row.value("Key", std::string{});
        if (name.empty()) name = row.value("Name", std::string{});
        if (!name.empty()) names.push_back(name);
    }
    return names;
}

Result<nlohmann::json> AppInteropTarget::reloadMo2Profile(const nlohmann::json& args) {
    std::filesystem::path instance;
    {
        std::scoped_lock lock(mutex_);
        if (snapshot_) instance = snapshot_->instance.instancePath;
    }
    if (instance.empty()) instance = settings_.snapshot().mo2InstancePath;
    if (instance.empty()) {
        return std::unexpected(Error{"Mo2InstanceMissing", "No MO2 instance is configured"});
    }

    auto loaded = loadProfile(instance, boolArgument(args, 0));
    if (!loaded) return std::unexpected(loaded.error());
    return getMo2Status();
}

Result<nlohmann::json> AppInteropTarget::pluginOperation(std::string_view method,
                                                          const nlohmann::json& args,
                                                          nlohmann::json fallback) const {
    if (!plugins_.capabilities().available) return fallback;
    return plugins_.invoke("appInterop." + std::string(method), args);
}

nlohmann::json AppInteropTarget::backendEnvironment(const mo2::ProfileSnapshot& snapshot,
                                                     bool discardPending) const {
    nlohmann::json plugins = nlohmann::json::array();
    for (const auto& plugin : snapshot.plugins) {
        plugins.push_back({
            {"name", plugin.name},
            {"path", plugin.path.string()},
            {"source", plugin.source},
            {"resolved", plugin.resolved}
        });
    }

    nlohmann::json mods = nlohmann::json::array();
    for (const auto& mod : snapshot.enabledModsHighToLow) {
        mods.push_back({
            {"name", mod.name},
            {"path", mod.path.string()},
            {"priority", mod.priority}
        });
    }

    nlohmann::json assetRoots = nlohmann::json::array();
    for (const auto& root : snapshot.assetRootsLowToHigh) assetRoots.push_back(root.string());

    return {
        {"instancePath", snapshot.instance.instancePath.string()},
        {"profile", snapshot.instance.profile},
        {"dataFolder", snapshot.instance.gameDataFolder.string()},
        {"overwriteFolder", snapshot.instance.overwriteFolder.string()},
        {"modsFolder", snapshot.instance.modsFolder.string()},
        {"loadOrder", snapshot.loadOrder},
        {"enabledModsHighToLow", std::move(mods)},
        {"plugins", std::move(plugins)},
        {"missingPlugins", snapshot.missingPlugins},
        {"assetRootsLowToHigh", std::move(assetRoots)},
        {"discardPending", discardPending}
    };
}

std::vector<AppInteropTarget::WatchedFile> AppInteropTarget::captureWatchFiles(
    const mo2::ProfileSnapshot& snapshot) const {
    const auto profileRoot = snapshot.instance.instancePath / "profiles" / snapshot.instance.profile;
    return {
        {snapshot.instance.instancePath / "ModOrganizer.ini",
         fileStamp(snapshot.instance.instancePath / "ModOrganizer.ini"),
         WatchKind::config},
        {profileRoot / "plugins.txt", fileStamp(profileRoot / "plugins.txt"), WatchKind::loadOrder},
        {profileRoot / "modlist.txt", fileStamp(profileRoot / "modlist.txt"), WatchKind::loadOrder},
        {profileRoot / "loadorder.txt", fileStamp(profileRoot / "loadorder.txt"), WatchKind::loadOrder},
    };
}

bool AppInteropTarget::isSnapshotStale(const std::vector<WatchedFile>& files) const {
    return isWatchKindStale(files, WatchKind::config) ||
           isWatchKindStale(files, WatchKind::loadOrder);
}

bool AppInteropTarget::isWatchKindStale(const std::vector<WatchedFile>& files, WatchKind kind) const {
    for (const auto& file : files) {
        if (file.kind == kind && fileStamp(file.path) != file.stamp) return true;
    }
    return false;
}

std::optional<mo2::ProfileSnapshot> AppInteropTarget::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

}
