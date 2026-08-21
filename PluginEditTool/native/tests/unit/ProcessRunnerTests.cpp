#include <chrono>
#include <filesystem>

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
