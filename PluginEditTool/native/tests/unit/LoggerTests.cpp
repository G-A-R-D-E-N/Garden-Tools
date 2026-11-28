#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/logging/Logger.hpp>

namespace {

std::filesystem::path tempLogDirectory() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_log_test_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        fo4ide::logging::shutdown();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

}

TEST_CASE("Logging writes categorized messages to the configured file") {
    const auto directory = tempLogDirectory();
    Cleanup cleanup{directory};

    REQUIRE(fo4ide::logging::initialize({
        .directory = directory,
        .filename = "native-test.log",
        .maxBytes = 1024 * 1024,
        .maxFiles = 2,
        .level = spdlog::level::trace,
    }).has_value());

    auto log = fo4ide::logging::get("rpc");
    CHECK(fo4ide::logging::get("rpc") == log);
    log->info("hello from native test");
    log->flush();

    const auto path = directory / "native-test.log";
    REQUIRE(std::filesystem::is_regular_file(path));
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents.find("[rpc]") != std::string::npos);
    CHECK(contents.find("hello from native test") != std::string::npos);
}
