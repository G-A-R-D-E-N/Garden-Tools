#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

class PreviewTarget final : public rpc::Target {
public:
    explicit PreviewTarget(rpc::Target& cells);

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    rpc::Target& cells_;
};

}  // namespace fo4ide::app
