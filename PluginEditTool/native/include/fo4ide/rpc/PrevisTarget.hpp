#pragma once

#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <string_view>
#include <vector>

namespace fo4ide::rpc {

// Root order invariant: the list must stay lowest priority first; MO2 roots already include Data first.
std::vector<std::filesystem::path> previsAssetRoots(const settings::Settings& settings,
                                                    const std::filesystem::path& data_dir);

// Read-only previs scan; all checks must stay in Radicle C++ and JSON is display only.
class PrevisTarget final : public Target {
public:
    PrevisTarget(plugin::IPluginBackend& backend, settings::SharedSettings& settings);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> scan(const nlohmann::json& args);
    Result<nlohmann::json> dataFolder() const;

    std::filesystem::path resolveDataFolder(const nlohmann::json& args) const;

    plugin::IPluginBackend& backend_;
    settings::SharedSettings& settings_;
    // One scan at a time: a second concurrent call must fail instead of loading a second environment.
    std::atomic<bool> scanning_{false};
};

}
