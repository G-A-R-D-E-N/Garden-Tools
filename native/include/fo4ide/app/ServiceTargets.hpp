#pragma once

#include <string_view>

#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::app {

class SettingsTarget final : public rpc::Target {
public:
    SettingsTarget(settings::Store& store,
                   settings::Settings& current,
                   platform::Services& platform,
                   process::Runner& runner);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> getSettings() const;
    Result<nlohmann::json> saveSettings(const nlohmann::json& args);
    Result<nlohmann::json> browseFolder(const nlohmann::json& args);
    Result<nlohmann::json> browseFile(const nlohmann::json& args);
    Result<nlohmann::json> testClaude(const nlohmann::json& args);

    settings::Store& store_;
    settings::Settings& current_;
    platform::Services& platform_;
    process::Runner& runner_;
};

class NifTarget final : public rpc::Target {
public:
    NifTarget(assets::NifService& service, platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    assets::NifService& service_;
    platform::Services& platform_;
};

}
