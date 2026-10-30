#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/rpc/RpcDispatcher.hpp>

struct radicle_swf;
struct radicle_swf_history;

namespace fo4ide::app {

// Constraint: the panel talks to one open document at a time, so every method answers with JSON text like the C# host.
class SwfTarget final : public rpc::Target {
public:
    SwfTarget();
    ~SwfTarget() override;

    SwfTarget(const SwfTarget&) = delete;
    SwfTarget& operator=(const SwfTarget&) = delete;

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    Result<nlohmann::json> open(const nlohmann::json& args);
    Result<nlohmann::json> snapshot(bool changed);
    bool refreshFromHistory(std::string& message);

    std::mutex mutex_;
    radicle_swf* movie_{nullptr};
    radicle_swf_history* history_{nullptr};
    std::optional<std::string> path_;
    std::vector<std::byte> saved_;
    std::vector<std::byte> current_;
};

}  // namespace fo4ide::app
