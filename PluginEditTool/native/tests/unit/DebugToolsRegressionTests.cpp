#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

#include "../../src/app/DebugToolsInternal.hpp"
#include "../helpers/PluginFixtures.hpp"
#include "../helpers/TempRoot.hpp"

namespace {
namespace fs = std::filesystem;
namespace dt = fo4ide::app::debugtools;
using nlohmann::json;

TEST_CASE("DebugTools.PrevisScanRejectsMissingOrInvalidCounts", "[debug-tools]") {
    fo4ide_test::TempRoot root{"debug_scan_regression"};
    json reply;
    fo4ide::app::DebugToolsEnv env;
    env.settings = [&] { return json{{"DataFolder", root.path().string()}}; };
    env.call = [&](const std::string&, const std::string&, const json&) -> fo4ide::Result<json> { return reply; };
    std::atomic<bool> cancel{false};
    dt::Ctx ctx{env, json::object(), {}, cancel, [](std::size_t, std::size_t, const std::string&) {}};
    const auto* tool = dt::findTool("previs.scan");
    REQUIRE(tool != nullptr);
    for (const auto& bad : {json::object(), json{{"summary", {{"broken", 0}}}},
                            json{{"summary", {{"broken", "zero"}, {"outdated", 0}}}},
                            json{{"summary", {{"broken", -1}, {"outdated", 0}}}},
                            json{{"summary", {{"broken", 0}, {"outdated", 0}, {"problems", nullptr}}}, {"problems", 2}}}) {
        reply = bad;
        std::vector<dt::Diag> results;
        tool->run(ctx, results);
        REQUIRE(results.size() == 1);
        CHECK(results[0].status == "error");
    }
    reply = {{"summary", {{"broken", 0}, {"outdated", 0}}}};
    std::vector<dt::Diag> incomplete;
    tool->run(ctx, incomplete);
    REQUIRE(incomplete.size() == 1);
    CHECK(incomplete[0].status == "unverified");
    reply = {{"summary", {{"broken", 0}, {"outdated", 0}, {"problems", 0}, {"skipped", 0}}}, {"truncated", false}, {"pendingPlugins", json::array()}};
    std::vector<dt::Diag> results;
    tool->run(ctx, results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].status == "pass");
    reply = {{"broken", 0}, {"outdated", 0}, {"problems", 0}, {"skipped", 0}, {"truncated", false}, {"pendingPlugins", json::array()}};
    std::vector<dt::Diag> legacy;
    tool->run(ctx, legacy);
    REQUIRE(legacy.size() == 1);
    CHECK(legacy[0].status == "pass");
}

TEST_CASE("DebugTools.RoundtripPreservesOnamAndDetectsHedrRepair", "[debug-tools]") {
    fo4ide_test::TempRoot root{"debug_roundtrip_regression"};
    auto bytes = plugin_fixtures_test::minimalPlugin(1, "");
    bytes[4] = static_cast<char>(28);
    bytes += "ONAM";
    plugin_fixtures_test::appendLittleEndian(bytes, 4, 2);
    plugin_fixtures_test::appendLittleEndian(bytes, 0x1234, 4);
    bytes[34] = static_cast<char>(99);
    const auto source = root.path() / "Master.esm";
    std::ofstream(source, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    fo4ide::app::DebugToolsEnv env;
    std::atomic<bool> cancel{false};
    dt::Ctx ctx{env, {{"plugin", source.string()}}, root.path() / "scratch", cancel,
                [](std::size_t, std::size_t, const std::string&) {}};
    const auto* tool = dt::findTool("plugin.roundtrip");
    REQUIRE(tool != nullptr);
    std::vector<dt::Diag> results;
    tool->run(ctx, results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].status == "pass");
    CHECK(results[0].detail.find("HEDR record count") != std::string::npos);
    const auto rebuilt = fo4::read_plugin_file(ctx.scratch / "Master.esm");
    CHECK(rebuilt.header.record_count == fo4::expected_hedr_record_count(rebuilt));
    REQUIRE(rebuilt.header.overrides.size() == 1);

    bytes[8] = 0;
    const auto changedSource = root.path() / "Changed.esm";
    std::ofstream(changedSource, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    dt::Ctx changed{env, {{"plugin", changedSource.string()}}, root.path() / "changed", cancel,
                    [](std::size_t, std::size_t, const std::string&) {}};
    std::vector<dt::Diag> changedResults;
    tool->run(changed, changedResults);
    REQUIRE(changedResults.size() == 1);
    CHECK(changedResults[0].status == "fail");
}
}
