#pragma once

#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string_view>

namespace fo4ide::rpc {

// Read-only previs scan; all checks must stay in Radicle C++ and JSON is display only.
class PrevisTarget final : public Target {
public:
    PrevisTarget(plugin::IPluginBackend& backend, settings::SharedSettings& settings);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> scan(const nlohmann::json& args) const;
    Result<nlohmann::json> dataFolder() const;

    std::filesystem::path resolveDataFolder(const nlohmann::json& args) const;

    plugin::IPluginBackend& backend_;
    settings::SharedSettings& settings_;
};

}
