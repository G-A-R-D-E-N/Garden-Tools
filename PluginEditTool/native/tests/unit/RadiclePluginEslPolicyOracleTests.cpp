#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;

bool patchPending(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    return active.at(0).at("Pending").get<bool>();
}

std::vector<std::uint32_t> range(std::uint32_t first, std::uint32_t last) {
    std::vector<std::uint32_t> ids;
    for (auto id = first; id <= last; ++id) ids.push_back(id);
    return ids;
}

}

TEST_CASE("Oracle SetLight refuses a masterless plugin whose owned ids sit outside the light range") {
    const auto outOfRange = GENERATE(0x005u, 0x5000u);
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x5001u, false, {outOfRange, 0x900u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto result = backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "SetLightFailed");
    CHECK(parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"}))).at("light") == false);
    CHECK_FALSE(patchPending(backend));
}

TEST_CASE("Oracle SetLight keeps an in-range HEDR next id and lowers a stale one to the floor of an empty plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto keepPath = root / "Keep.esp";
    const auto emptyPath = root / "Patch.esp";
    writeBytes(keepPath, miscPlugin(1.0f, 0x900u, false, {0x800u, 0x801u}));
    writeBytes(emptyPath, miscPlugin(1.0f, 0x5000u, false, {}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Keep.esp", keepPath}, {"Patch.esp", emptyPath}});

    const auto kept = textResult(backend.invoke("masters.SetLight", nlohmann::json::array({"Keep.esp", true})));
    CHECK(kept.find("lowered") == std::string::npos);
    REQUIRE(backend.invoke("masters.SavePlugin", nlohmann::json::array({"Keep.esp"})).has_value());
    CHECK(fo4::read_plugin_file(keepPath).header.next_form_id == 0x900u);

    const auto lowered = textResult(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})));
    CHECK(lowered.find("lowered HEDR nextFormID to 0x800") != std::string::npos);
    savePatch(backend);
    const auto disk = fo4::read_plugin_file(emptyPath);
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.next_form_id == 0x800u);
}

TEST_CASE("Oracle SetLight accepts low ids in a plugin with a master and no cross plugin links") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x900u, true, {0x005u, 0x006u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    REQUIRE(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    savePatch(backend);
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(recordAt(disk, 0x005u) != nullptr);
    CHECK(recordAt(disk, 0x006u) != nullptr);
}

TEST_CASE("Oracle compaction of a masterless plugin moves every low and high id into the light range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x5001u, false, {0x700u, 0x5000u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto report = textResult(backend.invokeBackend("CheckEslEligibility", nlohmann::json::array({"Patch.esp"})));
    CHECK(report.find("remapping 2 of 2") != std::string::npos);
    const auto result = textResult(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})));
    CHECK(result.find("remapped 2 record") != std::string::npos);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK(recordAt(disk, 0x700u) == nullptr);
    CHECK(recordAt(disk, 0x5000u) == nullptr);
    REQUIRE(recordAt(disk, 0x800u) != nullptr);
    REQUIRE(recordAt(disk, 0x801u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x800u)) == "Record1792");
    CHECK(editorIdOf(*recordAt(disk, 0x801u)) == "Record20480");
}

TEST_CASE("Oracle compaction fills the lowest free slot and leaves ids already in range alone") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x5001u, false, {0x900u, 0x5000u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    REQUIRE(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})).has_value());
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK(recordAt(disk, 0x5000u) == nullptr);
    REQUIRE(recordAt(disk, 0x800u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x800u)) == "Record20480");
    REQUIRE(recordAt(disk, 0x900u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x900u)) == "Record2304");
}

TEST_CASE("Oracle compaction stages nothing for a compatible plugin and one change when only HEDR next is stale") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto compatible = root / "Patch.esp";
    writeBytes(compatible, miscPlugin(1.0f, 0x901u, false, {0x900u}));
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        loadPatch(backend, compatible);
        const auto result = textResult(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})));
        CHECK(result.find("already ESL-compatible") != std::string::npos);
        CHECK_FALSE(patchPending(backend));
    }
    const auto stale = root / "Stale" / "Patch.esp";
    writeBytes(stale, miscPlugin(1.0f, 0x5000u, false, {0x900u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, stale);
    const auto result = textResult(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})));
    CHECK(result.find("lowered HEDR nextFormID to 0x901") != std::string::npos);
    CHECK(patchPending(backend));
}

TEST_CASE("Oracle compaction refuses a masterless plugin with more native records than light slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    auto ids = range(0x800u, 0xFFFu);
    ids.push_back(0x5000u);
    writeBytes(path, miscPlugin(1.0f, 0x5001u, false, ids));
    const auto before = readFile(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto report = textResult(backend.invokeBackend("CheckEslEligibility", nlohmann::json::array({"Patch.esp"})));
    CHECK(report.find("CANNOT fit") != std::string::npos);
    CHECK(report.find("2049 native record") != std::string::npos);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "EslCapacityExceeded");
    CHECK_FALSE(patchPending(backend));
    CHECK(readFile(path) == before);
}
