#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/app/DebugToolsTarget.hpp>

namespace fo4ide::app::debugtools {

using json = nlohmann::json;

struct Side {
    std::string label;
    std::string source;
    std::string value;
};

struct EmptyScratchCleanup {
    std::filesystem::path folder;
    ~EmptyScratchCleanup() {
        std::error_code error;
        if (std::filesystem::is_empty(folder, error) && !error) std::filesystem::remove(folder, error);
    }
};

struct Diag {
    std::string id;
    std::string title;
    std::string status{"error"};
    std::string detail;
    std::vector<std::string> evidence;
    std::vector<std::string> diffs;
    std::vector<std::string> outputs;
    std::optional<Side> left;
    std::optional<Side> right;
    long long durationMs{0};

    json toJson() const;
};

struct ParamSpec {
    std::string name;
    std::string label;
    std::string kind;
    bool required{true};
};

struct Ctx {
    const DebugToolsEnv& env;
    json params;
    std::filesystem::path scratch;
    std::atomic<bool>& cancel;
    std::function<void(std::size_t, std::size_t, const std::string&)> progress;
    bool cancelledRun{false};

    bool cancelled() const { return cancel.load(); }
    std::string param(const std::string& name) const;
    Result<json> call(const std::string& target, const std::string& method, const json& args) const;
};

struct ToolSpec {
    std::string id;
    std::string group;
    std::string title;
    std::string description;
    bool readOnly{true};
    bool writes{false};
    bool needsOutputDir{false};
    bool expensive{false};
    bool cancellable{false};
    bool inGame{false};
    std::vector<std::string> evidence;
    std::vector<ParamSpec> params;
    std::string cancelTarget;
    std::string cancelMethod;
    std::function<std::string(const DebugToolsEnv&)> unavailable;
    std::function<void(Ctx&, std::vector<Diag>&)> run;
};

std::string lowered(std::string text);
std::string textField(const json& object, const char* key);
long long countField(const json& object, const char* key);
bool hasField(const json& object, const char* key);
// Invariant: never throws, so a reply row that is not an object, or a field of the wrong type, cannot end a tool with a generic error.
json fieldOr(const json& object, const char* key, json fallback);
Diag begin(const char* id, const char* title, std::vector<std::string> evidence);
void setStatus(Diag& diag, const char* status, std::string detail);
// Constraint: Cancel leaves a non-cancellable tool running, but shutdown raises the flag for every tool, so a long tool checks this between its Radicle calls and FO4IDE exit waits only for the call in progress.
bool stoppedForShutdown(Ctx& c, Diag& diag);
void fromError(Diag& diag, const Error& error);
void addDiff(Diag& diag, std::string row);
std::string needCall(const DebugToolsEnv& env);
std::string needAssets(const DebugToolsEnv& env);
std::string noRequirement(const DebugToolsEnv& env);
ToolSpec makeTool(const char* id, const char* group, const char* title, const char* description,
                  std::vector<std::string> evidence, std::vector<ParamSpec> params,
                  std::function<std::string(const DebugToolsEnv&)> unavailable,
                  std::function<void(Ctx&, std::vector<Diag>&)> run);
void appendAssetTools(std::vector<ToolSpec>& table);
void appendReferenceTools(std::vector<ToolSpec>& table);

using BrokenCells = std::map<std::string, std::string>;

struct BrokenCellTable {
    std::string build;
    BrokenCells cells;
};

struct BrokenCellDiff {
    std::vector<std::string> missing;
    std::vector<std::string> extra;
    std::vector<std::string> different;

    bool equal() const { return missing.empty() && extra.empty() && different.empty(); }
};

Result<BrokenCellTable> parseBrokenCellTable(std::string_view text);
BrokenCellDiff compareBrokenCells(const BrokenCells& pinned, const BrokenCells& actual);

const std::vector<ToolSpec>& toolTable();
const ToolSpec* findTool(std::string_view id);
Diag inGameRow(const std::string& toolId);

std::string dumpJson(const json& value);
std::filesystem::path dataDirOf(const DebugToolsEnv& env);
std::string gameBuildFor(const DebugToolsEnv& env);
std::string gameBuildBesideData(const DebugToolsEnv& env);
std::filesystem::path resolvePluginPath(const DebugToolsEnv& env, const std::string& name);
std::vector<std::filesystem::path> protectedPaths(const DebugToolsEnv& env, const std::filesystem::path& sourcePlugin);
bool isInside(const std::filesystem::path& child, const std::filesystem::path& parent);

}  // namespace fo4ide::app::debugtools
