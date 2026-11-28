#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::rpc {

struct RemoteEndpoint {
    std::string host{"127.0.0.1"};
    int port{0};
    std::string token;
};

class RemoteTarget final : public Target {
public:
    RemoteTarget(RemoteEndpoint endpoint, std::string targetName);

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    RemoteEndpoint endpoint_;
    std::string targetName_;
};

Result<std::vector<std::string>> fetchRemoteTargetNames(const RemoteEndpoint& endpoint);

}
