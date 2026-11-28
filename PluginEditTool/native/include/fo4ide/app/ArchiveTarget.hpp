#pragma once

#include <fo4ide/archive/ArchiveService.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class ArchiveTarget final : public rpc::Target {
public:
    ArchiveTarget(archive::ArchiveService& service, platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    archive::ArchiveService& service_;
    platform::Services& platform_;
};

}
