#pragma once

#include <string_view>

#include <fo4ide/audio/AudioService.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class AudioTarget final : public rpc::Target {
public:
    AudioTarget(audio::AudioService& service, platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    audio::AudioService& service_;
    platform::Services& platform_;
};

}
