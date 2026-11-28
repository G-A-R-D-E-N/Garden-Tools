#include <fo4ide/app/DebugChecksTarget.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

constexpr std::size_t kMaxCheckNameBytes = 300;
constexpr std::size_t kMaxResultFileBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxExcerptBytes = 20 * 1024;
constexpr std::size_t kMaxDetailRows = 200;
constexpr std::size_t kMaxLogLines = 40;
constexpr std::size_t kMaxLogLineBytes = 500;
constexpr auto kQuickTimeout = std::chrono::milliseconds(60000);
constexpr auto kTestsTimeout = std::chrono::milliseconds(120000);

using nlohmann::json;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string capped(std::string text, std::size_t limit) {
    if (text.size() > limit) {
        text.resize(limit);
        text += "...";
    }
    return text;
}

process::Request rdfRequest(const std::filesystem::path& script, const std::vector<std::string>& extra) {
    process::Request request;
    request.executable = "powershell.exe";
    request.arguments = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script.string()};
    request.arguments.insert(request.arguments.end(), extra.begin(), extra.end());
    request.workingDirectory = script.parent_path();
    return request;
}

std::optional<json> parseObject(std::string_view text) {
    const auto begin = text.find('{');
    const auto end = text.rfind('}');
    if (begin == std::string_view::npos || end == std::string_view::npos || end < begin) return std::nullopt;
    auto value = json::parse(text.substr(begin, end - begin + 1), nullptr, false);
    if (value.is_discarded() || !value.is_object()) return std::nullopt;
    return value;
}

std::string dumpJson(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool listsString(const json& array, const std::string& wanted) {
    if (!array.is_array()) return false;
    return std::any_of(array.begin(), array.end(), [&](const json& item) {
        return item.is_string() && item.get<std::string>() == wanted;
    });
}

Result<std::string> resolveTier(const json& box, const std::string& tier) {
    std::string fallback;
    std::string match;
    if (const auto tiers = box.value("tiers", json::array()); tiers.is_array()) {
        for (const auto& item : tiers) {
            if (!item.is_string()) continue;
            const auto name = item.get<std::string>();
            if (fallback.empty() || name == "fast") fallback = name;
            if (match.empty() && !tier.empty() && lowered(name) == lowered(tier)) match = name;
        }
    }
    if (tier.empty() && !fallback.empty()) return fallback;
    if (!match.empty()) return match;
    return std::unexpected(
        Error{"UnknownTier", "box '" + stringField(box, "id") + "' has no tier '" + capped(tier, 40) + "'"});
}

bool isPassingStatus(const std::string& status) {
    const auto text = lowered(status);
    return text == "pass" || text == "passed" || text == "skip" || text == "skipped";
}

Error missingScript(const std::filesystem::path& script) {
    return Error{"RdfMissing", "rdf.ps1 was not found at '" + script.string() +
                                   "'; set RDF_SCRIPT to its full path"};
}

}  // namespace

bool isSafeCheckName(std::string_view name) {
    if (name.empty() || name.size() > kMaxCheckNameBytes || name.front() == '-') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return c >= 0x20 && c < 0x7F && c != '"' && c != '`' && c != '$';
    });
}

bool isSafeBaseRef(std::string_view ref) {
    if (ref.empty() || ref.size() > 200 || !std::isalnum(static_cast<unsigned char>(ref.front()))) return false;
    if (ref.find("..") != std::string_view::npos) return false;
    return std::all_of(ref.begin(), ref.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '/' || c == '@' || c == '~' || c == '^' || c == '-';
    });
}

std::filesystem::path defaultRdfScript() {
    if (const char* configured = std::getenv("RDF_SCRIPT"); configured && *configured) return configured;
    return "C:\\ci\\real-data-fast\\bin\\rdf.ps1";
}

std::optional<json> parseRdfLine(std::string_view line) {
    if (line.empty() || line.front() != '{') return std::nullopt;
    auto value = json::parse(line, nullptr, false);
    if (value.is_discarded() || !value.is_object()) return std::nullopt;
    return value;
}

json readResultDetail(const std::filesystem::path& resultPath) {
    std::error_code ec;
    auto path = resultPath;
    if (std::filesystem::is_directory(path, ec)) path /= "result.json";
    if (path.filename() != "result.json" || !std::filesystem::is_regular_file(path, ec)) return nullptr;
    if (std::filesystem::file_size(path, ec) > kMaxResultFileBytes || ec) return nullptr;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return nullptr;
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return nullptr;

    json detail = json::object();
    for (const char* key : {"status", "box", "tier", "sha", "passed", "failed", "skipped", "total", "seconds"}) {
        if (const auto it = parsed.find(key); it != parsed.end() && !it->is_object() && !it->is_array()) {
            detail[key] = *it;
        }
    }
    detail["failures"] = json::array();
    if (const auto it = parsed.find("failures"); it != parsed.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (detail["failures"].size() >= kMaxDetailRows) break;
            detail["failures"].push_back(item.is_string() ? json(capped(item.get<std::string>(), kMaxLogLineBytes)) : item);
        }
    }
    detail["failedTests"] = json::array();
    if (const auto it = parsed.find("tests"); it != parsed.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (detail["failedTests"].size() >= kMaxDetailRows) break;
            if (!item.is_object() || isPassingStatus(stringField(item, "status"))) continue;
            detail["failedTests"].push_back(item);
        }
    }
    detail["excerpt"] = capped(stringField(parsed, "excerpt"), kMaxExcerptBytes);
    return detail;
}

void RunState::note(std::string_view line) {
    if (line.empty()) return;
    log.push_back(capped(std::string(line), kMaxLogLineBytes));
    if (log.size() > kMaxLogLines) log.erase(log.begin());
}

void RunState::apply(const json& event, json detail) {
    const auto type = stringField(event, "event");
    if (type == "start") {
        if (const auto name = stringField(event, "box"); !name.empty()) currentBox = name;
    } else if (type == "phase") {
        phase = stringField(event, "name");
    } else if (type == "test") {
        const auto name = stringField(event, "name");
        if (name.empty()) return;
        json row{{"box", currentBox}, {"name", name}, {"status", stringField(event, "status")},
                 {"seconds", event.contains("seconds") && event["seconds"].is_number() ? event["seconds"] : json(0)}};
        const auto same = std::find_if(tests.begin(), tests.end(), [&](const json& existing) {
            return existing.value("box", std::string{}) == currentBox && existing.value("name", std::string{}) == name;
        });
        if (same != tests.end()) *same = std::move(row);
        else tests.push_back(std::move(row));
    } else if (type == "plan") {
        planSeen = true;
        plan.clear();
        if (const auto it = event.find("boxes"); it != event.end() && it->is_array()) {
            for (const auto& item : *it) {
                if (item.is_string()) plan.push_back(item.get<std::string>());
            }
        }
        unknown = event.value("unknown", false);
    } else if (type == "error") {
        error = stringField(event, "error");
    } else if (type == "done") {
        auto doneBox = stringField(event, "box");
        const auto code = event.find("exit_code");
        results.push_back({{"box", doneBox.empty() ? currentBox : doneBox},
                           {"status", stringField(event, "status")},
                           {"exitCode", code != event.end() && code->is_number_integer() ? *code : json(-1)},
                           {"sha", stringField(event, "sha")},
                           {"resultPath", stringField(event, "result_path")},
                           {"line", stringField(event, "line")},
                           {"detail", std::move(detail)}});
    }
}

json RunState::toJson() const {
    return {{"active", active}, {"cancelled", cancelled}, {"kind", kind}, {"box", box}, {"tier", tier},
            {"test", test}, {"phase", phase}, {"error", error}, {"plan", plan}, {"planSeen", planSeen},
            {"unknown", unknown}, {"startedMs", startedMs},
            {"exitCode", exitCode}, {"tests", tests}, {"results", results}, {"log", log}};
}

DebugChecksTarget::DebugChecksTarget(std::filesystem::path script, process::Runner& runner)
    : script_(std::move(script)), runner_(runner) {}

DebugChecksTarget::~DebugChecksTarget() {
    cancel_.store(true);
    process::killTree(pid_.load());
}

Result<json> DebugChecksTarget::runRdf(const std::vector<std::string>& extra, std::chrono::milliseconds timeout) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script_, ec)) return std::unexpected(missingScript(script_));
    auto request = rdfRequest(script_, extra);
    request.timeout = timeout;
    auto output = runner_.run(request);
    if (!output) return std::unexpected(output.error());
    if (output->timedOut) return std::unexpected(Error{"RdfTimedOut", "rdf.ps1 did not answer in time"});
    if (auto parsed = parseObject(output->stdOut)) {
        if (stringField(*parsed, "event") == "error") {
            return std::unexpected(Error{"RdfFailed", "rdf.ps1 failed: " + capped(stringField(*parsed, "error"), 200)});
        }
        return *parsed;
    }
    return std::unexpected(Error{"RdfBadOutput", "rdf.ps1 printed no JSON (exit " + std::to_string(output->exitCode) +
                                                     "): " + capped(support::trim(output->combined()), 300)});
}

Result<json> DebugChecksTarget::fetchBoxes() {
    auto list = runRdf({"-List", "-Json"}, kQuickTimeout);
    if (!list) return list;
    const auto boxes = list->find("boxes");
    if (boxes == list->end() || !boxes->is_array()) {
        return std::unexpected(Error{"RdfBadOutput", "rdf.ps1 -List -Json returned no boxes array"});
    }
    std::lock_guard<std::mutex> guard(mutex_);
    boxes_ = *boxes;
    return list;
}

Result<json> DebugChecksTarget::findBox(const std::string& box) {
    const auto lookup = [&]() -> std::optional<json> {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& item : boxes_) {
            if (lowered(stringField(item, "id")) == lowered(box)) return item;
        }
        return std::nullopt;
    };
    if (auto cached = lookup()) return *cached;
    if (auto fetched = fetchBoxes(); !fetched) return fetched;
    if (auto found = lookup()) return *found;
    return std::unexpected(Error{"UnknownBox", "rdf does not list a box named '" + capped(box, 60) + "'"});
}

Result<json> DebugChecksTarget::tests(const std::string& box, const std::string& tier) {
    auto found = findBox(box);
    if (!found) return found;
    auto resolved = resolveTier(*found, tier);
    if (!resolved) return std::unexpected(resolved.error());
    auto document = runRdf({"-Tests", "-Box", stringField(*found, "id"), "-Tier", *resolved, "-Json"}, kTestsTimeout);
    if (!document) return document;
    const auto available = document->find("available");
    if (available == document->end() || !available->is_boolean()) {
        return std::unexpected(Error{"RdfBadOutput", "rdf.ps1 -Tests -Json returned no available flag"});
    }
    if (!document->contains("tests") || !(*document)["tests"].is_array()) (*document)["tests"] = json::array();
    return document;
}

Result<json> DebugChecksTarget::start(const std::string& kind, const std::string& box, const std::string& tier,
                                      const std::string& test, const std::string& base) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script_, ec)) return std::unexpected(missingScript(script_));
    if (kind != "box" && kind != "all" && kind != "changed") {
        return std::unexpected(Error{"InvalidArgument", "kind must be box, all or changed"});
    }
    const auto busy = [] { return Error{"DebugChecksBusy", "a check run is already active"}; };
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state_.active) return std::unexpected(busy());
    }

    std::vector<std::string> extra;
    std::string runBox = box;
    std::string runTier = tier;
    if (kind == "box") {
        auto found = findBox(box);
        if (!found) return found;
        auto resolved = resolveTier(*found, tier);
        if (!resolved) return std::unexpected(resolved.error());
        runBox = stringField(*found, "id");
        runTier = *resolved;
        extra = {"-Box", runBox, "-Tier", runTier};
        if (!test.empty()) {
            if (!isSafeCheckName(test)) return std::unexpected(Error{"UnsafeTestName", "the test name is not allowed"});
            auto listed = tests(runBox, runTier);
            if (!listed) return listed;
            if (!listed->value("available", false)) {
                const auto reason = stringField(*listed, "reason");
                return std::unexpected(Error{"TestsUnavailable", reason.empty() ? "tests are unavailable" : reason});
            }
            if (!listsString(listed->value("tests", json::array()), test)) {
                return std::unexpected(Error{"UnknownTest", "rdf does not list that test for " + runBox + "/" + runTier});
            }
            extra.insert(extra.end(), {"-Test", test});
        }
    } else {
        extra = {kind == "all" ? "-All" : "-Changed"};
        if (kind == "changed" && !base.empty()) {
            if (!isSafeBaseRef(base)) return std::unexpected(Error{"UnsafeBaseRef", "the base ref is not allowed"});
            extra.insert(extra.end(), {"-Base", base});
        }
    }
    extra.push_back("-Json");

    std::lock_guard<std::mutex> guard(mutex_);
    if (state_.active) return std::unexpected(busy());
    if (worker_.joinable()) worker_.join();
    state_ = RunState{};
    state_.active = true;
    state_.kind = kind;
    state_.box = runBox;
    state_.tier = runTier;
    state_.test = test;
    state_.currentBox = runBox;
    state_.startedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    cancel_.store(false);
    pid_.store(0);
    worker_ = std::jthread([this, extra = std::move(extra)]() mutable { work(std::move(extra)); });
    return json{{"started", true}};
}

void DebugChecksTarget::handleLine(bool isStderr, std::string_view line) {
    if (!isStderr) {
        if (auto event = parseRdfLine(line)) {
            json detail = nullptr;
            if (stringField(*event, "event") == "done") {
                if (const auto path = stringField(*event, "result_path"); !path.empty()) {
                    detail = readResultDetail(std::filesystem::path(path));
                }
            }
            std::lock_guard<std::mutex> guard(mutex_);
            state_.apply(*event, std::move(detail));
            return;
        }
    }
    std::lock_guard<std::mutex> guard(mutex_);
    state_.note(line);
}

void DebugChecksTarget::work(std::vector<std::string> extra) {
    const auto request = rdfRequest(script_, extra);
    const auto result = process::runStreaming(
        request, [this](bool isStderr, std::string_view line) { handleLine(isStderr, line); }, pid_, cancel_);

    std::lock_guard<std::mutex> guard(mutex_);
    state_.active = false;
    state_.cancelled = cancel_.load();
    if (!result) {
        state_.error = result.error().message;
        return;
    }
    state_.exitCode = *result;
    if (!state_.cancelled && *result != 0 && state_.planSeen && state_.results.size() < state_.plan.size()) {
        const auto missing = state_.plan.size() - state_.results.size();
        state_.error = (state_.error.empty() ? "" : state_.error + ": ") + std::to_string(missing) + " of " +
                       std::to_string(state_.plan.size()) + " boxes did not report";
        return;
    }
    if (state_.cancelled || !state_.results.empty()) return;
    if (*result == 0 && state_.planSeen && state_.plan.empty()) {
        state_.results.push_back({{"box", ""}, {"status", "PASS"}, {"exitCode", 0}, {"sha", ""}, {"resultPath", ""},
                                  {"line", "No checks affected."}, {"detail", nullptr}});
        return;
    }
    if (!state_.error.empty()) {
        if (state_.error == "lock-timeout") state_.error += ": another run is using the data host";
        return;
    }
    std::string tail;
    for (std::size_t i = state_.log.size() > 3 ? state_.log.size() - 3 : 0; i < state_.log.size(); ++i) {
        tail += (tail.empty() ? "" : " ") + state_.log[i];
    }
    state_.error = (*result == 3 ? "rdf could not take the run lock: another run is using the data host"
                                 : "rdf exited with code " + std::to_string(*result) + " without reporting a result") +
                   (tail.empty() ? "" : ": " + tail);
}

Result<json> DebugChecksTarget::cancel() {
    // Invariant: the flag and the pid are used under mutex_, so a cancel cannot reach a run that started after the check.
    std::lock_guard<std::mutex> guard(mutex_);
    if (!state_.active) return json{{"cancelled", false}};
    cancel_.store(true);
    process::killTree(pid_.load());
    return json{{"cancelled", true}};
}

json DebugChecksTarget::status() {
    std::error_code ec;
    const bool found = std::filesystem::is_regular_file(script_, ec);
    std::lock_guard<std::mutex> guard(mutex_);
    return {{"script", script_.string()}, {"scriptFound", found}, {"run", state_.toJson()}};
}

Result<json> DebugChecksTarget::invoke(std::string_view method, const json& args) {
    const std::string name(method);
    if (name == "Status") return dumpJson(status());
    if (name == "Cancel") {
        auto result = cancel();
        if (!result) return result;
        return dumpJson(*result);
    }
    Result<json> result = std::unexpected(Error{"UnknownMethod", "Unknown debugChecks RPC method: " + name});
    if (name == "List") {
        result = fetchBoxes();
    } else if (name == "Results") {
        result = runRdf({"-Results", "-Json"}, kQuickTimeout);
    } else if (name == "Tests") {
        result = tests(support::stringArgument(args, 0), support::stringArgument(args, 1));
    } else if (name == "Start") {
        result = start(support::stringArgument(args, 0), support::stringArgument(args, 1),
                       support::stringArgument(args, 2), support::stringArgument(args, 3), support::stringArgument(args, 4));
    }
    if (!result) return result;
    return dumpJson(*result);
}

}  // namespace fo4ide::app
