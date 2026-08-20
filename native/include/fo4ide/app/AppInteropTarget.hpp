#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <fo4ide/mo2/Mo2Profile.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::app {

class AppInteropTarget final : public rpc::Target {
public:
    AppInteropTarget(settings::Store& store,
                     settings::Settings& settings,
                     platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

    std::optional<mo2::ProfileSnapshot> snapshot() const;

private:
    using WatchedFile = std::pair<std::filesystem::path, std::filesystem::file_time_type>;

    Result<nlohmann::json> browseForMo2Folder();
    Result<nlohmann::json> openMo2Profile(const nlohmann::json& args);
    Result<nlohmann::json> loadEnvironment();
    Result<nlohmann::json> getPlugins() const;
    Result<nlohmann::json> getMo2Status() const;
    Result<nlohmann::json> reloadMo2Profile();
    Result<void> loadProfile(const std::filesystem::path& instancePath);

    std::vector<WatchedFile> captureWatchFiles(const mo2::ProfileSnapshot& snapshot) const;
    bool isSnapshotStale(const std::vector<WatchedFile>& files) const;

    settings::Store& store_;
    settings::Settings& settings_;
    platform::Services& platform_;
    mutable std::mutex mutex_;
    std::optional<mo2::ProfileSnapshot> snapshot_;
    std::vector<WatchedFile> watchedFiles_;
    std::chrono::system_clock::time_point loadedAt_{};
};

}
