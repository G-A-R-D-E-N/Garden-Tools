#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class DestructibleTarget final : public rpc::Target {
public:
    explicit DestructibleTarget(plugin::IPluginBackend& backend);

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    nlohmann::json open(const nlohmann::json& args);
    nlohmann::json read(const nlohmann::json& args);
    nlohmann::json edit(const nlohmann::json& args, const char* action);
    nlohmann::json setHeader(const nlohmann::json& args);
    nlohmann::json create(const nlohmann::json& args);
    nlohmann::json save();
    nlohmann::json close();
    nlohmann::json state();

    plugin::IPluginBackend& backend_;
    std::mutex mutex_;
    std::string plugin_;
};

}  // namespace fo4ide::app
