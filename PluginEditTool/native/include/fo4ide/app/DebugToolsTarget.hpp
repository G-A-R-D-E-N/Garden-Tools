#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/archive/assets.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/mo2/Mo2Profile.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

// Invariant: every outside dependency of the debug tools comes through this struct, and a missing hook only marks the tools that need it unavailable.
struct DebugToolsEnv {
    using Call = std::function<Result<nlohmann::json>(const std::string& target, const std::string& method,
                                                      const nlohmann::json& args)>;
    Call call;
    std::function<nlohmann::json()> settings;
    std::function<std::optional<mo2::ProfileSnapshot>()> snapshot;
    std::function<std::filesystem::path()> gameRoot;
    std::function<std::shared_ptr<const fo4::archive::AssetResolver>()> assets;
    std::function<std::string()> timestamp;
    std::string fo4ideVersion;
    std::filesystem::path exeDir;
};

// Invariant: only allow-listed setting names leave this function, and a name that looks secret is dropped even if listed.
nlohmann::json scrubDebugSettings(const nlohmann::json& settings);

// Invariant: a returned folder is absolute, does not exist yet and is not equal to or inside any protected path.
Result<std::filesystem::path> validateScratchParent(const std::filesystem::path& outputDir,
                                                    const std::vector<std::filesystem::path>& protectedPaths,
                                                    const std::string& stamp);

std::string gameBuildOf(const std::filesystem::path& exe);
std::string readBuildCommit(const std::filesystem::path& exeDir);
nlohmann::json debugToolsContext(const DebugToolsEnv& env);
nlohmann::json debugToolsInventory();

class DebugToolsTarget final : public rpc::Target {
public:
    explicit DebugToolsTarget(DebugToolsEnv env);
    ~DebugToolsTarget() override;

    DebugToolsTarget(const DebugToolsTarget&) = delete;
    DebugToolsTarget& operator=(const DebugToolsTarget&) = delete;

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

    // Constraint: the worker calls the dispatcher that owns this target, so the owner must call this before the dispatcher is destroyed.
    // Constraint: it raises the cancel flag even for a tool that Cancel cannot stop, and that tool stops at its next step, so the join can still wait for one Radicle read, write or text export (a large plugin takes longest).
    void shutdown();

    // Invariant: true from the moment shutdown() raises the stop flag, so a caller can wait for the stop to be in force instead of guessing when the join began.
    bool stopping() const noexcept { return cancel_.load(); }

private:
    struct Run {
        bool active{false};
        bool cancelled{false};
        long long runId{0};
        std::string toolId;
        std::size_t done{0};
        std::size_t total{0};
        std::string text;
        std::string error;
        // Invariant: the UTC time the worker stored the results, empty until then, so a client that first sees a finished run records when it ran.
        std::string finishedAt;
        std::vector<nlohmann::json> results;
    };

    nlohmann::json list() const;
    nlohmann::json status() const;
    Result<nlohmann::json> start(const std::string& toolId, const nlohmann::json& rawParams);
    nlohmann::json cancel();
    void work(std::string toolId, nlohmann::json params, std::filesystem::path scratch);

    DebugToolsEnv env_;
    mutable std::mutex mutex_;
    Run run_;
    long long lastRunId_{0};
    std::atomic<bool> cancel_{false};
    std::jthread worker_;
};

}  // namespace fo4ide::app
