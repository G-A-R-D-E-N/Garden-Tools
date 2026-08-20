#pragma once

#include <mutex>
#include <optional>

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
    Result<nlohmann::json> browseForMo2Folder();
    Result<nlohmann::json> openMo2Profile(const nlohmann::json& args);
    Result<nlohmann::json> loadEnvironment();
    Result<nlohmann::json> getPlugins() const;
    Result<void> loadProfile(const std::filesystem::path& instancePath);

    settings::Store& store_;
    settings::Settings& settings_;
    platform::Services& platform_;
    mutable std::mutex mutex_;
    std::optional<mo2::ProfileSnapshot> snapshot_;
};

}
