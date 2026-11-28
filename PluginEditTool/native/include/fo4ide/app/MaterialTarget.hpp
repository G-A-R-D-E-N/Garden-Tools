#pragma once

#include <fo4ide/material/MaterialService.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class MaterialTarget final : public rpc::Target {
public:
    MaterialTarget(material::MaterialService& service, platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    material::MaterialService& service_;
    platform::Services& platform_;
};

}
