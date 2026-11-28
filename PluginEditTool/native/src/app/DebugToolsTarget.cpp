#include <fo4ide/app/DebugToolsTarget.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>
#include <system_error>
#include <utility>

#include "DebugToolsInternal.hpp"
#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

namespace dt = debugtools;
namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::size_t kMaxParamBytes = 4096;

struct GroupTitle {
    const char* id;
    const char* title;
};

constexpr GroupTitle kGroups[] = {{"environment", "Environment"}, {"plugins", "Plugins"}, {"assets", "Assets"},
                                  {"previs", "Previs"}, {"lod", "LOD"}};

std::string defaultStamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &seconds);
#else
    gmtime_r(&seconds, &parts);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
    char suffix[8]{};
    std::snprintf(suffix, sizeof(suffix), "-%03d", static_cast<int>(millis));
    return std::string(text) + suffix;
}

// Constraint: the web panel parses this with Date, so it is ISO 8601 UTC with milliseconds.
std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &seconds);
#else
    gmtime_r(&seconds, &parts);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &parts);
    char suffix[8]{};
    std::snprintf(suffix, sizeof(suffix), ".%03dZ", static_cast<int>(millis));
    return std::string(text) + suffix;
}

Error badParams(std::string message) {
    return Error{"BadParams", std::move(message)};
}

bool hasControl(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; });
}

Result<json> parseParams(const dt::ToolSpec& spec, const json& raw) {
    json params = json::object();
    if (raw.is_string()) {
        const auto text = support::trim(raw.get<std::string>());
        if (!text.empty()) {
            params = json::parse(text, nullptr, false);
            if (params.is_discarded()) return std::unexpected(badParams("the params are not valid JSON"));
        }
    } else if (!raw.is_null()) {
        params = raw;
    }
    if (!params.is_object()) return std::unexpected(badParams("the params must be a JSON object"));

    std::set<std::string> declared;
    for (const auto& param : spec.params) declared.insert(param.name);
    for (const auto& [key, value] : params.items()) {
        const bool isOutput = key == "outputDir" && spec.needsOutputDir;
        if (!isOutput && !declared.contains(key)) {
            return std::unexpected(badParams("unknown param '" + key + "' for " + spec.id));
        }
        if (!value.is_string()) return std::unexpected(badParams("param '" + key + "' must be text"));
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() > kMaxParamBytes) return std::unexpected(badParams("param '" + key + "' is too long"));
        if (hasControl(text)) return std::unexpected(badParams("param '" + key + "' has control characters"));
    }
    for (const auto& param : spec.params) {
        if (!param.required) continue;
        const auto it = params.find(param.name);
        if (it == params.end() || support::trim(it->get<std::string>()).empty()) {
            return std::unexpected(badParams("param '" + param.name + "' is required for " + spec.id));
        }
    }
    if (spec.needsOutputDir) {
        const auto it = params.find("outputDir");
        if (it == params.end() || support::trim(it->get<std::string>()).empty()) {
            return std::unexpected(badParams("outputDir is required for " + spec.id));
        }
    }
    return params;
}

json toolJson(const dt::ToolSpec& spec, const DebugToolsEnv& env) {
    const auto reason = spec.unavailable ? spec.unavailable(env) : std::string{};
    json params = json::array();
    for (const auto& param : spec.params) {
        params.push_back({{"name", param.name}, {"label", param.label}, {"kind", param.kind},
                          {"required", param.required}});
    }
    return {{"id", spec.id},
            {"title", spec.title},
            {"description", spec.description},
            {"readOnly", spec.readOnly},
            {"writes", spec.writes},
            {"needsOutputDir", spec.needsOutputDir},
            {"expensive", spec.expensive},
            {"cancellable", spec.cancellable},
            {"available", reason.empty()},
            {"unavailableReason", reason},
            {"evidence", spec.evidence},
            {"params", std::move(params)}};
}

}  // namespace

DebugToolsTarget::DebugToolsTarget(DebugToolsEnv env) : env_(std::move(env)) {}

DebugToolsTarget::~DebugToolsTarget() {
    shutdown();
}

void DebugToolsTarget::shutdown() {
    cancel();
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
}

json DebugToolsTarget::list() const {
    json groups = json::array();
    for (const auto& group : kGroups) {
        json tools = json::array();
        for (const auto& spec : dt::toolTable()) {
            if (spec.group == group.id) tools.push_back(toolJson(spec, env_));
        }
        if (!tools.empty()) groups.push_back({{"id", group.id}, {"title", group.title}, {"tools", std::move(tools)}});
    }
    return {{"groups", std::move(groups)}, {"inventory", debugToolsInventory()}};
}

json DebugToolsTarget::status() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return {{"active", run_.active},
            {"cancelled", run_.cancelled},
            {"runId", run_.runId},
            {"toolId", run_.toolId},
            {"progress", {{"done", run_.done}, {"total", run_.total}, {"text", run_.text}}},
            {"results", run_.results},
            {"error", run_.error},
            {"finishedAt", run_.finishedAt}};
}

json DebugToolsTarget::cancel() {
    std::string target;
    std::string method;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!run_.active) return json{{"cancelled", false}};
        const auto* spec = dt::findTool(run_.toolId);
        if (spec == nullptr || !spec->cancellable) return json{{"cancelled", false}};
        cancel_.store(true);
        target = spec->cancelTarget;
        method = spec->cancelMethod;
    }
    if (!target.empty() && env_.call) {
        try {
            static_cast<void>(env_.call(target, method, json::array()));
        } catch (const std::exception&) {
        }
    }
    return json{{"cancelled", true}};
}

Result<json> DebugToolsTarget::start(const std::string& toolId, const json& rawParams) {
    const auto* spec = dt::findTool(toolId);
    if (spec == nullptr) return std::unexpected(Error{"UnknownTool", "no debug tool is named '" + toolId + "'"});
    if (const auto reason = spec->unavailable ? spec->unavailable(env_) : std::string{}; !reason.empty()) {
        return std::unexpected(Error{"ToolUnavailable", spec->id + " is unavailable: " + reason});
    }
    auto params = parseParams(*spec, rawParams);
    if (!params) return std::unexpected(params.error());

    fs::path scratch;
    if (spec->needsOutputDir) {
        const auto outputText = (*params)["outputDir"].get<std::string>();
        const auto source = params->contains("plugin") ? dt::resolvePluginPath(env_, (*params)["plugin"].get<std::string>())
                                                        : fs::path{};
        const auto stamp = env_.timestamp ? env_.timestamp() : defaultStamp();
        auto checked = validateScratchParent(fs::path(std::u8string(outputText.begin(), outputText.end())),
                                             dt::protectedPaths(env_, source), stamp);
        if (!checked) return std::unexpected(checked.error());
        scratch = std::move(*checked);
        params->erase("outputDir");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (run_.active) return std::unexpected(Error{"DebugToolsBusy", "a debug tool is already running"});
    if (worker_.joinable()) worker_.join();
    run_ = Run{};
    run_.active = true;
    run_.runId = ++lastRunId_;
    run_.toolId = spec->id;
    run_.total = 1;
    run_.text = "starting " + spec->title;
    cancel_.store(false);
    // Constraint: a worker that fails to start must not leave the run active, because only the worker clears it and every later Start would report busy until FO4IDE restarts.
    try {
        worker_ = std::jthread([this, id = spec->id, parameters = std::move(*params), scratch = std::move(scratch)]() mutable {
            work(std::move(id), std::move(parameters), std::move(scratch));
        });
    } catch (const std::exception& error) {
        run_ = Run{};
        return std::unexpected(Error{"DebugToolsStartFailed", std::string("the debug tool could not be started: ") + error.what()});
    }
    return json{{"runId", run_.runId}};
}

void DebugToolsTarget::work(std::string toolId, json params, fs::path scratch) {
    const auto* spec = dt::findTool(toolId);
    std::vector<dt::Diag> results;
    dt::Ctx ctx{env_, std::move(params), std::move(scratch), cancel_,
                [this](std::size_t done, std::size_t total, const std::string& text) {
                    std::lock_guard<std::mutex> guard(mutex_);
                    run_.done = done;
                    run_.total = total;
                    run_.text = text;
                }};
    const auto started = std::chrono::steady_clock::now();
    std::string failure;
    try {
        spec->run(ctx, results);
    } catch (const std::exception& error) {
        failure = error.what();
    } catch (...) {
        failure = "unhandled non-standard exception";
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                             .count();
    if (!failure.empty() || results.empty()) {
        dt::Diag error;
        error.id = spec->id;
        error.title = spec->title;
        error.status = "error";
        error.detail = failure.empty() ? "the tool finished without reporting a result" : failure;
        results.push_back(std::move(error));
    }
    for (auto& result : results) result.durationMs = elapsed;
    if (spec->inGame) results.push_back(dt::inGameRow(spec->id));

    json rows = json::array();
    for (const auto& result : results) rows.push_back(result.toJson());
    std::lock_guard<std::mutex> guard(mutex_);
    run_.results.clear();
    for (auto& row : rows) run_.results.push_back(std::move(row));
    run_.cancelled = ctx.cancelledRun;
    run_.done = run_.total;
    run_.text = "finished";
    run_.finishedAt = utcNow();
    run_.active = false;
}

Result<json> DebugToolsTarget::invoke(std::string_view method, const json& args) {
    const std::string name(method);
    if (name == "Context") return json(dt::dumpJson(debugToolsContext(env_)));
    if (name == "List") return json(dt::dumpJson(list()));
    if (name == "Status") return json(dt::dumpJson(status()));
    if (name == "Cancel") return json(dt::dumpJson(cancel()));
    if (name == "Start") {
        const auto* raw = support::argument(args, 1);
        auto started = start(support::stringArgument(args, 0), raw == nullptr ? json(nullptr) : *raw);
        if (!started) return std::unexpected(started.error());
        return json(dt::dumpJson(*started));
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown debugTools RPC method: " + name});
}

}  // namespace fo4ide::app
