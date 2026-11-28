#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

// Invariant: a check name reaches an rdf command line only if it passes this and rdf listed it.
bool isSafeCheckName(std::string_view name);

// Invariant: a -Base value reaches rdf only if it matches the engine client's ref pattern (no leading dash, no "..").
bool isSafeBaseRef(std::string_view ref);

// Constraint: a non-empty RDF_SCRIPT must override the install path.
std::filesystem::path defaultRdfScript();

// Invariant: a stdout line that is not a JSON object is not an event, so it is returned as nothing.
std::optional<nlohmann::json> parseRdfLine(std::string_view line);

// Requires the file to be named result.json (a directory gets it appended); anything else yields null.
nlohmann::json readResultDetail(const std::filesystem::path& resultPath);

struct RunState {
    bool active{false};
    bool cancelled{false};
    std::string kind;
    std::string box;
    std::string tier;
    std::string test;
    std::string phase;
    std::string error;
    std::string currentBox;
    std::vector<std::string> plan;
    bool planSeen{false};
    bool unknown{false};
    long long startedMs{0};
    int exitCode{-1};
    std::vector<nlohmann::json> tests;
    std::vector<nlohmann::json> results;
    std::vector<std::string> log;

    // Invariant: unknown events are ignored, so a newer rdf cannot break an older panel.
    void apply(const nlohmann::json& event, nlohmann::json detail = nullptr);
    void note(std::string_view line);
    nlohmann::json toJson() const;
};

class DebugChecksTarget final : public rpc::Target {
public:
    DebugChecksTarget(std::filesystem::path script, process::Runner& runner);
    ~DebugChecksTarget() override;

    DebugChecksTarget(const DebugChecksTarget&) = delete;
    DebugChecksTarget& operator=(const DebugChecksTarget&) = delete;

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    Result<nlohmann::json> runRdf(const std::vector<std::string>& extra, std::chrono::milliseconds timeout);
    Result<nlohmann::json> fetchBoxes();
    Result<nlohmann::json> findBox(const std::string& box);
    Result<nlohmann::json> tests(const std::string& box, const std::string& tier);
    Result<nlohmann::json> start(const std::string& kind, const std::string& box, const std::string& tier,
                                 const std::string& test, const std::string& base);
    Result<nlohmann::json> cancel();
    nlohmann::json status();
    void work(std::vector<std::string> extra);
    void handleLine(bool isStderr, std::string_view line);

    std::filesystem::path script_;
    process::Runner& runner_;
    mutable std::mutex mutex_;
    RunState state_;
    nlohmann::json boxes_{nlohmann::json::array()};
    std::atomic<int> pid_{0};
    std::atomic<bool> cancel_{false};
    std::jthread worker_;
};

}  // namespace fo4ide::app
