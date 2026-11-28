#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/process/ProcessRunner.hpp>

using namespace std::chrono_literals;

namespace {

std::filesystem::path helper() {
    return std::filesystem::path(FO4IDE_FAKE_PROCESS_PATH);
}

}

TEST_CASE("ProcessRunner captures stdout stderr and exit code") {
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--stdout", "hello", "--stderr", "problem", "--exit", "7"};

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(result->started);
    CHECK_FALSE(result->timedOut);
    CHECK(result->exitCode == 7);
    CHECK(result->stdOut == "hello");
    CHECK(result->stdErr == "problem");
}

TEST_CASE("ProcessRunner preserves arguments containing spaces") {
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--echo-arg", "a path with spaces/file.nif"};

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(result->stdOut == "a path with spaces/file.nif");
}

TEST_CASE("ProcessRunner applies working directory") {
    const auto working = std::filesystem::temp_directory_path();
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--cwd"};
    request.workingDirectory = working;

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::equivalent(std::filesystem::path(result->stdOut), working));
}

TEST_CASE("ProcessRunner extends child environment") {
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--env", "FO4IDE_PROCESS_TEST"};
    request.environment.emplace("FO4IDE_PROCESS_TEST", "native-ok");

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(result->stdOut == "native-ok");
}

TEST_CASE("ProcessRunner terminates a timed out child") {
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--sleep", "1000"};
    request.timeout = 25ms;

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(result->started);
    CHECK(result->timedOut);
}

TEST_CASE("ProcessRunner caps captured output and reports the dropped bytes") {
    fo4ide::process::ReprocRunner runner;
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--stdout-bytes", "2200000", "--stderr-bytes", "1200000"};

    auto result = runner.run(request);
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    CHECK(result->truncated);
    CHECK(result->stdOut.size() <= 1024U * 1024U);
    CHECK(result->stdErr.size() <= 1024U * 1024U);
    CHECK(result->droppedBytes == (2200000U - result->stdOut.size()) +
                                   (1200000U - result->stdErr.size()));
    CHECK(result->combined().find("output truncated") != std::string::npos);
}

TEST_CASE("runStreaming delivers each line without its break and flushes an unterminated tail") {
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--stdout", "first\nsecond\nthird", "--stderr", "warn\n"};
    std::vector<std::string> lines;
    std::atomic<int> pid{0};
    const std::atomic<bool> cancel{false};

    auto result = fo4ide::process::runStreaming(
        request, [&lines](bool isStderr, std::string_view line) { lines.push_back((isStderr ? "E:" : "O:") + std::string(line)); },
        pid, cancel);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
    CHECK(lines.size() == 4);
    CHECK(std::count(lines.begin(), lines.end(), "O:first") == 1);
    CHECK(std::count(lines.begin(), lines.end(), "O:second") == 1);
    CHECK(std::count(lines.begin(), lines.end(), "O:third") == 1);
    CHECK(std::count(lines.begin(), lines.end(), "E:warn") == 1);
}

TEST_CASE("killTree ends a running streamed child and a cancel raised before the spawn still ends it") {
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--sleep", "60000"};
    const auto sink = [](bool, std::string_view) {};

    std::atomic<int> pid{0};
    const std::atomic<bool> noCancel{false};
    auto running = std::async(std::launch::async, [&] {
        return fo4ide::process::runStreaming(request, sink, pid, noCancel);
    });
    for (int i = 0; i < 500 && pid.load() == 0; ++i) std::this_thread::sleep_for(10ms);
    REQUIRE(pid.load() != 0);
    fo4ide::process::killTree(pid.load());
    REQUIRE(running.wait_for(15s) == std::future_status::ready);
    auto ended = running.get();
    REQUIRE(ended.has_value());
    CHECK(*ended != 0);

    std::atomic<int> earlyPid{0};
    const std::atomic<bool> early{true};
    auto cancelled = std::async(std::launch::async, [&] {
        return fo4ide::process::runStreaming(request, sink, earlyPid, early);
    });
    REQUIRE(cancelled.wait_for(15s) == std::future_status::ready);
    auto endedEarly = cancelled.get();
    REQUIRE(endedEarly.has_value());
    CHECK(*endedEarly != 0);
}

TEST_CASE("runStreaming bounds a line that never ends instead of buffering it") {
    fo4ide::process::Request request;
    request.executable = helper();
    request.arguments = {"--stdout-bytes", "300000"};
    std::size_t total = 0;
    std::size_t longest = 0;
    int pieces = 0;
    std::atomic<int> pid{0};
    const std::atomic<bool> cancel{false};

    auto result = fo4ide::process::runStreaming(
        request,
        [&](bool, std::string_view line) {
            total += line.size();
            longest = std::max(longest, line.size());
            ++pieces;
        },
        pid, cancel);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
    CHECK(total == 300000U);
    CHECK(pieces >= 3);
    CHECK(longest < 100000U);
}

#if defined(_WIN32)

namespace {

bool processAlive(int pid) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
}

fo4ide::process::Request grandchildRequest(const char* tail) {
    fo4ide::process::Request request;
    request.executable = "powershell.exe";
    request.arguments = {"-NoProfile", "-Command",
                         "$p = Start-Process -FilePath '" + helper().string() +
                             "' -ArgumentList '--sleep','30000' -PassThru -NoNewWindow; "
                             "[Console]::Out.WriteLine('gc=' + $p.Id); [Console]::Out.Flush(); " +
                             tail};
    return request;
}

int grandchildPid(const std::string& line) {
    return line.rfind("gc=", 0) == 0 ? std::stoi(line.substr(3)) : 0;
}

}  // namespace

TEST_CASE("killTree ends a grandchild that holds the output pipes, and runStreaming then returns") {
    const auto request = grandchildRequest("Start-Sleep 60");
    std::atomic<int> grandchild{0};
    std::atomic<int> pid{0};
    const std::atomic<bool> cancel{false};
    auto running = std::async(std::launch::async, [&] {
        return fo4ide::process::runStreaming(
            request, [&](bool, std::string_view line) { if (const int id = grandchildPid(std::string(line))) grandchild.store(id); },
            pid, cancel);
    });
    for (int i = 0; i < 1500 && (pid.load() == 0 || grandchild.load() == 0); ++i) std::this_thread::sleep_for(10ms);
    REQUIRE(grandchild.load() != 0);
    REQUIRE(processAlive(grandchild.load()));
    fo4ide::process::killTree(pid.load());
    const bool returned = running.wait_for(20s) == std::future_status::ready;
    if (!returned) fo4ide::process::killTree(grandchild.load());
    REQUIRE(returned);
    for (int i = 0; i < 200 && processAlive(grandchild.load()); ++i) std::this_thread::sleep_for(25ms);
    CHECK_FALSE(processAlive(grandchild.load()));
}

TEST_CASE("runStreaming returns shortly after the child exits even when a grandchild keeps the pipes open") {
    const auto request = grandchildRequest("exit 5");
    std::atomic<int> grandchild{0};
    std::atomic<int> pid{0};
    const std::atomic<bool> cancel{false};
    const auto began = std::chrono::steady_clock::now();
    auto running = std::async(std::launch::async, [&] {
        return fo4ide::process::runStreaming(
            request, [&](bool, std::string_view line) { if (const int id = grandchildPid(std::string(line))) grandchild.store(id); },
            pid, cancel);
    });
    const bool returned = running.wait_for(20s) == std::future_status::ready;
    const auto elapsed = std::chrono::steady_clock::now() - began;
    if (grandchild.load() != 0) fo4ide::process::killTree(grandchild.load());
    REQUIRE(returned);
    auto result = running.get();
    REQUIRE(result.has_value());
    CHECK(*result == 5);
    CHECK(elapsed < 20s);
    CHECK(grandchild.load() != 0);
}

#endif
