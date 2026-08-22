#pragma once

#include <string>

#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class PluginBackendTarget final : public rpc::Target {
public:
    PluginBackendTarget(plugin::IPluginBackend& backend, std::string targetNamespace);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    plugin::IPluginBackend& backend_;
    std::string targetNamespace_;
};

}
