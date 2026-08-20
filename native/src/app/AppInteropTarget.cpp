#include <fo4ide/app/AppInteropTarget.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace fo4ide::app {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
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

    std::scoped_lock lock(mutex_);
    snapshot_ = std::move(*loaded);
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

std::optional<mo2::ProfileSnapshot> AppInteropTarget::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

}
