#include <fo4ide/app/AppInteropTarget.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace fo4ide::app {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::filesystem::file_time_type fileStamp(const std::filesystem::path& path) {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    return ec ? std::filesystem::file_time_type::min() : stamp;
}

}

AppInteropTarget::AppInteropTarget(settings::Store& store,
                                   settings::Settings& settings,
                                   platform::Services& platform)
    : store_(store), settings_(settings), platform_(platform) {}

Result<nlohmann::json> AppInteropTarget::invoke(std::string_view method,
                                                 const nlohmann::json& args) {
    if (method == "BrowseForMo2Folder") return browseForMo2Folder();
    if (method == "OpenMo2Profile") return openMo2Profile(args);
    if (method == "LoadEnvironment") return loadEnvironment();
    if (method == "RefreshTree") return getPlugins();
    if (method == "GetPlugins") return getPlugins();
    if (method == "GetMo2Status") return getMo2Status();
    if (method == "ReloadMo2Profile") return reloadMo2Profile();
    if (method == "GetChildren") return nlohmann::json("[]");
    if (method == "OpenRecord") return nlohmann::json("");
    if (method == "ScanConflicts") return nlohmann::json("Native plugin backend is not connected yet.");
    if (method == "ScanBrokenRefs") return nlohmann::json("Native plugin backend is not connected yet.");
    return std::unexpected(Error{"UnknownMethod", "Unknown appInterop RPC method: " + std::string(method)});
}

Result<nlohmann::json> AppInteropTarget::browseForMo2Folder() {
    platform::FileDialogOptions options;
    options.title = "Select your Mod Organizer 2 instance folder";
    if (!settings_.mo2InstancePath.empty() && std::filesystem::is_directory(settings_.mo2InstancePath)) {
        options.initialPath = settings_.mo2InstancePath;
    }

    auto selected = platform_.pickFolder(options);
    if (!selected || selected->empty()) return nlohmann::json("");
    if (!std::filesystem::is_directory(*selected / "profiles")) return nlohmann::json("");
    return nlohmann::json(selected->string());
}

Result<nlohmann::json> AppInteropTarget::openMo2Profile(const nlohmann::json& args) {
    const auto instance = std::filesystem::path(stringArgument(args, 0));
    if (instance.empty()) return std::unexpected(Error{"Mo2InstanceMissing", "MO2 instance path is empty"});
    auto loaded = loadProfile(instance);
    if (!loaded) return std::unexpected(loaded.error());
    return nlohmann::json(nullptr);
}

Result<nlohmann::json> AppInteropTarget::loadEnvironment() {
    if (settings_.mo2InstancePath.empty()) {
        return std::unexpected(Error{"EnvironmentUnavailable", "No MO2 instance is configured and the native plugin backend is not connected yet"});
    }
    auto loaded = loadProfile(settings_.mo2InstancePath);
    if (!loaded) return std::unexpected(loaded.error());
    return nlohmann::json(nullptr);
}

Result<void> AppInteropTarget::loadProfile(const std::filesystem::path& instancePath) {
    auto loaded = mo2::readProfile(instancePath);
    if (!loaded) return std::unexpected(loaded.error());

    settings_.mo2InstancePath = loaded->instance.instancePath;
    if (!loaded->instance.gameDataFolder.empty()) settings_.dataFolder = loaded->instance.gameDataFolder;
    auto saved = store_.save(settings_);
    if (!saved) return std::unexpected(saved.error());

    auto watched = captureWatchFiles(*loaded);
    const auto loadedAt = std::chrono::system_clock::now();
    std::scoped_lock lock(mutex_);
    snapshot_ = std::move(*loaded);
    watchedFiles_ = std::move(watched);
    loadedAt_ = loadedAt;
    return {};
}

Result<nlohmann::json> AppInteropTarget::getPlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    if (!snapshot_) return rows.dump();

    for (const auto& plugin : snapshot_->plugins) {
        rows.push_back({
            {"Key", plugin.name},
            {"ConflictStatus", 0},
            {"IsRecordNode", false},
            {"HasChildren", false},
            {"FilePath", plugin.resolved ? plugin.path.string() : std::string{}}
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

    if (!current) {
        return nlohmann::json{
            {"loaded", false},
            {"configuredInstance", settings_.mo2InstancePath.string()},
            {"stale", false}
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
        {"loadedAtUnixMs", loadedAtMs},
        {"stale", isSnapshotStale(watched)}
    };
}

Result<nlohmann::json> AppInteropTarget::reloadMo2Profile() {
    std::filesystem::path instance;
    {
        std::scoped_lock lock(mutex_);
        if (snapshot_) instance = snapshot_->instance.instancePath;
    }
    if (instance.empty()) instance = settings_.mo2InstancePath;
    if (instance.empty()) {
        return std::unexpected(Error{"Mo2InstanceMissing", "No MO2 instance is configured"});
    }

    auto loaded = loadProfile(instance);
    if (!loaded) return std::unexpected(loaded.error());
    return getMo2Status();
}

std::vector<AppInteropTarget::WatchedFile> AppInteropTarget::captureWatchFiles(
    const mo2::ProfileSnapshot& snapshot) const {
    const auto profileRoot = snapshot.instance.instancePath / "profiles" / snapshot.instance.profile;
    const std::vector<std::filesystem::path> paths{
        snapshot.instance.instancePath / "ModOrganizer.ini",
        profileRoot / "plugins.txt",
        profileRoot / "modlist.txt",
        profileRoot / "loadorder.txt"
    };

    std::vector<WatchedFile> watched;
    watched.reserve(paths.size());
    for (const auto& path : paths) watched.emplace_back(path, fileStamp(path));
    return watched;
}

bool AppInteropTarget::isSnapshotStale(const std::vector<WatchedFile>& files) const {
    for (const auto& [path, stamp] : files) {
        if (fileStamp(path) != stamp) return true;
    }
    return false;
}

std::optional<mo2::ProfileSnapshot> AppInteropTarget::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

}
