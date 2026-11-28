#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/DebugChecksTarget.hpp>

#include "../helpers/TempRoot.hpp"

namespace {

using fo4ide::app::DebugChecksTarget;
using nlohmann::json;

constexpr const char* kList =
    R"({"version":1,"results_root":"C:\\r","boxes":[)"
    R"({"id":"G16","title":"LOD","kind":"native","tags":["lod"],"tiers":["fast","slow"],"last":{"status":"NONE","sha":"","passed":0,"total":0,"skip":0,"seconds":0,"at":"","failing":[],"error":""},"running":null},)"
    R"({"id":"ALL","title":"Everything","kind":"full","tags":[],"tiers":["full"],"last":{"status":"INTERRUPTED","sha":"abc1234","passed":0,"total":0,"skip":0,"seconds":0,"at":"2026-11-20T13:57:01Z","failing":[],"error":"interrupted"},"running":null}]})";
constexpr const char* kTests =
    R"({"version":1,"box":"G16","tier":"fast","available":true,"reason":"","count":2,"tests":["LodObjectTileReal.Fixed","LodObjectTileReal.Other"]})";
constexpr const char* kUnavailable =
    R"({"version":1,"box":"G16","tier":"fast","available":false,"reason":"tests unavailable until the first run has built the box","count":0,"tests":[]})";

class FakeRdf final : public fo4ide::process::Runner {
public:
    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        std::string joined;
        for (const auto& argument : request.arguments) joined += argument + " ";
        calls.push_back(joined);
        fo4ide::process::Output output;
        output.started = true;
        output.stdOut = joined.find("-List") != std::string::npos ? list : tests;
        return output;
    }

    std::string list = kList;
    std::string tests = kTests;
    std::vector<std::string> calls;
};

struct Scripted {
    fo4ide_test::TempRoot root{"fo4ide_debug_checks"};
    FakeRdf rdf;
    std::filesystem::path script = root.path() / "rdf.ps1";
    Scripted() { std::ofstream(script) << "param()\n"; }
};

json statusOf(DebugChecksTarget& target) {
    return json::parse(target.invoke("Status", json::array()).value().get<std::string>());
}

bool anyCallHas(const FakeRdf& rdf, const std::string& needle) {
    for (const auto& call : rdf.calls) {
        if (call.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("debug checks: a test name or base ref that rdf could read as a switch is never accepted") {
    CHECK_FALSE(fo4ide::app::isSafeCheckName("-All"));
    CHECK_FALSE(fo4ide::app::isSafeCheckName(""));
    CHECK_FALSE(fo4ide::app::isSafeCheckName("Suite.Name\"; calc"));
    CHECK_FALSE(fo4ide::app::isSafeCheckName("Suite.$env:PATH"));
    CHECK(fo4ide::app::isSafeCheckName("CApiLodReal.GeneratesCommonwealthLodEndToEnd"));
    CHECK(fo4ide::app::isSafeCheckName("Fixture/Suite.Name/3"));
    CHECK(fo4ide::app::isSafeCheckName("catch2 name with spaces"));

    CHECK(fo4ide::app::isSafeBaseRef("origin/nomad/dev"));
    CHECK(fo4ide::app::isSafeBaseRef("1f7615b9e"));
    CHECK(fo4ide::app::isSafeBaseRef("HEAD~3"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef(""));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef("-All"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef("--upload-pack=calc"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef("a..b"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef("main; calc"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef("two words"));
    CHECK_FALSE(fo4ide::app::isSafeBaseRef(std::string(201, 'a')));
}

TEST_CASE("debug checks: Start refuses box, tier, test and base values that rdf did not list or allow") {
    Scripted s;
    DebugChecksTarget target(s.script, s.rdf);

    auto unknownBox = target.invoke("Start", json::array({"box", "ZZ9", "fast", ""}));
    REQUIRE_FALSE(unknownBox);
    CHECK(unknownBox.error().type == "UnknownBox");

    auto dashBox = target.invoke("Start", json::array({"box", "-All", "fast", ""}));
    REQUIRE_FALSE(dashBox);
    CHECK(dashBox.error().type == "UnknownBox");

    auto unknownTier = target.invoke("Start", json::array({"box", "G16", "full", ""}));
    REQUIRE_FALSE(unknownTier);
    CHECK(unknownTier.error().type == "UnknownTier");

    auto badName = target.invoke("Start", json::array({"box", "G16", "fast", "-All"}));
    REQUIRE_FALSE(badName);
    CHECK(badName.error().type == "UnsafeTestName");

    auto unlisted = target.invoke("Start", json::array({"box", "G16", "fast", "Other.Test"}));
    REQUIRE_FALSE(unlisted);
    CHECK(unlisted.error().type == "UnknownTest");

    auto kind = target.invoke("Start", json::array({"everything", "", "", ""}));
    REQUIRE_FALSE(kind);
    CHECK(kind.error().type == "InvalidArgument");

    for (const char* base : {"-All", "--upload-pack=calc", "a..b"}) {
        auto changed = target.invoke("Start", json::array({"changed", "", "", "", base}));
        REQUIRE_FALSE(changed);
        CHECK(changed.error().type == "UnsafeBaseRef");
    }

    CHECK_FALSE(anyCallHas(s.rdf, "-Test "));
    CHECK_FALSE(anyCallHas(s.rdf, "-All"));
    CHECK_FALSE(anyCallHas(s.rdf, "-Base"));
    CHECK_FALSE(statusOf(target)["run"]["active"].get<bool>());
}

TEST_CASE("debug checks: an unavailable test list is shown with its reason and blocks a single-test run") {
    Scripted s;
    s.rdf.tests = kUnavailable;
    DebugChecksTarget target(s.script, s.rdf);

    auto listed = target.invoke("Tests", json::array({"G16", "fast"}));
    REQUIRE(listed);
    const auto document = json::parse(listed->get<std::string>());
    CHECK_FALSE(document["available"].get<bool>());
    CHECK(document["reason"].get<std::string>().find("first run") != std::string::npos);
    CHECK(document["tests"].empty());

    auto start = target.invoke("Start", json::array({"box", "G16", "fast", "LodObjectTileReal.Fixed"}));
    REQUIRE_FALSE(start);
    CHECK(start.error().type == "TestsUnavailable");
    CHECK(start.error().message.find("first run") != std::string::npos);
}

TEST_CASE("debug checks: box and tier are matched like the engine client and the listed spelling is what rdf gets") {
    Scripted s;
    DebugChecksTarget target(s.script, s.rdf);

    REQUIRE(target.invoke("Tests", json::array({"g16", ""})));
    REQUIRE(target.invoke("Tests", json::array({"G16", "SLOW"})));
    REQUIRE(target.invoke("Tests", json::array({"ALL", ""})));
    REQUIRE_FALSE(target.invoke("Tests", json::array({"G16", "full"})));

    REQUIRE(s.rdf.calls.size() == 4);
    CHECK(s.rdf.calls[1].find("-Tests -Box G16 -Tier fast -Json") != std::string::npos);
    CHECK(s.rdf.calls[2].find("-Tests -Box G16 -Tier slow -Json") != std::string::npos);
    CHECK(s.rdf.calls[3].find("-Tests -Box ALL -Tier full -Json") != std::string::npos);
}

TEST_CASE("debug checks: a missing rdf.ps1 is a clear error, not a spawn failure") {
    fo4ide_test::TempRoot root("fo4ide_debug_checks");
    FakeRdf rdf;
    DebugChecksTarget target(root.path() / "absent" / "rdf.ps1", rdf);
    for (const char* method : {"List", "Results"}) {
        auto result = target.invoke(method, json::array());
        REQUIRE_FALSE(result);
        CHECK(result.error().type == "RdfMissing");
        CHECK(result.error().message.find("RDF_SCRIPT") != std::string::npos);
    }
    auto start = target.invoke("Start", json::array({"all", "", "", ""}));
    REQUIRE_FALSE(start);
    CHECK(start.error().type == "RdfMissing");
    CHECK_FALSE(statusOf(target)["scriptFound"].get<bool>());
}

TEST_CASE("debug checks: an error event on a read command is an error, not a document") {
    Scripted s;
    s.rdf.list = R"({"event":"error","error":"git-failed","exit_code":2})";
    DebugChecksTarget target(s.script, s.rdf);
    auto listed = target.invoke("List", json::array());
    REQUIRE_FALSE(listed);
    CHECK(listed.error().type == "RdfFailed");
    CHECK(listed.error().message.find("git-failed") != std::string::npos);
}

TEST_CASE("debug checks: the run state follows the rdf JSON lines and keeps what it cannot parse") {
    fo4ide::app::RunState state;
    state.currentBox = "G16";
    const std::vector<std::string> lines = {
        R"({"event":"plan","boxes":["G16","G15"],"unknown":true})",
        R"({"event":"start","box":"G16","tier":"fast","ref":"nomad/dev","started":"2026-11-20T13:57:01Z"})",
        R"({"event":"phase","box":"G16","name":"build","sha":"abcdef0123"})",
        "building target fo4_tests_assets",
        R"({"event":"phase","box":"G16","name":"test","total":2})",
        R"({"event":"test","box":"G16","name":"A.One","status":"passed","seconds":1.5})",
        R"({"event":"test","box":"G16","name":"A.Two","status":"failed","seconds":2})",
        R"({"event":"future-thing","x":1})",
        R"({"event":"done","box":"G16","status":"FAIL","exit_code":1,"sha":"abcdef0123","result_path":"C:\\r\\G16\\x","line":"FAIL G16 abc1234 1/2 skip=0 4s"})",
        R"({"event":"start","box":"G15","tier":"fast"})",
        R"({"event":"test","name":"B.One","status":"skipped","seconds":0})",
        R"({"event":"error","error":"git-failed","exit_code":2})",
        R"({"event":"test","name":)",
    };
    for (const auto& line : lines) {
        if (auto event = fo4ide::app::parseRdfLine(line)) state.apply(*event);
        else state.note(line);
    }
    const auto out = state.toJson();
    CHECK(out["plan"] == json::array({"G16", "G15"}));
    CHECK(out["planSeen"].get<bool>());
    CHECK(out["unknown"].get<bool>());
    CHECK(out["phase"] == "test");
    REQUIRE(out["tests"].size() == 3);
    CHECK(out["tests"][0]["name"] == "A.One");
    CHECK(out["tests"][0]["status"] == "passed");
    CHECK(out["tests"][0]["seconds"] == 1.5);
    CHECK(out["tests"][1]["status"] == "failed");
    CHECK(out["tests"][2]["box"] == "G15");
    REQUIRE(out["results"].size() == 1);
    CHECK(out["results"][0]["box"] == "G16");
    CHECK(out["results"][0]["status"] == "FAIL");
    CHECK(out["results"][0]["exitCode"] == 1);
    CHECK(out["results"][0]["sha"] == "abcdef0123");
    CHECK(out["results"][0]["line"] == "FAIL G16 abc1234 1/2 skip=0 4s");
    CHECK(out["error"] == "git-failed");
    REQUIRE(out["log"].size() == 2);
    CHECK(out["log"][0] == "building target fo4_tests_assets");
}

TEST_CASE("debug checks: the failure excerpt comes from result.json and is bounded") {
    fo4ide_test::TempRoot root("fo4ide_debug_checks");
    const auto dir = root.path() / "G16" / "20261120-101010-abc1234";
    std::filesystem::create_directories(dir);
    const json result = {{"box", "G16"}, {"status", "FAIL"}, {"passed", 1}, {"failed", 1}, {"total", 2},
                         {"seconds", 4.5}, {"failures", json::array({"A.Two"})},
                         {"tests", json::array({{{"name", "A.One"}, {"status", "passed"}, {"seconds", 1}},
                                                {{"name", "A.Two"}, {"status", "failed"}, {"seconds", 2}}})},
                         {"excerpt", std::string(50000, 'x')}};
    std::ofstream(dir / "result.json") << result.dump();

    const auto fromFile = fo4ide::app::readResultDetail(dir / "result.json");
    const auto fromDir = fo4ide::app::readResultDetail(dir);
    REQUIRE(fromFile.is_object());
    CHECK(fromDir == fromFile);
    CHECK(fromFile["failures"] == json::array({"A.Two"}));
    REQUIRE(fromFile["failedTests"].size() == 1);
    CHECK(fromFile["failedTests"][0]["name"] == "A.Two");
    CHECK(fromFile["excerpt"].get<std::string>().size() < 21000);

    std::ofstream(dir / "other.json") << result.dump();
    CHECK(fo4ide::app::readResultDetail(dir / "other.json").is_null());
}

#if defined(_WIN32)

namespace {

constexpr const char* kStub = R"PS(param([string]$Box,[string]$Tier,[string]$Test,[string]$Base,[switch]$List,[switch]$Tests,[switch]$Results,[switch]$Changed,[switch]$All,[switch]$Json)
$ErrorActionPreference = 'Stop'
if ($List) { '@LIST@'; exit 0 }
if ($Tests) { '@TESTS@'; exit 0 }
if ($All) { Start-Sleep -Seconds 60; exit 0 }
if ($Changed -and $Base -eq 'empty') { '{"event":"plan","boxes":[],"unknown":false}'; exit 0 }
if ($Changed -and $Base -eq 'locked') { '{"event":"error","error":"lock-timeout","exit_code":3}'; exit 3 }
if ($Changed -and $Base -eq 'partial') { '{"event":"plan","boxes":["G16","G15"],"unknown":false}'; '{"event":"done","box":"G16","status":"PASS","exit_code":0,"sha":"abcdef0","result_path":"","line":"PASS G16 abcdef0 1/1 skip=0 1s"}'; exit 1 }
if ($Changed -and $Base -eq 'silent') { [Console]::Error.WriteLine('git exploded'); exit 2 }
[Console]::Error.WriteLine('build noise')
'{"event":"start","box":"G16","tier":"fast"}'
'{"event":"phase","box":"G16","name":"test","total":1}'
('{"event":"test","box":"G16","name":"args=' + $Box + '|' + $Tier + '|' + $Test + '|' + $Base + '","status":"passed","seconds":0.5}')
'{"event":"done","box":"G16","status":"PASS","exit_code":0,"sha":"abcdef0","result_path":"","line":"PASS G16 abcdef0 1/1 skip=0 1s"}'
exit 0
)PS";

std::string stubText() {
    std::string text = kStub;
    const auto swap = [&](const std::string& token, const std::string& value) {
        text.replace(text.find(token), token.size(), value);
    };
    swap("@LIST@", kList);
    swap("@TESTS@", kTests);
    return text;
}

json waitUntilIdle(DebugChecksTarget& target) {
    for (int i = 0; i < 600; ++i) {
        auto status = statusOf(target);
        if (!status["run"]["active"].get<bool>()) return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    FAIL("the run did not end in 30 seconds");
    return nullptr;
}

struct Live {
    fo4ide_test::TempRoot root{"fo4ide_debug_checks"};
    fo4ide::process::ReprocRunner runner;
    std::filesystem::path script = root.path() / "rdf.ps1";
    std::unique_ptr<DebugChecksTarget> target;
    Live() {
        std::ofstream(script) << stubText();
        target = std::make_unique<DebugChecksTarget>(script, runner);
    }
};

}  // namespace

TEST_CASE("debug checks: a box run streams the real launcher arguments and events through a live process") {
    Live s;
    auto started = s.target->invoke("Start", json::array({"box", "g16", "", "LodObjectTileReal.Other"}));
    REQUIRE(started);

    const auto status = waitUntilIdle(*s.target);
    const auto& run = status["run"];
    CHECK(run["kind"] == "box");
    CHECK(run["exitCode"] == 0);
    CHECK(run["error"] == "");
    REQUIRE(run["tests"].size() == 1);
    CHECK(run["tests"][0]["name"] == "args=G16|fast|LodObjectTileReal.Other|");
    REQUIRE(run["results"].size() == 1);
    CHECK(run["results"][0]["status"] == "PASS");
    CHECK(run["results"][0]["line"] == "PASS G16 abcdef0 1/1 skip=0 1s");
    REQUIRE(run["log"].size() == 1);
    CHECK(run["log"][0] == "build noise");
}

TEST_CASE("debug checks: -Changed passes a checked base, and an empty plan is a pass that says nothing was affected") {
    Live s;
    REQUIRE(s.target->invoke("Start", json::array({"changed", "", "", "", "empty"})));
    const auto run = waitUntilIdle(*s.target)["run"];
    CHECK(run["error"] == "");
    CHECK(run["exitCode"] == 0);
    REQUIRE(run["results"].size() == 1);
    CHECK(run["results"][0]["status"] == "PASS");
    CHECK(run["results"][0]["line"] == "No checks affected.");
}

TEST_CASE("debug checks: an error event and a silent non-zero exit both end as errors, never as a pass") {
    Live s;
    REQUIRE(s.target->invoke("Start", json::array({"changed", "", "", "", "locked"})));
    auto run = waitUntilIdle(*s.target)["run"];
    CHECK(run["exitCode"] == 3);
    CHECK(run["results"].empty());
    CHECK(run["error"].get<std::string>().find("lock-timeout") != std::string::npos);

    REQUIRE(s.target->invoke("Start", json::array({"changed", "", "", "", "silent"})));
    run = waitUntilIdle(*s.target)["run"];
    CHECK(run["exitCode"] == 2);
    CHECK(run["results"].empty());
    CHECK(run["error"].get<std::string>().find("exited with code 2") != std::string::npos);
    CHECK(run["error"].get<std::string>().find("git exploded") != std::string::npos);
}

TEST_CASE("debug checks: a changed or all run that ends non-zero with fewer results than planned says how many boxes are missing") {
    Live s;
    REQUIRE(s.target->invoke("Start", json::array({"changed", "", "", "", "partial"})));
    const auto run = waitUntilIdle(*s.target)["run"];
    CHECK(run["exitCode"] == 1);
    CHECK(run["results"].size() == 1);
    CHECK(run["error"] == "1 of 2 boxes did not report");
}

TEST_CASE("debug checks: a second Start is refused while a run is active, and Cancel ends the run as cancelled") {
    Live s;
    REQUIRE(s.target->invoke("Start", json::array({"all", "", "", ""})));
    const auto began = std::chrono::steady_clock::now();
    auto second = s.target->invoke("Start", json::array({"all", "", "", ""}));
    REQUIRE_FALSE(second);
    CHECK(second.error().type == "DebugChecksBusy");

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    auto cancelled = s.target->invoke("Cancel", json::array());
    REQUIRE(cancelled);
    CHECK(json::parse(cancelled->get<std::string>())["cancelled"].get<bool>());
    const auto run = waitUntilIdle(*s.target)["run"];
    CHECK(run["cancelled"].get<bool>());
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(30));

    auto idle = s.target->invoke("Cancel", json::array());
    REQUIRE(idle);
    CHECK_FALSE(json::parse(idle->get<std::string>())["cancelled"].get<bool>());
}

#endif
