#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/texture/dds.hpp>
#include <fo4ide/app/DebugToolsTarget.hpp>

#include "../../src/app/DebugToolsInternal.hpp"
#include "../helpers/NifFixture.hpp"
#include "../helpers/PluginFixtures.hpp"
#include "../helpers/TempRoot.hpp"

namespace {

namespace fs = std::filesystem;
using fo4ide::app::DebugToolsEnv;
using fo4ide::app::DebugToolsTarget;
using nlohmann::json;

struct Host {
    fo4ide_test::TempRoot root{"fo4ide_debug_tools"};
    fs::path dataDir = root.path() / "Data";
    fs::path pluginDir = root.path() / "plugins";
    fs::path outDir = root.path() / "out";
    json settings = json::object();
    std::mutex mutex;
    std::vector<std::pair<std::string, json>> calls;
    std::map<std::string, json> replies;
    std::map<std::string, std::string> errors;
    std::atomic<bool> gate{true};
    std::atomic<bool> entered{false};
    std::string gateKey{"lod.Worlds"};
    bool gateSnapshot = false;
    std::thread::id testThread = std::this_thread::get_id();
    bool gateAssets = false;
    bool withAssets = true;

    Host() {
        fs::create_directories(dataDir);
        fs::create_directories(pluginDir);
        fs::create_directories(outDir);
        settings["DataFolder"] = dataDir.string();
        const json resolved{{"Path", "meshes/a.nif"},
                            {"Found", true},
                            {"Providers", 2},
                            {"Ambiguous", true},
                            {"Winner", {{"Kind", "loose"}, {"Provider", "ModA"}, {"Container", "x"}}},
                            {"Others", json::array({{{"Kind", "ba2"}, {"Provider", "Base"}, {"Container", "y"}}})}};
        replies["nif.ResolveAsset"] = resolved.dump();
        replies["backend.ValidatePlugin"] = json{{"errors", 0}, {"warnings", 0}, {"infos", 0}, {"findings", json::array()}};
        replies["backend.CheckForErrors"] = json("[]");
        replies["nif.AuditAssetUsage"] = json{{"plugin", "A.esp"}, {"referenced", 2}, {"resolved", 2},
                                              {"missing", 0},      {"unusable", 0},   {"skippedArchives", 0}};
        replies["nif.Verify"] = json{{"text", "ok"}, {"checks", json::array()}};
        replies["previs.Scan"] =
            json{{"summary", {{"broken", 0}, {"outdated", 0}}}, {"broken", json::array()}, {"outdated", json::array()}};
        replies["previs.PatchPlan"] = json{{"groupCount", 1}, {"eligibleReferences", 2}, {"groups", json::array()}};
        replies["previs.Generate"] =
            json{{"dryRun", true}, {"references", 3}, {"meshIds", 1}, {"groups", json::array({{{"path", "p"}}})}};
        replies["lod.Worlds"] = json("[{\"name\":\"Commonwealth\",\"stride\":32}]");
        replies["lod.Plan"] = json("{\"plan\":[],\"ledger\":\"\",\"commit\":\"\"}");
        replies["lod.GenerateScoped"] =
            json("{\"report\":{\"apply\":false,\"written\":0,\"failed\":0,\"wouldWrite\":2},\"ledger\":\"\"}");
        replies["lod.VerifyWorld"] = json("{\"missing\":0,\"partial\":0,\"ok\":3,\"stale\":0,\"unverified\":0}");
        replies["lod.CancelScoped"] = json("{}");
        replies["strings.ListStringTables"] = tableList({".strings", ".dlstrings", ".ilstrings"}).dump();
    }

    static json tableList(const std::vector<std::string>& kinds, bool pending = false, int skipped = 0) {
        json tables = json::array();
        for (const auto& kind : kinds) {
            json row{{"kind", kind}, {"path", "Strings/A_en" + kind}, {"count", 3}, {"source", "loose"}};
            if (pending) row["pending"] = true;
            tables.push_back(std::move(row));
        }
        json archives = json::array();
        for (int i = 0; i < skipped; ++i) archives.push_back({{"path", "x.ba2"}, {"reason", "unreadable"}});
        return {{"tables", std::move(tables)}, {"skippedArchives", std::move(archives)}};
    }

    DebugToolsEnv env() {
        DebugToolsEnv e;
        e.call = [this](const std::string& target, const std::string& method, const json& args) -> fo4ide::Result<json> {
            const auto key = target + "." + method;
            {
                std::lock_guard<std::mutex> guard(mutex);
                calls.emplace_back(key, args);
            }
            if (key == gateKey && !gate.load()) {
                entered.store(true);
                while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (const auto failure = errors.find(key); failure != errors.end()) {
                return std::unexpected(fo4ide::Error{failure->second, key + " failed"});
            }
            const auto it = replies.find(key);
            if (it == replies.end()) return std::unexpected(fo4ide::Error{"UnknownMethod", key});
            return it->second;
        };
        e.settings = [this] { return settings; };
        e.snapshot = [this]() -> std::optional<fo4ide::mo2::ProfileSnapshot> {
            if (gateSnapshot && !gate.load() && std::this_thread::get_id() != testThread) {
                entered.store(true);
                while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            fo4ide::mo2::ProfileSnapshot snapshot;
            snapshot.instance.profile = "Default";
            snapshot.loadOrder = {"A.esp"};
            snapshot.plugins.push_back({"A.esp", pluginDir / "A.esp", "mod", true});
            return snapshot;
        };
        e.gameRoot = [this] { return root.path() / "Fallout 4"; };
        if (withAssets) {
            e.assets = [this] {
                if (gateAssets && !gate.load()) {
                    entered.store(true);
                    while (!gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                return std::make_shared<const fo4::archive::AssetResolver>(fo4::archive::AssetResolver::open(dataDir, {}));
            };
        }
        e.timestamp = [] { return std::string("T1"); };
        e.fo4ideVersion = "0.1.0";
        return e;
    }
};

json parse(const fo4ide::Result<json>& result) {
    REQUIRE(result.has_value());
    return json::parse(result->get<std::string>());
}

json statusOf(DebugToolsTarget& target) {
    return parse(target.invoke("Status", json::array()));
}

json waitIdle(DebugToolsTarget& target, std::chrono::seconds limit = std::chrono::seconds(15)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        auto status = statusOf(target);
        if (!status["active"].get<bool>()) return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    FAIL("the debug tool run never finished");
    return {};
}

fo4ide::Result<json> start(DebugToolsTarget& target, const std::string& id, const json& params = json::object()) {
    return target.invoke("Start", json::array({id, params.dump()}));
}

json runTool(DebugToolsTarget& target, const std::string& id, const json& params = json::object(),
             std::chrono::seconds limit = std::chrono::seconds(15)) {
    const auto started = start(target, id, params);
    REQUIRE(started.has_value());
    return waitIdle(target, limit);
}

std::string errorType(const fo4ide::Result<json>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().type;
}

std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeMiniPlugin(const fs::path& path) {
    std::ofstream(path, std::ios::binary) << plugin_fixtures_test::minimalPlugin(0, "");
}

const std::set<std::string> kStatuses{"pass", "fail", "skip", "unsupported", "unverified", "error"};

const std::set<std::string> kAllowedCalls{"nif.ResolveAsset",       "nif.Verify",         "nif.AuditAssetUsage",
                                          "backend.ValidatePlugin", "backend.CheckForErrors", "previs.Scan",
                                          "previs.PatchPlan",       "previs.Generate",    "lod.Worlds",
                                          "lod.Plan",               "lod.GenerateScoped", "lod.VerifyWorld",
                                          "lod.CancelScoped",       "strings.ListStringTables"};

void patchU32Le(std::string& bytes, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset + 4 <= bytes.size());
    for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
}

}  // namespace

TEST_CASE("DebugTools.ListShapeAndInventory", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto list = parse(target.invoke("List", json::array()));
    std::set<std::string> ids;
    for (const auto& group : list["groups"]) {
        for (const auto& tool : group["tools"]) {
            for (const char* key : {"id", "title", "description", "readOnly", "writes", "needsOutputDir", "expensive",
                                    "cancellable", "available", "unavailableReason", "evidence", "params"}) {
                CHECK(tool.contains(key));
            }
            for (const auto& param : tool["params"]) CHECK(param["name"] != "outputDir");
            ids.insert(tool["id"].get<std::string>());
        }
    }
    CHECK(ids.size() == 24);
    bool gap = false;
    std::set<std::string> integrated;
    for (const auto& row : list["inventory"]) {
        const auto status = row["status"].get<std::string>();
        CHECK((status == "integrated" || status == "available" || status == "gap"));
        if (status == "gap") {
            gap = true;
            CHECK(row["toolId"].get<std::string>().empty());
            CHECK_FALSE(row["note"].get<std::string>().empty());
        }
        if (status == "integrated") {
            CHECK(ids.contains(row["toolId"].get<std::string>()));
            integrated.insert(row["toolId"].get<std::string>());
        }
    }
    CHECK(gap);
    for (const char* id : {"nif.corpusRoundtrip", "dds.inspect", "pex.inspect", "strings.inspect", "strings.pluginTables",
                           "audio.inspect", "plugin.textRoundtrip", "previs.vanillaBaseline", "plugin.rowRoundtrip"}) {
        CHECK(integrated.contains(id));
    }
}

TEST_CASE("DebugTools.StatusAnswersWhenIdle", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto status = statusOf(target);
    CHECK_FALSE(status["active"].get<bool>());
    CHECK(status["results"].empty());
}

TEST_CASE("DebugTools.UnavailableToolReportsReasonAndRefusesStart", "[debug-tools]") {
    Host host;
    host.withAssets = false;
    DebugToolsTarget target(host.env());
    const auto list = parse(target.invoke("List", json::array()));
    bool found = false;
    for (const auto& group : list["groups"]) {
        for (const auto& tool : group["tools"]) {
            if (tool["id"] != "nif.roundtrip") continue;
            found = true;
            CHECK_FALSE(tool["available"].get<bool>());
            CHECK_FALSE(tool["unavailableReason"].get<std::string>().empty());
        }
    }
    CHECK(found);
    CHECK(errorType(start(target, "nif.roundtrip", {{"asset", "meshes/a.nif"}})) == "ToolUnavailable");
}

TEST_CASE("DebugTools.StartRejectsUnknownToolAndBadParams", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    CHECK(errorType(start(target, "nope.nothing")) == "UnknownTool");
    CHECK(errorType(start(target, "plugin.validate")) == "BadParams");
    CHECK(errorType(start(target, "plugin.validate", {{"plugin", "A.esp"}, {"extra", "x"}})) == "BadParams");
    CHECK(errorType(start(target, "plugin.validate", {{"plugin", 5}})) == "BadParams");
    CHECK(errorType(target.invoke("Start", json::array({"plugin.validate", "{not json"}))) == "BadParams");
    CHECK(errorType(start(target, "plugin.validate", {{"plugin", "A.esp"}, {"outputDir", "x"}})) == "BadParams");
}

TEST_CASE("DebugTools.OnlyOneRunAtATime", "[debug-tools]") {
    Host host;
    host.gate.store(false);
    DebugToolsTarget target(host.env());
    REQUIRE(start(target, "lod.worlds").has_value());
    for (int i = 0; i < 1000 && !host.entered.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(host.entered.load());
    CHECK(errorType(start(target, "lod.worlds")) == "DebugToolsBusy");
    host.gate.store(true);
    const auto status = waitIdle(target);
    CHECK(status["results"].size() == 2);
}

TEST_CASE("DebugTools.UnknownReplyNeverPasses", "[debug-tools]") {
    Host host;
    host.replies["backend.ValidatePlugin"] = json{{"surprise", true}};
    host.replies["nif.AuditAssetUsage"] = json{{"surprise", true}};
    host.replies["lod.Worlds"] = json("not json at all");
    DebugToolsTarget target(host.env());
    const std::vector<std::pair<std::string, json>> runs{{"plugin.validate", {{"plugin", "A.esp"}}},
                                                         {"assets.audit", {{"plugin", "A.esp"}}},
                                                         {"lod.worlds", json::object()}};
    for (const auto& [id, params] : runs) {
        const auto status = runTool(target, id, params);
        CHECK(status["results"][0]["status"] == "error");
    }
}

TEST_CASE("DebugTools.MissingDataFolderIsSkipNotFail", "[debug-tools]") {
    Host host;
    host.settings["DataFolder"] = (host.root.path() / "NoSuchData").string();
    DebugToolsTarget target(host.env());
    for (const char* id : {"previs.scan", "lod.worlds", "previs.vanillaBaseline"}) {
        const auto status = runTool(target, id);
        CHECK(status["results"][0]["status"] == "skip");
    }
    std::lock_guard<std::mutex> guard(host.mutex);
    CHECK(host.calls.empty());
}

TEST_CASE("DebugTools.SettingsAreScrubbed", "[debug-tools]") {
    Host host;
    host.settings["AnthropicApiKey"] = "sk-secret";
    host.settings["GeminiApiKey"] = "g-secret";
    host.settings["hasAnthropicApiKey"] = true;
    host.settings["OllamaUrl"] = "http://10.0.0.5:11434";
    host.settings["SomeToken"] = "tok";
    host.settings["extra_password"] = "pw";
    host.settings["Model"] = "m1";
    DebugToolsTarget target(host.env());
    const auto context = parse(target.invoke("Context", json::array()));
    const auto& settings = context["settings"];
    for (const char* key :
         {"AnthropicApiKey", "GeminiApiKey", "hasAnthropicApiKey", "OllamaUrl", "SomeToken", "extra_password"}) {
        CHECK_FALSE(settings.contains(key));
    }
    CHECK(settings["Model"] == "m1");
    CHECK(context["versions"]["fo4ide"] == "0.1.0");
    CHECK(context["loadOrder"].size() == 1);
    CHECK(context.dump().find("sk-secret") == std::string::npos);
}

TEST_CASE("DebugTools.ScratchValidatorRefusesUnsafeFolders", "[debug-tools]") {
    Host host;
    const std::vector<fs::path> guarded{host.dataDir, host.pluginDir};
    CHECK(fo4ide::app::validateScratchParent(host.outDir, guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent("relative/dir", guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent(host.root.path() / "missing", guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent(host.dataDir, guarded, "S1").has_value());
    fs::create_directories(host.dataDir / "sub");
    CHECK_FALSE(fo4ide::app::validateScratchParent(host.dataDir / "sub", guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent(host.pluginDir, guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent(host.outDir / ".." / "out", guarded, "S1").has_value());
    fs::create_directories(host.outDir / "debugtools-S1");
    const auto existing = fo4ide::app::validateScratchParent(host.outDir, guarded, "S1");
    REQUIRE_FALSE(existing.has_value());
    CHECK(existing.error().type == "UnsafeOutputDir");
    const auto sibling = host.root.path() / "Data2";
    fs::create_directories(sibling);
    CHECK(fo4ide::app::validateScratchParent(sibling, guarded, "S1").has_value());
}

TEST_CASE("DebugTools.RoundtripStartEnforcesOutputDirRules", "[debug-tools]") {
    Host host;
    writeMiniPlugin(host.pluginDir / "A.esp");
    DebugToolsTarget target(host.env());
    CHECK(errorType(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}})) == "BadParams");
    CHECK(errorType(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", ""}})) == "BadParams");
    for (const auto& dir : {host.dataDir, host.dataDir / "inner", host.pluginDir, host.root.path() / "Fallout 4"}) {
        fs::create_directories(dir);
        CHECK(errorType(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", dir.string()}})) ==
              "UnsafeOutputDir");
    }
    CHECK(errorType(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", "rel"}})) == "UnsafeOutputDir");
    fs::create_directories(host.outDir / "debugtools-T1");
    CHECK(errorType(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}})) ==
          "UnsafeOutputDir");
}

TEST_CASE("DebugTools.RoundtripWritesOnlyUnderScratch", "[debug-tools]") {
    Host host;
    writeMiniPlugin(host.pluginDir / "A.esp");
    const auto original = readAll(host.pluginDir / "A.esp");
    DebugToolsTarget target(host.env());
    const auto status = runTool(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}});
    const auto& result = status["results"][0];
    CHECK(kStatuses.contains(result["status"].get<std::string>()));
    CHECK(result["status"] == "pass");
    CHECK(result["left"]["value"].get<std::string>().find("bytes") != std::string::npos);
    CHECK(result["right"]["value"].get<std::string>().find("bytes") != std::string::npos);
    const auto scratch = host.outDir / "debugtools-T1";
    REQUIRE(fs::is_regular_file(scratch / "A.esp"));
    REQUIRE(result["outputs"].size() == 1);
    CHECK(fs::equivalent(fs::path(result["outputs"][0].get<std::string>()), scratch / "A.esp"));
    std::size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(host.root.path())) files += entry.is_regular_file() ? 1 : 0;
    CHECK(files == 2);
    CHECK(readAll(host.pluginDir / "A.esp") == original);
    CHECK(fs::is_empty(host.dataDir));
}

TEST_CASE("DebugTools.RoundtripPassesWhenOnlyTheHedrRecordCountWasStale", "[debug-tools]") {
    Host host;
    auto bytes = plugin_fixtures_test::minimalPlugin(0, "");
    patchU32Le(bytes, 34, 999);
    const auto source = host.pluginDir / "A.esp";
    std::ofstream(source, std::ios::binary) << bytes;
    DebugToolsTarget target(host.env());
    const auto result =
        runTool(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}})["results"][0];
    CHECK(result["status"] == "pass");
    REQUIRE(result["diffs"].size() == 1);
    CHECK(result["diffs"][0].get<std::string>().find("HEDR record count at offset 34") != std::string::npos);
    CHECK(result["diffs"][0].get<std::string>().find("shipped 999") != std::string::npos);
    REQUIRE(result["outputs"].size() == 1);
    CHECK(fs::is_regular_file(fs::path(result["outputs"][0].get<std::string>())));
    CHECK(readAll(source) == bytes);
}

TEST_CASE("DebugTools.RoundtripFailsWhenTheRebuildChangesBytesBesideTheHedrCount", "[debug-tools]") {
    Host host;
    auto bytes = plugin_fixtures_test::minimalPlugin(0, "");
    patchU32Le(bytes, 34, 999);
    const auto source = host.pluginDir / "A.esm";
    std::ofstream(source, std::ios::binary) << bytes;
    DebugToolsTarget target(host.env());
    const auto status =
        runTool(target, "plugin.roundtrip", {{"plugin", source.string()}, {"outputDir", host.outDir.string()}});
    const auto& result = status["results"][0];
    CHECK(result["status"] == "fail");
    REQUIRE(result["diffs"].size() >= 1);
    CHECK(result["diffs"][0] == "first differing byte at offset 8");
    REQUIRE(result["outputs"].size() == 1);
    CHECK(fs::is_regular_file(fs::path(result["outputs"][0].get<std::string>())));
    CHECK(readAll(source) == bytes);
}

TEST_CASE("DebugTools.RoundtripFailsWithParseBackCountsWhenTheRebuildChangesMoreThanTheHedrCount", "[debug-tools]") {
    Host host;
    // Constraint: a .esm without the master flag is rebuilt with the flag set, so only the TES4 flags at offset 8 change; the TES4 itself is the one record read back.
    const auto source = host.pluginDir / "B.esm";
    const auto bytes = plugin_fixtures_test::minimalPlugin(0, "");
    std::ofstream(source, std::ios::binary) << bytes;
    DebugToolsTarget target(host.env());
    const auto status =
        runTool(target, "plugin.roundtrip", {{"plugin", source.string()}, {"outputDir", host.outDir.string()}});
    const auto& result = status["results"][0];
    CHECK(result["status"] == "fail");
    REQUIRE(result["diffs"].size() == 4);
    CHECK(result["diffs"][0] == "first differing byte at offset 8");
    CHECK(result["diffs"][1] == "masters: 0 -> 0");
    CHECK(result["diffs"][2] == "records: 1 -> 1");
    CHECK(result["diffs"][3] == "groups: 0 -> 0");
    REQUIRE(result["outputs"].size() == 1);
    CHECK(fs::is_regular_file(fs::path(result["outputs"][0].get<std::string>())));
    CHECK(readAll(source) == bytes);
}

TEST_CASE("DebugTools.PrevisAndLodAdaptersStayDryRunAndAlwaysAddUnverifiedRow", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const std::vector<std::pair<std::string, json>> runs{
        {"previs.scan", json::object()},
        {"previs.plan", {{"plugin", "A.esp"}, {"cell", "000ABC:Fallout4.esm"}}},
        {"previs.dryRunGenerate", {{"plugin", "A.esp"}, {"cell", "000ABC:Fallout4.esm"}}},
        {"previs.vanillaBaseline", json::object()},
        {"lod.worlds", json::object()},
        {"lod.plan", json::object()},
        {"lod.dryRunGenerate", {{"world", "Commonwealth"}}},
        {"lod.verifyWorld", {{"world", "Commonwealth"}}}};
    for (const auto& [id, params] : runs) {
        const auto status = runTool(target, id, params);
        const auto& results = status["results"];
        REQUIRE(results.size() >= 2);
        const auto& last = results[results.size() - 1];
        CHECK(last["status"] == "unverified");
        CHECK(last["title"] == "In-game acceptance and RVIS/visibility behaviour");
        CHECK(last["detail"].get<std::string>().find("PREVIS.md:1013,1051") != std::string::npos);
        CHECK(last["detail"].get<std::string>().find("lodgen-plan.md:44-49") != std::string::npos);
        for (const auto& result : results) {
            CHECK(kStatuses.contains(result["status"].get<std::string>()));
            for (const auto& kind : result["evidence"]) CHECK(kind != "observed-in-game");
        }
    }
    std::lock_guard<std::mutex> guard(host.mutex);
    bool sawGenerate = false;
    bool sawScoped = false;
    for (const auto& [key, args] : host.calls) {
        CHECK(kAllowedCalls.contains(key));
        if (key == "previs.Generate") {
            sawGenerate = true;
            CHECK(args[5] == json(false));
            CHECK(args[8] == json(false));
        }
        if (key == "lod.GenerateScoped") {
            sawScoped = true;
            CHECK(args[5] == json(false));
        }
    }
    CHECK(sawGenerate);
    CHECK(sawScoped);
}

TEST_CASE("DebugTools.DryRunThatWritesFailsAndEmptyLedgerVerifyIsUnverified", "[debug-tools]") {
    Host host;
    host.replies["lod.GenerateScoped"] = json("{\"report\":{\"apply\":false,\"written\":3,\"failed\":0},\"ledger\":\"\"}");
    host.replies["lod.VerifyWorld"] = json("{\"missing\":0,\"partial\":0,\"ok\":0,\"stale\":4,\"unverified\":0}");
    host.replies["previs.Generate"] = json{{"dryRun", false}, {"references", 1}, {"groups", json::array()}};
    DebugToolsTarget target(host.env());
    CHECK(runTool(target, "lod.dryRunGenerate", {{"world", "W"}})["results"][0]["status"] == "fail");
    CHECK(runTool(target, "lod.verifyWorld", {{"world", "W"}})["results"][0]["status"] == "unverified");
    CHECK(runTool(target, "previs.dryRunGenerate", {{"plugin", "A.esp"}, {"cell", "1:A.esp"}})["results"][0]["status"] ==
          "fail");
}

TEST_CASE("DebugTools.AssetPrecedenceWithSeveralProvidersIsUnverified", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto status = runTool(target, "env.assetPrecedence", {{"asset", "meshes/a.nif"}});
    const auto& result = status["results"][0];
    CHECK(result["status"] == "unverified");
    CHECK(result["detail"].get<std::string>().find("not proven engine order") != std::string::npos);
    CHECK(result["diffs"].size() == 1);
}

namespace {

void waitEntered(Host& host) {
    for (int i = 0; i < 1000 && !host.entered.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(host.entered.load());
}

std::size_t countCalls(Host& host, const std::string& key) {
    std::lock_guard<std::mutex> guard(host.mutex);
    std::size_t count = 0;
    for (const auto& call : host.calls) count += call.first == key ? 1 : 0;
    return count;
}

}  // namespace

TEST_CASE("DebugTools.StartReturnsNumericRunIdAndStatusTypesArePinned", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto idle = statusOf(target);
    CHECK(idle["active"].is_boolean());
    CHECK(idle["cancelled"].is_boolean());
    CHECK(idle["runId"].is_number_integer());
    CHECK(idle["progress"]["done"].is_number_integer());
    CHECK(idle["progress"]["total"].is_number_integer());
    const auto started = parse(target.invoke("Start", json::array({"lod.worlds", "{}"})));
    CHECK(started["runId"].is_number_integer());
    const auto status = waitIdle(target);
    CHECK(status["runId"] == started["runId"]);
    CHECK(status["progress"]["done"].is_number_integer());
    CHECK(status["progress"]["total"].is_number_integer());
    CHECK(status["cancelled"].is_boolean());
}

TEST_CASE("DebugTools.CancelCallsCancelScopedOnlyForCancellableTools", "[debug-tools]") {
    Host host;
    host.gateKey = "lod.GenerateScoped";
    host.gate.store(false);
    DebugToolsTarget target(host.env());
    CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == false);
    REQUIRE(start(target, "lod.dryRunGenerate", {{"world", "W"}}).has_value());
    waitEntered(host);
    CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == true);
    CHECK(countCalls(host, "lod.CancelScoped") == 1);
    host.gate.store(true);
    CHECK_FALSE(waitIdle(target)["cancelled"].get<bool>());

    host.gateKey = "backend.ValidatePlugin";
    host.entered.store(false);
    host.gate.store(false);
    REQUIRE(start(target, "plugin.validate", {{"plugin", "A.esp"}}).has_value());
    waitEntered(host);
    CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == false);
    CHECK(countCalls(host, "lod.CancelScoped") == 1);
    host.gate.store(true);
    CHECK_FALSE(waitIdle(target)["cancelled"].get<bool>());
}

TEST_CASE("DebugTools.CancelBeforeGenerateSkipsAndReportsCancelled", "[debug-tools]") {
    {
        Host host;
        host.gateSnapshot = true;
        host.gate.store(false);
        DebugToolsTarget target(host.env());
        REQUIRE(start(target, "lod.dryRunGenerate", {{"world", "W"}}).has_value());
        waitEntered(host);
        CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == true);
        host.gate.store(true);
        const auto status = waitIdle(target);
        CHECK(status["results"][0]["status"] == "skip");
        CHECK(status["results"][0]["detail"] == "cancelled before start");
        CHECK(status["cancelled"].get<bool>());
        CHECK(countCalls(host, "lod.GenerateScoped") == 0);
    }
    Host host;
    host.replies["lod.GenerateScoped"] =
        json("{\"report\":{\"apply\":false,\"written\":0,\"failed\":0,\"cancelled\":true},\"ledger\":\"\"}");
    DebugToolsTarget target(host.env());
    const auto status = runTool(target, "lod.dryRunGenerate", {{"world", "W"}});
    CHECK(status["results"][0]["status"] == "skip");
    CHECK(status["cancelled"].get<bool>());
}

TEST_CASE("DebugTools.ShutdownJoinsAnActiveRun", "[debug-tools]") {
    Host host;
    host.gateKey = "lod.GenerateScoped";
    host.gate.store(false);
    DebugToolsTarget target(host.env());
    REQUIRE(start(target, "lod.dryRunGenerate", {{"world", "W"}}).has_value());
    waitEntered(host);
    std::atomic<bool> shutdownDone{false};
    std::thread shutter([&] {
        target.shutdown();
        shutdownDone.store(true);
    });
    for (int i = 0; i < 1000 && countCalls(host, "lod.CancelScoped") == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(countCalls(host, "lod.CancelScoped") == 1);
    CHECK_FALSE(shutdownDone.load());
    host.gate.store(true);
    shutter.join();
    CHECK_FALSE(statusOf(target)["active"].get<bool>());
}

// Detects: closing FO4IDE while a tool that Cancel cannot stop is running must stop it at its next step, not wait for it to rebuild, write and compare the whole plugin.
TEST_CASE("DebugTools.ShutdownStopsANonCancellableToolAtItsNextStep", "[debug-tools]") {
    Host host;
    writeMiniPlugin(host.pluginDir / "A.esp");
    host.gateSnapshot = true;
    host.gate.store(false);
    DebugToolsTarget target(host.env());
    REQUIRE(start(target, "plugin.roundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}}).has_value());
    waitEntered(host);
    CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == false);
    std::atomic<bool> shutdownDone{false};
    std::thread shutter([&] {
        target.shutdown();
        shutdownDone.store(true);
    });
    for (int i = 0; i < 1000 && !target.stopping(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(target.stopping());
    CHECK_FALSE(shutdownDone.load());
    host.gate.store(true);
    shutter.join();
    CHECK(shutdownDone.load());
    const auto status = statusOf(target);
    CHECK_FALSE(status["active"].get<bool>());
    CHECK(status["cancelled"].get<bool>());
    CHECK(status["results"][0]["status"] == "skip");
    CHECK_FALSE(fs::exists(host.outDir / "debugtools-T1" / "A.esp"));
}

TEST_CASE("DebugTools.ContextReportsDataDirAndGameBuild", "[debug-tools]") {
    Host host;
    const unsigned char exe[] = {0x01, 0x02, 0x03, 0x04, 0xBD, 0x04, 0xEF, 0xFE, 0x00, 0x00, 0x01, 0x00,
                                 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA3, 0x00, 0x05, 0x06, 0x07, 0x08};
    std::ofstream(host.root.path() / "Fallout4.exe", std::ios::binary)
        .write(reinterpret_cast<const char*>(exe), static_cast<std::streamsize>(sizeof(exe)));
    DebugToolsTarget target(host.env());
    const auto context = parse(target.invoke("Context", json::array()));
    CHECK(context["dataDir"] == host.dataDir.string());
    CHECK(context["gameBuild"] == "1.10.163.0");
    CHECK(context["mo2Profile"] == "Default");
    CHECK_FALSE(context["versions"]["radicleAbi"].get<std::string>().empty());
}

TEST_CASE("DebugTools.PrevisScanNeverPassesWithProblemsTruncationOrPendingPlugins", "[debug-tools]") {
    const auto scan = [](const json& reply) {
        Host host;
        host.replies["previs.Scan"] = reply;
        DebugToolsTarget target(host.env());
        return runTool(target, "previs.scan")["results"][0];
    };
    const json clean{{"summary", {{"broken", 0}, {"outdated", 0}, {"problems", 0}, {"skipped", 0}}},
                     {"broken", json::array()}, {"outdated", json::array()}, {"truncated", false},
                     {"pendingPlugins", json::array()}};
    CHECK(scan(clean)["status"] == "pass");

    auto problems = clean;
    problems["summary"]["problems"] = 2;
    const auto withProblems = scan(problems);
    CHECK(withProblems["status"] == "unverified");
    CHECK(withProblems["detail"].get<std::string>().find("2 previs data problem") != std::string::npos);

    auto truncated = clean;
    truncated["truncated"] = true;
    const auto withTruncation = scan(truncated);
    CHECK(withTruncation["status"] == "unverified");
    CHECK(withTruncation["detail"].get<std::string>().find("truncated") != std::string::npos);

    auto pending = clean;
    pending["pendingPlugins"] = json::array({"Missing.esp"});
    const auto withPending = scan(pending);
    CHECK(withPending["status"] == "unverified");
    CHECK(withPending["detail"].get<std::string>().find("on-disk plugins only") != std::string::npos);

    auto skipped = clean;
    skipped["summary"]["skipped"] = 3;
    const auto withSkipped = scan(skipped);
    CHECK(withSkipped["status"] == "unverified");
    CHECK(withSkipped["detail"].get<std::string>().find("3 cell(s) were skipped as unsupported") != std::string::npos);

    auto broken = clean;
    broken["summary"]["broken"] = 1;
    CHECK(scan(broken)["status"] == "fail");
}

TEST_CASE("DebugTools.LodVerifyWithLedgerFailsOnStaleTilesAlone", "[debug-tools]") {
    const auto verify = [](const char* reply, const json& params) {
        Host host;
        host.replies["lod.VerifyWorld"] = json(reply);
        DebugToolsTarget target(host.env());
        return runTool(target, "lod.verifyWorld", params)["results"][0];
    };
    const char* staleOnly = "{\"missing\":0,\"partial\":0,\"ok\":2,\"stale\":3,\"unverified\":0}";
    const auto withLedger = verify(staleOnly, {{"world", "W"}, {"ledger", "ledger text"}});
    CHECK(withLedger["status"] == "fail");
    CHECK(withLedger["detail"].get<std::string>().find("3 stale") != std::string::npos);
    CHECK(withLedger["detail"].get<std::string>().find("matches the ledger") == std::string::npos);
    CHECK(verify(staleOnly, {{"world", "W"}})["status"] == "unverified");
    const char* clean = "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0}";
    CHECK(verify(clean, {{"world", "W"}, {"ledger", "ledger text"}})["status"] == "pass");
    const json withLedgerArgs{{"world", "W"}, {"ledger", "ledger text"}};
    const char* textureMissing =
        "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0,\"texturesMissing\":2}";
    const auto missingResult = verify(textureMissing, withLedgerArgs);
    CHECK(missingResult["status"] == "unverified");
    CHECK(missingResult["detail"].get<std::string>().find("2 map(s) were never generated") != std::string::npos);
    CHECK(verify(textureMissing, {{"world", "W"}})["status"] == "unverified");
    const char* textureLost =
        "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0,\"texturesMissing\":2,\"texturesMissingRecorded\":1}";
    const auto lostResult = verify(textureLost, withLedgerArgs);
    CHECK(lostResult["status"] == "fail");
    CHECK(lostResult["detail"].get<std::string>().find("1 recorded texture map(s) are missing") != std::string::npos);
    CHECK(verify(textureLost, {{"world", "W"}})["status"] == "unverified");
    const char* textureNeverRecorded =
        "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0,\"texturesMissing\":2,\"texturesMissingRecorded\":0}";
    const auto neverResult = verify(textureNeverRecorded, withLedgerArgs);
    CHECK(neverResult["status"] == "unverified");
    CHECK(neverResult["detail"].get<std::string>().find("2 map(s) were never generated") != std::string::npos);
    const char* textureUnverified =
        "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0,\"texturesUnverified\":1}";
    CHECK(verify(textureUnverified, withLedgerArgs)["status"] == "unverified");
    CHECK(verify(textureUnverified, {{"world", "W"}})["status"] == "unverified");
    const char* textureStale =
        "{\"missing\":0,\"partial\":0,\"ok\":5,\"stale\":0,\"unverified\":0,\"texturesStale\":1}";
    CHECK(verify(textureStale, withLedgerArgs)["status"] == "fail");
    CHECK(verify(textureStale, {{"world", "W"}})["status"] == "unverified");
}

TEST_CASE("DebugTools.PrevisPlanNeverPassesWithSkippedReferences", "[debug-tools]") {
    const auto plan = [](const json& reply) {
        Host host;
        host.replies["previs.PatchPlan"] = reply;
        DebugToolsTarget target(host.env());
        return runTool(target, "previs.plan", {{"plugin", "A.esp"}, {"cell", "000ABC:Fallout4.esm"}})["results"][0];
    };
    const json clean{{"groupCount", 1}, {"eligibleReferences", 2}, {"groups", json::array()},
                     {"skipped", {{"scripted", 0}, {"enableParent", 0}}}};
    CHECK(plan(clean)["status"] == "pass");

    auto scripted = clean;
    scripted["skipped"]["scripted"] = 2;
    scripted["skipped"]["enableParent"] = 1;
    const auto withSkips = plan(scripted);
    CHECK(withSkips["status"] == "unverified");
    const auto detail = withSkips["detail"].get<std::string>();
    CHECK(detail.find("3 reference(s) were skipped") != std::string::npos);
    CHECK(detail.find("scripted 2") != std::string::npos);
    CHECK(detail.find("enableParent 1") != std::string::npos);
    CHECK(withSkips["diffs"].size() == 2);

    auto foreign = clean;
    foreign["skipped"]["foreignPlugin"] = 4;
    const auto withForeign = plan(foreign);
    CHECK(withForeign["status"] == "unverified");
    CHECK(withForeign["detail"].get<std::string>().find("foreignPlugin 4") != std::string::npos);

    auto noSkipped = clean;
    noSkipped.erase("skipped");
    CHECK(plan(noSkipped)["status"] == "pass");

    auto omitted = clean;
    omitted["groupsOmitted"] = 7;
    const auto withOmitted = plan(omitted);
    CHECK(withOmitted["status"] == "pass");
    CHECK(withOmitted["detail"].get<std::string>().find("7 group(s) are counted but not listed") != std::string::npos);
}

TEST_CASE("DebugTools.PrevisDryRunShowsLimitationsAndOnlyExpectedOnesPass", "[debug-tools]") {
    const auto dryRun = [](const json& limitations) {
        Host host;
        json reply{{"dryRun", true}, {"references", 3}, {"meshIds", 1}, {"groups", json::array({{{"path", "p"}}})}};
        if (!limitations.is_null()) reply["limitations"] = limitations;
        host.replies["previs.Generate"] = reply;
        DebugToolsTarget target(host.env());
        return runTool(target, "previs.dryRunGenerate", {{"plugin", "A.esp"}, {"cell", "1:A.esp"}})["results"][0];
    };
    const auto hasDiff = [](const json& row, const std::string& text) {
        for (const auto& diff : row["diffs"]) {
            if (diff.get<std::string>() == text) return true;
        }
        return false;
    };
    CHECK(dryRun(nullptr)["status"] == "pass");
    CHECK(dryRun(json::array())["status"] == "pass");

    const auto expected = dryRun(json::array({"tome_not_written", "dry_run_only"}));
    CHECK(expected["status"] == "pass");
    CHECK(hasDiff(expected, "limitation: dry_run_only"));
    CHECK(hasDiff(expected, "limitation: tome_not_written"));

    const auto foreign = dryRun(json::array({"foreign_plugin_refs:4", "tome_not_written", "dry_run_only"}));
    CHECK(foreign["status"] == "unverified");
    CHECK(hasDiff(foreign, "limitation: foreign_plugin_refs:4"));
    CHECK(foreign["detail"].get<std::string>().find("foreign_plugin_refs:4") != std::string::npos);

    const auto rvis = dryRun(json::array({"rvis_cluster_cell", "dry_run_only"}));
    CHECK(rvis["status"] == "unverified");
    CHECK(rvis["detail"].get<std::string>().find("rvis_cluster_cell") != std::string::npos);

    const auto mixed = dryRun(json::array({"foreign_plugin_refs:1", "rvis_cluster_cell", "dry_run_only"}));
    CHECK(mixed["status"] == "unverified");
    CHECK(mixed["detail"].get<std::string>().find("foreign_plugin_refs:1, rvis_cluster_cell") != std::string::npos);
    CHECK(mixed["detail"].get<std::string>().find("dry_run_only") == std::string::npos);
}

TEST_CASE("DebugTools.PrevisPlanUsesUnsignedNumbers", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    runTool(target, "previs.plan", {{"plugin", "A.esp"}, {"cell", "000ABC:Fallout4.esm"}});
    std::lock_guard<std::mutex> guard(host.mutex);
    bool seen = false;
    for (const auto& [key, args] : host.calls) {
        if (key != "previs.PatchPlan") continue;
        seen = true;
        CHECK(args[2].is_number_unsigned());
        CHECK(args[4].is_number_unsigned());
    }
    CHECK(seen);
}

TEST_CASE("DebugTools.PluginNotLoadedIsSkipAndSkipRowsCarryNoEvidence", "[debug-tools]") {
    Host host;
    host.errors["backend.ValidatePlugin"] = "PluginNotLoaded";
    host.errors["nif.AuditAssetUsage"] = "SomethingElse";
    DebugToolsTarget target(host.env());
    const auto skipped = runTool(target, "plugin.validate", {{"plugin", "A.esp"}})["results"][0];
    CHECK(skipped["status"] == "skip");
    CHECK(skipped["evidence"].empty());
    const auto failed = runTool(target, "assets.audit", {{"plugin", "A.esp"}})["results"][0];
    CHECK(failed["status"] == "error");
    CHECK(failed["evidence"].empty());
    const auto passed = runTool(target, "plugin.checkErrors", {{"plugin", "A.esp"}})["results"][0];
    CHECK(passed["status"] == "pass");
    CHECK_FALSE(passed["evidence"].empty());
}

TEST_CASE("DebugTools.AssetAuditDoesNotPassWhenArchivesWereSkipped", "[debug-tools]") {
    Host host;
    host.replies["nif.AuditAssetUsage"]["skippedArchives"] = 1;
    DebugToolsTarget target(host.env());
    const auto result = runTool(target, "assets.audit", {{"plugin", "A.esp"}})["results"][0];
    CHECK(result["status"] == "unverified");
    host.replies["nif.AuditAssetUsage"]["missing"] = 1;
    const auto missing = runTool(target, "assets.audit", {{"plugin", "A.esp"}})["results"][0];
    CHECK(missing["status"] == "fail");
}

TEST_CASE("DebugTools.ScratchValidatorHandlesExtendedAndNonAsciiPaths", "[debug-tools]") {
    Host host;
    const fs::path accented = host.root.path() / fs::path(std::u8string(u8"Donn\u00e9es"));
    fs::create_directories(accented / "sub");
    const std::vector<fs::path> guarded{accented};
    CHECK_FALSE(fo4ide::app::validateScratchParent(accented / "sub", guarded, "S1").has_value());
    CHECK_FALSE(fo4ide::app::validateScratchParent(accented, guarded, "S1").has_value());
    CHECK(fo4ide::app::validateScratchParent(host.outDir, guarded, "S1").has_value());
#ifdef _WIN32
    const std::vector<fs::path> dataGuard{host.dataDir};
    const fs::path prefixed{L"\\\\?\\" + host.dataDir.wstring()};
    CHECK_FALSE(fo4ide::app::validateScratchParent(prefixed, dataGuard, "S1").has_value());
    fs::create_directories(host.dataDir / "inner");
    const fs::path prefixedInner{L"\\\\?\\" + (host.dataDir / "inner").wstring()};
    CHECK_FALSE(fo4ide::app::validateScratchParent(prefixedInner, dataGuard, "S1").has_value());
#endif
}

namespace {

template <class Bytes>
void writeBytes(const fs::path& path, const Bytes& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool hasRow(const json& result, const std::string& text) {
    for (const auto& row : result["diffs"]) {
        if (row.get<std::string>().find(text) != std::string::npos) return true;
    }
    return false;
}

std::map<std::string, std::pair<std::uintmax_t, fs::file_time_type::rep>> dataListing(const fs::path& data) {
    std::map<std::string, std::pair<std::uintmax_t, fs::file_time_type::rep>> files;
    for (const auto& entry : fs::recursive_directory_iterator(data)) {
        if (!entry.is_regular_file()) continue;
        files[entry.path().string()] = {entry.file_size(), entry.last_write_time().time_since_epoch().count()};
    }
    return files;
}

std::vector<std::uint8_t> bc1Texture(std::size_t payloadBytes) {
    const auto format = fo4::texture::lookup_format(71);
    REQUIRE(format.has_value());
    const fo4::texture::DdsInfo info{8, 8, 1, false, 1, 1, 148, *format, 0};
    auto bytes = fo4::texture::build_dds_header(info);
    bytes.resize(bytes.size() + payloadBytes, 0);
    return bytes;
}

std::vector<std::uint8_t> legacyTexture(std::uint32_t fourCc) {
    std::vector<std::uint8_t> bytes(128, 0);
    const auto put = [&](std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(0, 0x20534444U);
    put(4, 124U);
    put(12, 4U);
    put(16, 4U);
    put(28, 1U);
    put(80, 0x4U);
    put(84, fourCc);
    return bytes;
}

std::string syntheticScript(std::uint8_t isConstByte = 0) {
    std::string bytes;
    const auto u8 = [&](std::uint32_t value) { plugin_fixtures_test::appendLittleEndian(bytes, value, 1); };
    const auto u16 = [&](std::uint32_t value) { plugin_fixtures_test::appendLittleEndian(bytes, value, 2); };
    const auto u32 = [&](std::uint32_t value) { plugin_fixtures_test::appendLittleEndian(bytes, value, 4); };
    const auto text = [&](const std::string& value) {
        u16(static_cast<std::uint32_t>(value.size()));
        bytes += value;
    };
    u32(0xFA57C0DEU);
    u8(3);
    u8(9);
    u16(2);
    for (int i = 0; i < 8; ++i) u8(0);
    text("Synthetic.psc");
    text("");
    text("");
    u16(2);
    text("SyntheticObject");
    text("");
    u8(0);
    u16(0);
    u16(1);
    u16(0);
    u32(23);
    u16(1);
    u16(1);
    u8(isConstByte);
    u32(0);
    u16(1);
    u16(0);
    u16(0);
    u16(0);
    u16(0);
    return bytes;
}

std::vector<std::byte> pcmWave() {
    std::vector<std::byte> out;
    const auto text = [&](const char* value) {
        for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>(value[i]));
    };
    const auto u32 = [&](std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
    };
    const auto u16 = [&](std::uint16_t value) {
        out.push_back(static_cast<std::byte>(value & 0xFFU));
        out.push_back(static_cast<std::byte>(value >> 8));
    };
    text("RIFF");
    u32(4 + 24 + 8 + 8);
    text("WAVE");
    text("fmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(22050);
    u32(44100);
    u16(2);
    u16(16);
    text("data");
    u32(8);
    out.resize(out.size() + 8, std::byte{0x10});
    return out;
}

}  // namespace

TEST_CASE("DebugTools.NifCorpusOnlyPassesAFullListingAndEveryMismatchFails", "[debug-tools]") {
    Host host;
    const auto nif = fo4ide::test::triangleNif();
    for (int i = 0; i < 5; ++i) writeBytes(host.dataDir / "meshes" / "t" / ("a" + std::to_string(i) + ".nif"), nif);
    DebugToolsTarget target(host.env());

    const auto every = runTool(target, "nif.corpusRoundtrip", {{"limit", "all"}})["results"][0];
    CHECK(every["status"] == "pass");
    CHECK(every["left"]["value"] == "5 of 5");
    const auto byDefault = runTool(target, "nif.corpusRoundtrip")["results"][0];
    CHECK(byDefault["status"] == every["status"]);
    const auto sample = runTool(target, "nif.corpusRoundtrip", {{"limit", "2"}})["results"][0];
    CHECK(sample["status"] == "unverified");
    CHECK(sample["left"]["value"] == "2 of 5");
    const auto none = runTool(target, "nif.corpusRoundtrip", {{"prefix", "meshes/none/"}})["results"][0];
    CHECK(none["status"] == "skip");
    CHECK(none["evidence"].empty());
    for (const char* limit : {"0", "zero", "-3"}) {
        CHECK(runTool(target, "nif.corpusRoundtrip", {{"limit", limit}})["results"][0]["status"] == "error");
    }

    writeBytes(host.dataDir / "meshes" / "t" / "bad.nif", std::string("this is not a nif"));
    const auto broken = runTool(target, "nif.corpusRoundtrip", {{"limit", "all"}})["results"][0];
    CHECK(broken["status"] == "fail");
    CHECK(broken["left"]["value"] == "6 of 6");
    CHECK(hasRow(broken, "bad.nif"));
    CHECK(host.calls.empty());
}

TEST_CASE("DebugTools.NifCorpusCancelSkipsWithoutCallingTheHost", "[debug-tools]") {
    Host host;
    writeBytes(host.dataDir / "meshes" / "a.nif", fo4ide::test::triangleNif());
    host.gateAssets = true;
    host.gate.store(false);
    DebugToolsTarget target(host.env());
    REQUIRE(start(target, "nif.corpusRoundtrip", {{"limit", "all"}}).has_value());
    waitEntered(host);
    CHECK(parse(target.invoke("Cancel", json::array()))["cancelled"] == true);
    host.gate.store(true);
    const auto status = waitIdle(target);
    CHECK(status["cancelled"].get<bool>());
    CHECK(status["results"][0]["status"] == "skip");
    CHECK(status["results"][0]["detail"].get<std::string>().find("Cancelled after 0 of 1") != std::string::npos);
    CHECK(host.calls.empty());
}

TEST_CASE("DebugTools.DdsInspectChecksHeaderSizeAndFormat", "[debug-tools]") {
    Host host;
    writeBytes(host.dataDir / "textures" / "t" / "ok.dds", bc1Texture(32));
    writeBytes(host.dataDir / "textures" / "t" / "short.dds", bc1Texture(16));
    writeBytes(host.dataDir / "textures" / "t" / "long.dds", bc1Texture(40));
    writeBytes(host.dataDir / "textures" / "t" / "zero.dds", std::vector<std::uint8_t>(128, 0));
    writeBytes(host.dataDir / "textures" / "t" / "legacy.dds", legacyTexture(0x20202020U));
    DebugToolsTarget target(host.env());
    const auto run = [&](const char* name) {
        return runTool(target, "dds.inspect", {{"asset", std::string("textures/t/") + name}})["results"][0];
    };

    const auto ok = run("ok.dds");
    CHECK(ok["status"] == "pass");
    CHECK_FALSE(ok["evidence"].empty());
    CHECK(ok["left"]["value"].get<std::string>().find("8x8") != std::string::npos);
    const auto shortFile = run("short.dds");
    CHECK(shortFile["status"] == "fail");
    CHECK(hasRow(shortFile, "truncated: 16"));
    const auto longFile = run("long.dds");
    CHECK(longFile["status"] == "pass");
    CHECK(hasRow(longFile, "trailing: 8"));
    CHECK(run("zero.dds")["status"] == "fail");
    const auto legacy = run("legacy.dds");
    CHECK(legacy["status"] == "unsupported");
    CHECK(legacy["evidence"].empty());
    CHECK(run("missing.dds")["status"] == "skip");
    CHECK(host.calls.empty());
}

TEST_CASE("DebugTools.DdsInspectAcceptsCompleteNonPowerOfTwoMipChains", "[debug-tools]") {
    Host host;
    const auto format = fo4::texture::lookup_format(71);
    REQUIRE(format.has_value());
    const fo4::texture::DdsInfo info{5, 3, 3, false, 1, 1, 148, *format, 0};
    auto bytes = fo4::texture::build_dds_header(info);
    bytes.resize(bytes.size() + 32, 0);
    writeBytes(host.dataDir / "textures" / "npot.dds", bytes);
    DebugToolsTarget target(host.env());
    const auto result = runTool(target, "dds.inspect", {{"asset", "textures/npot.dds"}})["results"][0];
    CHECK(result["status"] == "pass");
}

TEST_CASE("DebugTools.PexInspectComparesTheRewrittenBytes", "[debug-tools]") {
    Host host;
    const auto script = syntheticScript();
    writeBytes(host.dataDir / "scripts" / "synthetic.pex", script);
    writeBytes(host.dataDir / "scripts" / "mismatch.pex", syntheticScript(2));
    writeBytes(host.dataDir / "scripts" / "garbage.pex", std::string("not a compiled script at all"));
    DebugToolsTarget target(host.env());
    const auto good = runTool(target, "pex.inspect", {{"asset", "scripts/synthetic.pex"}})["results"][0];
    CHECK(good["status"] == "pass");
    CHECK(hasRow(good, "object SyntheticObject"));
    const auto mismatch = runTool(target, "pex.inspect", {{"asset", "scripts/mismatch.pex"}})["results"][0];
    CHECK(mismatch["status"] == "fail");
    CHECK(hasRow(mismatch, "first differing byte at position 71"));
    CHECK(runTool(target, "pex.inspect", {{"asset", "scripts/garbage.pex"}})["results"][0]["status"] == "fail");
    CHECK(runTool(target, "pex.inspect", {{"asset", "scripts/none.pex"}})["results"][0]["status"] == "skip");
}

TEST_CASE("DebugTools.StringsInspectReadsStrictlyAndComparesMeaningNotBytes", "[debug-tools]") {
    Host host;
    fo4::localization::LocalizedStringTable table;
    table.kind = fo4::localization::StringTableKind::strings;
    fo4::localization::insert_string(table, 1, "alpha");
    fo4::localization::insert_string(table, 2, "beta");
    fo4::localization::insert_string(table, 3, "alpha");
    auto bytes = fo4::localization::write_string_table(table);
    writeBytes(host.dataDir / "strings" / "t_en.strings", bytes);
    bytes.resize(5);
    writeBytes(host.dataDir / "strings" / "cut_en.strings", bytes);
    writeBytes(host.dataDir / "strings" / "notes.txt", std::string("plain text"));
    DebugToolsTarget target(host.env());
    const auto run = [&](const char* name) {
        return runTool(target, "strings.inspect", {{"asset", std::string("strings/") + name}})["results"][0];
    };

    const auto ok = run("t_en.strings");
    CHECK(ok["status"] == "pass");
    CHECK(hasRow(ok, "3 entrie(s), 2 distinct text(s)"));
    CHECK(run("cut_en.strings")["status"] == "fail");
    CHECK(run("notes.txt")["status"] == "unsupported");
    CHECK(run("none_en.strings")["status"] == "skip");
}

TEST_CASE("DebugTools.AudioInspectDecodesInMemory", "[debug-tools]") {
    Host host;
    writeBytes(host.dataDir / "sound" / "fx" / "t" / "tone.wav", pcmWave());
    writeBytes(host.dataDir / "sound" / "fx" / "t" / "noise.wav", std::string(40, 'x'));
    DebugToolsTarget target(host.env());
    const auto tone = runTool(target, "audio.inspect", {{"asset", "sound/fx/t/tone.wav"}})["results"][0];
    CHECK(tone["status"] == "pass");
    CHECK(tone["left"]["value"].get<std::string>().find("22050 Hz") != std::string::npos);
    CHECK(tone["right"]["value"].get<std::string>().find("4 frame(s)") != std::string::npos);
    CHECK(runTool(target, "audio.inspect", {{"asset", "sound/fx/t/noise.wav"}})["results"][0]["status"] == "fail");
    CHECK(runTool(target, "audio.inspect", {{"asset", "sound/fx/t/none.wav"}})["results"][0]["status"] == "skip");
}

TEST_CASE("DebugTools.PluginStringTablesPassOnlyWhenCompleteAndSaved", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto run = [&] { return runTool(target, "strings.pluginTables", {{"plugin", "A.esp"}})["results"][0]; };

    CHECK(run()["status"] == "pass");
    REQUIRE(host.calls.size() == 1);
    CHECK(host.calls.front().first == "strings.ListStringTables");
    CHECK(host.calls.front().second == json::array({"A.esp", "en"}));
    host.replies["strings.ListStringTables"] = Host::tableList({".strings", ".dlstrings"}).dump();
    CHECK(run()["status"] == "unverified");
    host.replies["strings.ListStringTables"] = Host::tableList({}).dump();
    CHECK(run()["status"] == "unverified");
    host.replies["strings.ListStringTables"] = Host::tableList({".strings", ".dlstrings", ".ilstrings"}, true).dump();
    CHECK(run()["status"] == "unverified");
    host.replies["strings.ListStringTables"] = Host::tableList({".strings", ".dlstrings", ".ilstrings"}, false, 1).dump();
    CHECK(run()["status"] == "unverified");
    host.replies["strings.ListStringTables"] = json("{}");
    CHECK(run()["status"] == "error");
    host.errors["strings.ListStringTables"] = "PluginNotLoaded";
    CHECK(run()["status"] == "skip");
}

TEST_CASE("DebugTools.TextRoundtripWritesOnlyUnderScratchAndNeverTouchesThePlugin", "[debug-tools]") {
    Host host;
    writeMiniPlugin(host.pluginDir / "A.esp");
    const auto original = readAll(host.pluginDir / "A.esp");
    DebugToolsTarget target(host.env());
    CHECK(errorType(start(target, "plugin.textRoundtrip", {{"plugin", "A.esp"}})) == "BadParams");
    CHECK(errorType(start(target, "plugin.textRoundtrip", {{"plugin", "A.esp"}, {"outputDir", host.dataDir.string()}})) ==
          "UnsafeOutputDir");

    const auto result =
        runTool(target, "plugin.textRoundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}})["results"][0];
    CHECK(kStatuses.contains(result["status"].get<std::string>()));
    CHECK(result["status"] == "pass");
    CHECK(result["left"]["value"].get<std::string>().find("bytes") != std::string::npos);
    const auto scratch = host.outDir / "debugtools-T1";
    CHECK_FALSE(fs::exists(scratch));
    std::size_t inScratch = 0;
    for (const auto& entry : fs::recursive_directory_iterator(host.root.path())) {
        if (!entry.is_regular_file()) continue;
        const auto relative = fs::relative(entry.path(), scratch);
        const bool below = !relative.empty() && *relative.begin() != "..";
        if (below) {
            ++inScratch;
        } else {
            CHECK(entry.path() == host.pluginDir / "A.esp");
        }
    }
    if (result["status"] == "pass") {
        CHECK(inScratch == 0);
    } else {
        CHECK_FALSE(result["outputs"].empty());
    }
    CHECK(readAll(host.pluginDir / "A.esp") == original);
    CHECK(fs::is_empty(host.dataDir));

    const auto otherOut = host.root.path() / "out2";
    fs::create_directories(otherOut);
    const auto missing =
        runTool(target, "plugin.textRoundtrip", {{"plugin", "Missing.esp"}, {"outputDir", otherOut.string()}})["results"][0];
    CHECK(missing["status"] == "skip");
}

namespace {

namespace dt = fo4ide::app::debugtools;

std::string rawSubrecord(const std::string& signature, const std::string& data) {
    std::string out = signature;
    plugin_fixtures_test::appendLittleEndian(out, static_cast<std::uint32_t>(data.size()), 2);
    return out + data;
}

std::string oneRecordGroup(const std::string& signature, const std::string& body, std::uint32_t formId = 0x800) {
    std::string record = signature;
    plugin_fixtures_test::appendLittleEndian(record, static_cast<std::uint32_t>(body.size()), 4);
    plugin_fixtures_test::appendLittleEndian(record, 0, 4);
    plugin_fixtures_test::appendLittleEndian(record, formId, 4);
    plugin_fixtures_test::appendLittleEndian(record, 0, 4);
    plugin_fixtures_test::appendLittleEndian(record, 131, 2);
    plugin_fixtures_test::appendLittleEndian(record, 0, 2);
    record += body;
    std::string group = "GRUP";
    plugin_fixtures_test::appendLittleEndian(group, static_cast<std::uint32_t>(24 + record.size()), 4);
    group += signature;
    for (int i = 0; i < 3; ++i) plugin_fixtures_test::appendLittleEndian(group, 0, 4);
    return group + record;
}

std::string miscItemGroup() {
    std::string bounds;
    for (const int value : {-5, -5, -5, 5, 5, 5}) {
        plugin_fixtures_test::appendLittleEndian(bounds, static_cast<std::uint32_t>(static_cast<std::uint16_t>(value)), 2);
    }
    std::string data;
    plugin_fixtures_test::appendLittleEndian(data, 10, 4);
    plugin_fixtures_test::appendLittleEndian(data, 0x3FC00000u, 4);
    const std::string body =
        rawSubrecord("EDID", std::string("RowItem\0", 8)) + rawSubrecord("OBND", bounds) + rawSubrecord("DATA", data);
    return oneRecordGroup("MISC", body);
}

// minBits and maxBits are float bit patterns in world units (4096 to the cell), used for both axes; ofstCells is how many OFST entries the record carries.
std::string worldGroup(std::uint32_t minBits, std::uint32_t maxBits, std::size_t ofstCells) {
    std::string nam0;
    std::string nam9;
    for (int axis = 0; axis < 2; ++axis) {
        plugin_fixtures_test::appendLittleEndian(nam0, minBits, 4);
        plugin_fixtures_test::appendLittleEndian(nam9, maxBits, 4);
    }
    const std::string body = rawSubrecord("EDID", std::string("RowWorld\0", 9)) + rawSubrecord("NAM0", nam0) +
                             rawSubrecord("NAM9", nam9) + rawSubrecord("OFST", std::string(ofstCells * 4, '\0'));
    return oneRecordGroup("WRLD", body);
}

json rowRun(DebugToolsTarget& target, Host& host, const json& params, const char* folder) {
    const auto out = host.root.path() / folder;
    fs::create_directories(out);
    json full = params;
    full["outputDir"] = out.string();
    return runTool(target, "plugin.rowRoundtrip", full)["results"][0];
}

}  // namespace

TEST_CASE("DebugTools.BrokenCellTableParsesAndComparesExactly", "[debug-tools]") {
    const auto table = dt::parseBrokenCellTable(
        "# Generated from the Fallout 4 AE 1.11.240.0 Data folder\r\n00DC49:Fallout4.esm\tcdx_row_missing,oc_nif_missing\r\n\n"
        "23F94B:Fallout4.esm\ttome_missing\n");
    REQUIRE(table.has_value());
    CHECK(table->build == "1.11.240.0");
    REQUIRE(table->cells.size() == 2);
    CHECK(table->cells.at("00DC49:Fallout4.esm") == "cdx_row_missing,oc_nif_missing");
    CHECK(table->cells.at("23F94B:Fallout4.esm") == "tome_missing");

    const auto unlabeled = dt::parseBrokenCellTable("00DC49:Fallout4.esm\ttome_missing\n");
    REQUIRE(unlabeled.has_value());
    CHECK(unlabeled->build.empty());
    const auto noTab = dt::parseBrokenCellTable("# AE 1.11.240.0\n00DC49:Fallout4.esm tome_missing\n");
    REQUIRE_FALSE(noTab.has_value());
    CHECK(noTab.error().message == "line 2 has no tab");
    const auto duplicate = dt::parseBrokenCellTable("00DC49:Fallout4.esm\ttome_missing\n00DC49:Fallout4.esm\toc_nif_missing\n");
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().message == "line 2 repeats a cell");

    const dt::BrokenCells pinned{{"A:x.esm", "tome_missing"}, {"B:x.esm", "cdx_row_missing"}, {"C:x.esm", "tome_missing"}};
    CHECK(dt::compareBrokenCells(pinned, pinned).equal());
    CHECK(dt::compareBrokenCells({}, {}).equal());
    const dt::BrokenCells actual{{"A:x.esm", "tome_missing"}, {"B:x.esm", "oc_nif_missing"}, {"D:x.esm", "tome_missing"}};
    const auto diff = dt::compareBrokenCells(pinned, actual);
    CHECK_FALSE(diff.equal());
    CHECK(diff.missing == std::vector<std::string>{"C:x.esm tome_missing"});
    CHECK(diff.extra == std::vector<std::string>{"D:x.esm tome_missing"});
    CHECK(diff.different == std::vector<std::string>{"B:x.esm reported oc_nif_missing, pinned cdx_row_missing"});
}

TEST_CASE("DebugTools.ShippedPinnedBrokenCellListParses", "[debug-tools]") {
    const auto text = readAll(fs::path(FO4IDE_RADICLE_TESTDATA_DIR) / "previs" / "vanilla_1_11_240_broken.tsv");
    REQUIRE_FALSE(text.empty());
    const auto table = dt::parseBrokenCellTable(text);
    REQUIRE(table.has_value());
    CHECK(table->build == "1.11.240.0");
    CHECK(table->cells.size() == 370);
    CHECK(table->cells.at("00DC49:Fallout4.esm") == "cdx_row_missing,oc_nif_missing");
    for (const auto& [cell, codes] : table->cells) {
        CHECK(cell.find(':') != std::string::npos);
        CHECK_FALSE(codes.empty());
    }
    CHECK(dt::compareBrokenCells(table->cells, table->cells).equal());
}

TEST_CASE("DebugTools.BaselineBuildComesOnlyFromTheExeBesideTheAnalysedDataFolder", "[debug-tools]") {
    Host host;
    const unsigned char exe[] = {0x01, 0x02, 0x03, 0x04, 0xBD, 0x04, 0xEF, 0xFE, 0x00, 0x00, 0x01, 0x00,
                                 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA3, 0x00, 0x05, 0x06, 0x07, 0x08};
    const auto write = [&](const fs::path& path) {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(exe), static_cast<std::streamsize>(sizeof(exe)));
    };
    const auto env = host.env();
    write(host.root.path() / "Fallout 4" / "Fallout4.exe");
    CHECK(dt::gameBuildFor(env) == "1.10.163.0");
    CHECK(dt::gameBuildBesideData(env).empty());
    write(host.root.path() / "Fallout4.exe");
    CHECK(dt::gameBuildBesideData(env) == "1.10.163.0");
}

TEST_CASE("DebugTools.VanillaBaselineSkipsOrErrorsAndNeverPassesWithoutRunningTheGate", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto run = [&](const json& params) { return runTool(target, "previs.vanillaBaseline", params)["results"]; };

    const auto noList = run(json::object());
    REQUIRE(noList.size() == 2);
    CHECK(noList[0]["status"] == "skip");
    CHECK(noList[0]["detail"].get<std::string>().find("pinned broken-cell list was not found") != std::string::npos);
    CHECK(noList[0]["evidence"].empty());
    CHECK(noList[1]["status"] == "unverified");

    const auto good = host.root.path() / "pinned.tsv";
    std::ofstream(good, std::ios::binary) << "# AE 1.11.240.0\n00DC49:Fallout4.esm\ttome_missing\n";
    const auto noMasters = run({{"baseline", good.string()}});
    CHECK(noMasters[0]["status"] == "skip");
    CHECK(noMasters[0]["detail"].get<std::string>().find("missing: Fallout4.esm") != std::string::npos);

    const auto bad = host.root.path() / "bad.tsv";
    std::ofstream(bad, std::ios::binary) << "00DC49:Fallout4.esm tome_missing\n";
    const auto unusable = run({{"baseline", bad.string()}});
    CHECK(unusable[0]["status"] == "error");
    CHECK(unusable[0]["detail"].get<std::string>().find("is unusable") != std::string::npos);
    CHECK(unusable[0]["detail"].get<std::string>().find("line 1 has no tab") != std::string::npos);
    CHECK(unusable[0]["detail"].get<std::string>().find("00DC49") == std::string::npos);

    CHECK(run({{"baseline", (host.root.path() / "none.tsv").string()}})[0]["status"] == "skip");

    for (const char* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop03.esm",
                             "DLCNukaWorld.esm"}) {
        std::ofstream(host.dataDir / name, std::ios::binary) << "not a plugin";
    }
    const auto garbage = run({{"baseline", good.string()}})[0];
    CHECK((garbage["status"] == "error" || garbage["status"] == "unverified"));

    std::lock_guard<std::mutex> guard(host.mutex);
    CHECK(host.calls.empty());
}

// Detects: the baseline's status mapping, not only its skip and error paths. Six record-free masters report no broken cell, so a pinned cell is a diff: fail on the matching build, unverified on another build, and an empty pinned list on the matching build passes.
TEST_CASE("DebugTools.VanillaBaselineMapsTheDiffAndBuildToFailPassOrUnverified", "[debug-tools]") {
    Host host;
    const unsigned char exe[] = {0x01, 0x02, 0x03, 0x04, 0xBD, 0x04, 0xEF, 0xFE, 0x00, 0x00, 0x01, 0x00,
                                 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA3, 0x00, 0x05, 0x06, 0x07, 0x08};
    std::ofstream(host.root.path() / "Fallout4.exe", std::ios::binary)
        .write(reinterpret_cast<const char*>(exe), static_cast<std::streamsize>(sizeof(exe)));
    for (const char* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop03.esm",
                             "DLCNukaWorld.esm"}) {
        std::ofstream(host.dataDir / name, std::ios::binary) << plugin_fixtures_test::minimalPlugin(1, "");
    }
    DebugToolsTarget target(host.env());
    const auto run = [&](const char* file, const std::string& text) {
        const auto pinned = host.root.path() / file;
        std::ofstream(pinned, std::ios::binary) << text;
        return runTool(target, "previs.vanillaBaseline", {{"baseline", pinned.string()}})["results"][0];
    };

    const auto diffOnBuild = run("diff.tsv", "# AE 1.10.163.0\n00DC49:Fallout4.esm\ttome_missing\n");
    CHECK(diffOnBuild["status"] == "fail");
    CHECK(diffOnBuild["detail"].get<std::string>().find("1 pinned cell(s) not reported") != std::string::npos);

    const auto diffOffBuild = run("other.tsv", "# AE 1.11.240.0\n00DC49:Fallout4.esm\ttome_missing\n");
    CHECK(diffOffBuild["status"] == "unverified");
    CHECK(diffOffBuild["detail"].get<std::string>().find("not comparable") != std::string::npos);

    const auto equalOffBuild = run("other-empty.tsv", "# AE 1.11.240.0\n");
    CHECK(equalOffBuild["status"] == "unverified");

    const auto equalOnBuild = run("empty.tsv", "# AE 1.10.163.0\n");
    CHECK(equalOnBuild["status"] == "pass");
}

// Detects: a reply row that is not an object, or a field of the wrong type, must not throw out of the tool and replace its own diagnostic with a generic error.
TEST_CASE("DebugTools.ReplyRowsThatAreNotObjectsKeepTheToolsOwnDiagnostic", "[debug-tools]") {
    {
        Host host;
        host.replies["lod.Plan"] =
            json{{"plan", json::array({1, nullptr, {{"rebuild", "yes"}}, {{"rebuild", true}}})}, {"ledger", ""}};
        DebugToolsTarget target(host.env());
        const auto result = runTool(target, "lod.plan")["results"][0];
        CHECK(result["status"] == "pass");
        CHECK(result["left"]["value"] == "4");
        CHECK(result["right"]["value"] == "1");
    }
    {
        Host host;
        host.replies["previs.Scan"] =
            json{{"summary", {{"broken", 2}, {"outdated", 0}}},
                 {"broken", json::array({7, {{"cell", "000801:A.esp"}, {"plugin", "A.esp"}, {"issues", 5}}})},
                 {"outdated", "none"},
                 {"truncated", "no"}};
        DebugToolsTarget target(host.env());
        const auto result = runTool(target, "previs.scan")["results"][0];
        CHECK(result["status"] == "fail");
        CHECK(result["detail"].get<std::string>().find("2 broken") != std::string::npos);
    }
}

// Detects: a client that first sees a run after it finished must be able to record when it ran, so the status carries the finish time and it is empty before any run.
TEST_CASE("DebugTools.StatusCarriesTheTimeTheRunFinished", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    CHECK(statusOf(target)["finishedAt"] == "");
    const auto status = runTool(target, "lod.worlds");
    const auto finishedAt = status["finishedAt"].get<std::string>();
    REQUIRE(finishedAt.size() == 24u);
    CHECK(finishedAt[4] == '-');
    CHECK(finishedAt[10] == 'T');
    CHECK(finishedAt[19] == '.');
    CHECK(finishedAt.back() == 'Z');
}

TEST_CASE("DebugTools.ReferenceToolFlagsMatchWhatTheyDo", "[debug-tools]") {
    Host host;
    DebugToolsTarget target(host.env());
    const auto list = parse(target.invoke("List", json::array()));
    std::map<std::string, json> tools;
    for (const auto& group : list["groups"]) {
        for (const auto& tool : group["tools"]) tools[tool["id"].get<std::string>()] = tool;
    }
    REQUIRE(tools.contains("previs.vanillaBaseline"));
    REQUIRE(tools.contains("plugin.rowRoundtrip"));
    const auto& baseline = tools.at("previs.vanillaBaseline");
    CHECK(baseline["readOnly"] == true);
    CHECK(baseline["writes"] == false);
    CHECK(baseline["expensive"] == true);
    CHECK(baseline["cancellable"] == false);
    const auto& rows = tools.at("plugin.rowRoundtrip");
    CHECK(rows["writes"] == true);
    CHECK(rows["needsOutputDir"] == true);
    CHECK(rows["expensive"] == true);
    CHECK(rows["cancellable"] == false);
}

TEST_CASE("DebugTools.RowRoundtripReportsUnsupportedWithThePreconditionAndKeepsScratchRules", "[debug-tools]") {
    Host host;
    writeMiniPlugin(host.pluginDir / "A.esp");
    const auto original = readAll(host.pluginDir / "A.esp");
    DebugToolsTarget target(host.env());
    CHECK(errorType(start(target, "plugin.rowRoundtrip", {{"plugin", "A.esp"}, {"family", "MISC"}})) == "BadParams");
    CHECK(errorType(start(target, "plugin.rowRoundtrip", {{"plugin", "A.esp"}, {"outputDir", host.outDir.string()}})) ==
          "BadParams");
    CHECK(errorType(start(target, "plugin.rowRoundtrip",
                          {{"plugin", "A.esp"}, {"family", "MISC"}, {"outputDir", host.dataDir.string()}})) ==
          "UnsafeOutputDir");
    CHECK(errorType(start(target, "plugin.rowRoundtrip",
                          {{"plugin", "A.esp"}, {"family", "MISC"}, {"outputDir", host.pluginDir.string()}})) ==
          "UnsafeOutputDir");

    const auto noRecord = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "MISC"}}, "out1");
    CHECK(noRecord["status"] == "unsupported");
    CHECK(noRecord["detail"].get<std::string>().find("no MISC record") != std::string::npos);
    CHECK(noRecord["evidence"].empty());

    const auto noRows = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "ZZZZ"}}, "out2");
    CHECK(noRows["status"] == "unsupported");
    CHECK(noRows["detail"].get<std::string>().find("no scalar rows for ZZZZ") != std::string::npos);

    CHECK(rowRun(target, host, {{"plugin", "A.esp"}, {"family", "TOOLONG"}}, "out3")["status"] == "error");
    CHECK(rowRun(target, host, {{"plugin", "Missing.esp"}, {"family", "MISC"}}, "out4")["status"] == "skip");
    CHECK(readAll(host.pluginDir / "A.esp") == original);
    CHECK(fs::is_empty(host.dataDir));
    std::lock_guard<std::mutex> guard(host.mutex);
    CHECK(host.calls.empty());
}

TEST_CASE("DebugTools.RowRoundtripEditsWritesRereadsAndLeavesTheSourceAlone", "[debug-tools]") {
    Host host;
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary) << plugin_fixtures_test::minimalPlugin(0, miscItemGroup());
    const auto original = readAll(host.pluginDir / "A.esp");
    DebugToolsTarget target(host.env());
    const auto result = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "misc"}}, "out1");
    INFO(result.dump());
    CHECK((result["status"] == "pass" || result["status"] == "unverified"));
    const auto left = result["left"]["value"].get<std::string>();
    CHECK(left == std::string("2 row(s) on 1 record(s)"));
    REQUIRE(result["outputs"].size() == 1);
    const auto written = fs::path(result["outputs"][0].get<std::string>());
    REQUIRE(fs::is_regular_file(written));
    CHECK(fs::equivalent(written, host.root.path() / "out1" / "debugtools-T1" / "A.esp"));
    CHECK(readAll(written) != original);
    CHECK(readAll(host.pluginDir / "A.esp") == original);
    std::size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(host.root.path())) files += entry.is_regular_file() ? 1 : 0;
    CHECK(files == 2);
    CHECK(fs::is_empty(host.dataDir));
}

TEST_CASE("DebugTools.RowRoundtripShutdownDoesNotLeaveAnEmptyScratchFolder", "[debug-tools]") {
    Host host;
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary) << plugin_fixtures_test::minimalPlugin(0, miscItemGroup());
    const auto env = host.env();
    std::vector<dt::ToolSpec> tools;
    dt::appendReferenceTools(tools);
    const auto tool = std::find_if(tools.begin(), tools.end(), [](const auto& item) { return item.id == "plugin.rowRoundtrip"; });
    REQUIRE(tool != tools.end());
    std::atomic<bool> cancel{false};
    const auto scratch = host.outDir / "shutdown";
    dt::Ctx ctx{env, {{"plugin", "A.esp"}, {"family", "MISC"}}, scratch, cancel,
                [&](std::size_t step, std::size_t, const std::string&) { if (step == 1) cancel = true; }};
    std::vector<dt::Diag> results;
    tool->run(ctx, results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].status == "skip");
    CHECK_FALSE(fs::exists(scratch));
}

TEST_CASE("DebugTools.RowRoundtripReportsCappedCarriersAsUnresolved", "[debug-tools]") {
    Host host;
    std::string groups;
    for (std::uint32_t i = 0; i < 9; ++i) {
        const auto data = i < 8 ? std::string(1, '\0') : std::string(8, '\0');
        groups += oneRecordGroup("MISC", rawSubrecord("DATA", data), 0x800 + i);
    }
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary) << plugin_fixtures_test::minimalPlugin(0, groups);
    DebugToolsTarget target(host.env());
    const auto result = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "MISC"}}, "out1");
    CHECK(result["status"] == "unsupported");
    CHECK(result["detail"].get<std::string>().find("0 with no usable candidate") != std::string::npos);
    CHECK(result["detail"].get<std::string>().find("2 unresolved") != std::string::npos);
}

TEST_CASE("DebugTools.RoundtripReportsScratchCreationErrors", "[debug-tools]") {
    Host host;
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary) << plugin_fixtures_test::minimalPlugin(0, miscItemGroup());
    const auto env = host.env();
    for (const auto* id : {"plugin.roundtrip", "plugin.rowRoundtrip"}) {
        const auto& tools = dt::toolTable();
        const auto tool = std::find_if(tools.begin(), tools.end(), [&](const auto& item) { return item.id == id; });
        REQUIRE(tool != tools.end());
        std::atomic<bool> cancel{false};
        dt::Ctx ctx{env, {{"plugin", "A.esp"}, {"family", "MISC"}}, host.outDir / "missing" / "scratch", cancel,
                    [](std::size_t, std::size_t, const std::string&) {}};
        std::vector<dt::Diag> results;
        tool->run(ctx, results);
        REQUIRE(results.size() == 1);
        CHECK(results[0].status == "error");
        CHECK(results[0].detail.find("could not be created:") != std::string::npos);
    }
}

// Detects: a setter error on a WRLD bounds row other than Radicle refusing the new grid shape (here an OFST that does not match the bounds) must fail the run, not be counted as a row with no usable candidate.
TEST_CASE("DebugTools.RowRoundtripFailsWhenAWorldBoundsSetterThrows", "[debug-tools]") {
    Host host;
    // NAM0 0,0 and NAM9 8192,8192 span a 3 by 3 grid, but OFST holds only 2 cells.
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary)
        << plugin_fixtures_test::minimalPlugin(0, worldGroup(0x00000000u, 0x46000000u, 2));
    DebugToolsTarget target(host.env());
    const auto result = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "WRLD"}}, "out1");
    INFO(result.dump());
    CHECK(result["status"] == "fail");
    CHECK(result["detail"].get<std::string>().find("first setter failure") != std::string::npos);
    CHECK(result["diffs"].dump().find("does not match its NAM0/NAM9 bounds") != std::string::npos);
    CHECK(result["outputs"].empty());
}

// Detects: the narrowed bounds exemption must still skip a candidate Radicle refuses because it leaves no grid. Rows run in path order and each accepted edit stays, so the max rows go first and must all be refused: every max candidate rounds to cell 0, below the min cell 2, so only the min rows and OFST are edited.
TEST_CASE("DebugTools.RowRoundtripSkipsWorldBoundsCandidatesThatLeaveNoGrid", "[debug-tools]") {
    Host host;
    // NAM0 8192 (cell 2) and NAM9 16384 (cell 4) span a 3 by 3 grid with a matching OFST.
    std::ofstream(host.pluginDir / "A.esp", std::ios::binary)
        << plugin_fixtures_test::minimalPlugin(0, worldGroup(0x46000000u, 0x46800000u, 9));
    DebugToolsTarget target(host.env());
    const auto result = rowRun(target, host, {{"plugin", "A.esp"}, {"family", "WRLD"}}, "out1");
    INFO(result.dump());
    CHECK((result["status"] == "pass" || result["status"] == "unverified"));
    CHECK(result["left"]["value"] == "3 row(s) on 1 record(s)");
    CHECK(result["detail"].get<std::string>().find("2 with no usable candidate") != std::string::npos);
}

TEST_CASE("DebugToolsReal.RowRoundtripOnAShippedDlcMasterLeavesTheDataFolderUntouched", "[debug-tools-real][slow]") {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr || *dir == '\0') SKIP("Set FO4_DATA_DIR to a Fallout 4 Data folder");
    const fs::path data{dir};
    if (!fs::is_regular_file(data / "DLCworkshop01.esm")) SKIP("DLCworkshop01.esm is not in the Data folder");
    const auto before = dataListing(data);

    DebugToolsEnv env;
    env.call = [](const std::string&, const std::string&, const json&) -> fo4ide::Result<json> {
        return std::unexpected(fo4ide::Error{"NoHost", "not used by this test"});
    };
    env.settings = [&data] { return json{{"DataFolder", data.string()}}; };
    env.timestamp = [] { return std::string("REAL"); };
    env.fo4ideVersion = "0.1.0";
    DebugToolsTarget target(std::move(env));

    fo4ide_test::TempRoot scratchRoot{"fo4ide_debug_tools_real_rows"};
    const auto run = runTool(target, "plugin.rowRoundtrip",
                             {{"plugin", (data / "DLCworkshop01.esm").string()},
                              {"family", "MISC"},
                              {"outputDir", scratchRoot.path().string()}},
                             std::chrono::seconds(120));
    const auto& result = run["results"][0];
    INFO(result.dump());
    CHECK(result["status"] != "error");
    CHECK(result["status"] != "fail");
    CHECK(dataListing(data) == before);
}

TEST_CASE("DebugToolsReal.AssetToolsReadShippedFilesAndLeaveTheDataFolderUntouched", "[debug-tools-real][slow]") {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr || *dir == '\0') SKIP("Set FO4_DATA_DIR to a Fallout 4 Data folder");
    const fs::path data{dir};
    REQUIRE(fs::is_regular_file(data / "Fallout4.esm"));
    const auto before = dataListing(data);

    std::vector<fs::path> archives;
    for (const auto& entry : fs::directory_iterator(data)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("Fallout4 - ") && entry.path().extension() == ".ba2") {
            archives.push_back(entry.path());
        }
    }
    std::sort(archives.begin(), archives.end());
    auto resolver = std::make_shared<const fo4::archive::AssetResolver>(fo4::archive::AssetResolver::open(data, archives));
    const auto first = [&](const char* prefix, const char* extension) {
        const auto listed = resolver->list(prefix, extension, false);
        return listed.empty() ? std::string{} : listed.front().path;
    };

    DebugToolsEnv env;
    env.call = [](const std::string&, const std::string&, const json&) -> fo4ide::Result<json> {
        return std::unexpected(fo4ide::Error{"NoHost", "not used by this test"});
    };
    env.settings = [&data] { return json{{"DataFolder", data.string()}}; };
    env.assets = [resolver] { return resolver; };
    env.timestamp = [] { return std::string("REAL"); };
    env.fo4ideVersion = "0.1.0";
    DebugToolsTarget target(std::move(env));

    const std::vector<std::pair<std::string, std::string>> assets{
        {"dds.inspect", first("textures/", ".dds")},
        {"pex.inspect", first("scripts/", ".pex")},
        {"strings.inspect", first("strings/", ".strings")},
        {"audio.inspect", first("sound/", ".xwm")},
    };
    for (const auto& [tool, asset] : assets) {
        if (asset.empty()) continue;
        INFO(tool << " " << asset);
        const auto status = runTool(target, tool, {{"asset", asset}}, std::chrono::seconds(30))["results"][0]["status"];
        CHECK(status != "error");
        CHECK(status != "fail");
        CHECK(status != "skip");
    }
    const auto corpus = runTool(target, "nif.corpusRoundtrip", {{"limit", "25"}}, std::chrono::seconds(90));
    CHECK(corpus["results"][0]["status"] != "error");
    CHECK(corpus["results"][0]["status"] != "fail");
    CHECK(corpus["results"][0]["status"] != "skip");

    fo4ide_test::TempRoot scratchRoot{"fo4ide_debug_tools_real_text"};
    if (fs::is_regular_file(data / "DLCworkshop01.esm")) {
        const auto text = runTool(target, "plugin.textRoundtrip",
                                  {{"plugin", (data / "DLCworkshop01.esm").string()}, {"outputDir", scratchRoot.path().string()}},
                                  std::chrono::seconds(60));
        CHECK(text["results"][0]["status"] != "error");
        CHECK(text["results"][0]["status"] != "fail");
        CHECK(text["results"][0]["status"] != "skip");
    }
    CHECK(dataListing(data) == before);
}

TEST_CASE("DebugToolsReal.RoundtripsAndContextLeaveTheDataFolderUntouched", "[debug-tools-real][slow]") {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr || *dir == '\0') SKIP("Set FO4_DATA_DIR to a Fallout 4 Data folder");
    const fs::path data{dir};
    REQUIRE(fs::is_regular_file(data / "Fallout4.esm"));

    const auto listing = [&] { return dataListing(data); };
    const auto before = listing();

    fo4ide_test::TempRoot scratchRoot{"fo4ide_debug_tools_real"};
    std::vector<fs::path> archives;
    for (const auto* name : {"Fallout4 - Meshes.ba2", "Fallout4 - MeshesExtra.ba2"}) {
        if (fs::is_regular_file(data / name)) archives.push_back(data / name);
    }
    auto resolver = std::make_shared<const fo4::archive::AssetResolver>(fo4::archive::AssetResolver::open(data, archives));
    std::string nif;
    if (const auto listed = resolver->list("meshes/", ".nif", false); !listed.empty()) nif = listed.front().path;

    DebugToolsEnv env;
    env.call = [](const std::string&, const std::string&, const json&) -> fo4ide::Result<json> {
        return std::unexpected(fo4ide::Error{"NoHost", "not used by this test"});
    };
    env.settings = [&data] { return json{{"DataFolder", data.string()}}; };
    env.assets = [resolver] { return resolver; };
    env.timestamp = [] { return std::string("REAL"); };
    env.fo4ideVersion = "0.1.0";
    DebugToolsTarget target(std::move(env));

    const auto context = runTool(target, "env.context");
    CHECK(context["results"][0]["status"] == "pass");
    const auto roundtrip = runTool(
        target, "plugin.roundtrip", {{"plugin", (data / "Fallout4.esm").string()}, {"outputDir", scratchRoot.path().string()}},
        std::chrono::seconds(250));
    REQUIRE(roundtrip["results"].size() == 1);
    CHECK(roundtrip["results"][0]["status"] == "pass");
    if (!nif.empty()) {
        const auto nifRun = runTool(target, "nif.roundtrip", {{"asset", nif}}, std::chrono::seconds(250));
        REQUIRE(nifRun["results"].size() == 1);
        CHECK(nifRun["results"][0]["status"] == "pass");
    }
    CHECK(listing() == before);
}
