#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

struct radicle_swf;
struct radicle_swf_history;

namespace fo4ide::app {

using SwfRootsProvider = std::function<std::vector<std::filesystem::path>()>;

// Constraint: the four roots and their order match ToolPaths.RequireDataOrProjectPath in the C# host.
std::vector<std::filesystem::path> swfRootsFor(const fo4ide::settings::Settings& settings);

// Constraint: the panel talks to one open document at a time, so every method answers with JSON text like the C# host.
class SwfTarget final : public rpc::Target {
public:
    // Invariant: the provider is called on every Open, so a settings change takes effect without a restart.
    SwfTarget(SwfRootsProvider roots, platform::Services& platform);
    ~SwfTarget() override;

    SwfTarget(const SwfTarget&) = delete;
    SwfTarget& operator=(const SwfTarget&) = delete;

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    Result<nlohmann::json> open(const nlohmann::json& args);
    Result<nlohmann::json> save();
    Result<nlohmann::json> snapshot(bool changed);
    bool reopenHistory(std::string& message);
    bool refreshFromHistory(std::string& message, bool undid);
    bool requireUnderRoots(const std::string& path, std::string& rooted) const;
    void closeDocument();

    std::mutex mutex_;
    SwfRootsProvider roots_;
    platform::Services& platform_;
    radicle_swf* movie_{nullptr};
    radicle_swf_history* history_{nullptr};
    std::optional<std::string> path_;
    std::optional<std::filesystem::path> browsed_;
    std::vector<std::byte> saved_;
    std::vector<std::byte> current_;
};

}  // namespace fo4ide::app
