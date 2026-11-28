#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;

std::vector<std::byte> masterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    writeNamedRecord(out, "KYWD", 0x00000800, "MasterKeyword");
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> patchUsingSecondMaster() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x802, {"MasterA.esm", "MasterB.esp"});
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x02000801, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PatchRecord\0", 12});
    fo4::test::ByteWriter name;
    name.u32(0x01000800);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> masterlessLinkPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x901u);
    const auto misc = fo4::test::begin_group(out, "MISC", 0);
    writeNamedRecord(out, "MISC", 0x700u, "Moved");
    fo4::test::end_group(out, misc);
    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x900u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
    fo4::test::ByteWriter link;
    link.u32(0x700u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

struct MasterPair {
    std::filesystem::path root;
    std::filesystem::path masterA;
    std::filesystem::path masterB;
    std::filesystem::path patch;
};

MasterPair writeMasterPair(const std::filesystem::path& root) {
    MasterPair pair{root, root / "MasterA.esm", root / "MasterB.esp", root / "Patch.esp"};
    writeBytes(pair.masterA, masterPlugin());
    writeBytes(pair.masterB, masterPlugin());
    writeBytes(pair.patch, patchUsingSecondMaster());
    return pair;
}

void loadMasterPair(fo4ide::plugin::RadiclePluginBackend& backend, const MasterPair& pair) {
    loadPlugins(backend, {{"MasterA.esm", pair.masterA}, {"MasterB.esp", pair.masterB}, {"Patch.esp", pair.patch}});
}

}

TEST_CASE("Oracle masters list marks an unreferenced master unused and reports indexes and the light flag") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pair = writeMasterPair(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadMasterPair(backend, pair);

    const auto before = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    REQUIRE(before.at("masters").size() == 2);
    CHECK(before.at("light") == false);
    CHECK(before.at("masters")[0].at("name") == "MasterA.esm");
    CHECK(before.at("masters")[0].at("index") == 0);
    CHECK(before.at("masters")[0].at("used") == false);
    CHECK(before.at("masters")[1].at("name") == "MasterB.esp");
    CHECK(before.at("masters")[1].at("index") == 1);
    CHECK(before.at("masters")[1].at("used") == true);

    REQUIRE(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    const auto after = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    CHECK(after.at("light") == true);
    CHECK(after.at("masters")[0].at("used") == false);
}

TEST_CASE("Oracle masters reorder refuses an undeclared master and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pair = writeMasterPair(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadMasterPair(backend, pair);
    const auto before = readFile(pair.patch);

    const auto result = backend.invoke(
        "masters.Reorder", nlohmann::json::array({"Patch.esp", "[\"MasterA.esm\",\"MasterC.esm\"]"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidMasterOrder");
    CHECK(result.error().message.find("MasterC.esm") != std::string::npos);

    CHECK(readFile(pair.patch) == before);
    const auto listed = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    CHECK(listed.at("masters")[0].at("name") == "MasterA.esm");
    CHECK(listed.at("masters")[1].at("name") == "MasterB.esp");
    savePatch(backend);
    const auto disk = fo4::read_plugin_file(pair.patch);
    REQUIRE(disk.header.masters.size() == 2);
    CHECK(disk.header.masters[0].key.filename() == "MasterA.esm");
    CHECK(disk.header.masters[1].key.filename() == "MasterB.esp");
    CHECK(readFile(pair.patch) == before);
}

TEST_CASE("Oracle masters light flag set twice stays set and clears once") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pair = writeMasterPair(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadMasterPair(backend, pair);

    REQUIRE(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    REQUIRE(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    CHECK(parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"}))).at("light") == true);
    savePatch(backend);
    CHECK(fo4::read_plugin_file(pair.patch).header.tes4_flags.is_small_file());

    REQUIRE(backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", false})).has_value());
    CHECK(parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"}))).at("light") == false);
    savePatch(backend);
    CHECK_FALSE(fo4::read_plugin_file(pair.patch).header.tes4_flags.is_small_file());
}

TEST_CASE("Oracle renumber repoints an in-plugin link and refuses an id already in use") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, masterlessLinkPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto taken = backend.invokeBackend(
        "RenumberRecord", nlohmann::json::array({"Patch.esp", "000700:Patch.esp", "000900:Patch.esp"}));
    REQUIRE_FALSE(taken.has_value());
    CHECK(taken.error().type == "RecordFormIdExists");

    const auto renumbered = parseStringResult(backend.invokeBackend(
        "RenumberRecord", nlohmann::json::array({"Patch.esp", "000700:Patch.esp", "000950:Patch.esp"})));
    CHECK(renumbered.at("changed") == true);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK(recordAt(disk, 0x700u) == nullptr);
    REQUIRE(recordAt(disk, 0x950u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x950u)) == "Moved");
    REQUIRE(recordAt(disk, 0x900u) != nullptr);
    CHECK(formLinkOf(*recordAt(disk, 0x900u), "AHCM") == 0x950u);
}

TEST_CASE("Oracle first record created in a new esl plugin lands inside the light range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto overwrite = root / "overwrite";
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(overwrite).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"OracleLight.esl"})).has_value());

    const auto created = parseStringResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"OracleLight.esl", "EYES", nlohmann::json{
            {"editorId", "OracleEyes"},
            {"fullText", "Oracle Eyes"},
            {"icon", "Textures\\Interface\\OracleEyes.dds"},
            {"dataFlags", 0},
            {"recordFlags", 0}}})));
    const auto key = fo4::FormKey::parse(created.at("formKey").get<std::string>());
    CHECK(key.local().value() >= 0x800u);
    CHECK(key.local().value() <= 0xFFFu);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"OracleLight.esl"})).has_value());
    const auto disk = fo4::read_plugin_file(overwrite / "OracleLight.esl");
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(fo4::find_record(disk, key) != nullptr);
}
