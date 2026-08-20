#pragma once

#include <filesystem>
#include <string_view>

#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::app {

class SettingsTarget final : public rpc::Target {
public:
    SettingsTarget(settings::Store& store, settings::Settings& current);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> getSettings() const;
    Result<nlohmann::json> saveSettings(const nlohmann::json& args);

    settings::Store& store_;
    settings::Settings& current_;
};

class NifTarget final : public rpc::Target {
public:
    explicit NifTarget(assets::NifService& service);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    assets::NifService& service_;
};

std::filesystem::path locateNifTool(const settings::Settings& settings,
                                    const std::filesystem::path& executablePath);

}
