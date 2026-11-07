#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <fo4ide/mo2/Mo2Profile.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::app {

class AppInteropTarget final : public rpc::Target {
public:
    AppInteropTarget(settings::Store& store,
                     settings::SharedSettings& settings,
                     platform::Services& platform,
                     plugin::IPluginBackend& plugins);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

    std::optional<mo2::ProfileSnapshot> snapshot() const;

private:
    enum class WatchKind { config, loadOrder };
    struct WatchedFile {
        std::filesystem::path path;
        std::filesystem::file_time_type stamp{};
        WatchKind kind{WatchKind::loadOrder};
    };

    Result<nlohmann::json> browseForMo2Folder();
    Result<nlohmann::json> openMo2Profile(const nlohmann::json& args);
    Result<nlohmann::json> loadEnvironment(const nlohmann::json& args);
    Result<nlohmann::json> getPlugins() const;
    Result<nlohmann::json> getMo2Status() const;
    Result<nlohmann::json> reloadMo2Profile(const nlohmann::json& args);
    Result<void> rejectPendingReloadUnlessDiscarded(bool discardPending) const;
    Result<void> loadProfile(const std::filesystem::path& instancePath, bool discardPending);
    Result<void> restoreLoadedProfile(const std::optional<mo2::ProfileSnapshot>& previous,
                                      const std::filesystem::path& previousRoot);
    Result<nlohmann::json> pluginOperation(std::string_view method,
                                           const nlohmann::json& args,
                                           nlohmann::json fallback) const;

    nlohmann::json backendEnvironment(const mo2::ProfileSnapshot& snapshot,
                                      bool discardPending) const;
    nlohmann::json pendingPluginNames() const;
    std::vector<WatchedFile> captureWatchFiles(const mo2::ProfileSnapshot& snapshot) const;
    bool isSnapshotStale(const std::vector<WatchedFile>& files) const;
    bool isWatchKindStale(const std::vector<WatchedFile>& files, WatchKind kind) const;

    settings::Store& store_;
    settings::SharedSettings& settings_;
    platform::Services& platform_;
    plugin::IPluginBackend& plugins_;
    mutable std::mutex mutex_;
    std::optional<mo2::ProfileSnapshot> snapshot_;
    std::vector<WatchedFile> watchedFiles_;
    std::chrono::system_clock::time_point loadedAt_{};
};

}
