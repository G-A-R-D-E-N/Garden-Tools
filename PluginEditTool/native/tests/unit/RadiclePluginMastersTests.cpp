#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_masters_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> makeMaster(std::string_view editorId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    const auto record = fo4::test::begin_record(out, "KYWD", 0, 0x00000800);
    std::vector<std::byte> editor;
    for (const char ch : editorId) editor.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    editor.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", editor);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makePatch() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

    fo4::test::ByteWriter dataA;
    dataA.u32(0x11111111);
    dataA.u32(0x00000000);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"MasterA.esm\0", 12});
    fo4::test::write_subrecord(out, "DATA", dataA.bytes);

    fo4::test::ByteWriter dataB;
    dataB.u32(0x22222222);
    dataB.u32(0x00000000);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"MasterB.esp\0", 12});
    fo4::test::write_subrecord(out, "DATA", dataB.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x02000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PatchRecord\0", 12});
    fo4::test::ByteWriter name;
    name.u32(0x01000800);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

std::string textResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& masterA,
                 const std::filesystem::path& masterB,
                 const std::filesystem::path& patch) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("MasterA.esm", masterA),
            pluginEntry("MasterB.esp", masterB),
            pluginEntry("Patch.esp", patch)
        })}
    };
    auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 3);
}

}

TEST_CASE("Masters reorder writes immediately and preserves logical FormLinks") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterA = root / "MasterA.esm";
    const auto masterB = root / "MasterB.esp";
    const auto patch = root / "Patch.esp";
    writeBytes(masterA, makeMaster("MasterAKeyword"));
    writeBytes(masterB, makeMaster("MasterBKeyword"));
    writeBytes(patch, makePatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterA, masterB, patch);

    auto before = parseStringResult(
        backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    REQUIRE(before.at("masters").size() == 2);
    CHECK(before.at("masters")[0].at("name") == "MasterA.esm");
    CHECK(before.at("masters")[1].at("name") == "MasterB.esp");

    auto depsBefore = parseStringResult(
        backend.invoke("backend.GetDependencies",
                       nlohmann::json::array({"000801:Patch.esp"})));
    REQUIRE(depsBefore.size() == 1);
    CHECK(depsBefore[0].at("FormKey") == "000800:MasterB.esp");

    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(patch));

    const auto reorder = textResult(
        backend.invoke("masters.Reorder",
                       nlohmann::json::array({"Patch.esp", "[\"MasterB.esp\",\"MasterA.esm\"]"})));
    CHECK_FALSE(reorder.empty());

    auto after = parseStringResult(
        backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    REQUIRE(after.at("masters").size() == 2);
    CHECK(after.at("masters")[0].at("name") == "MasterB.esp");
    CHECK(after.at("masters")[1].at("name") == "MasterA.esm");
    CHECK(after.at("masters")[0].at("size") == 0x22222222u);
    CHECK(after.at("masters")[1].at("size") == 0x11111111u);

    auto depsAfter = parseStringResult(
        backend.invoke("backend.GetDependencies",
                       nlohmann::json::array({"000801:Patch.esp"})));
    REQUIRE(depsAfter.size() == 1);
    CHECK(depsAfter[0].at("FormKey") == "000800:MasterB.esp");

    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patch)) == beforeSave);
    CHECK(reorder.find("SavePlugin") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(patch);
    REQUIRE(reloaded.header.masters.size() == 2);
    CHECK(reloaded.header.masters[0].key.filename() == "MasterB.esp");
    CHECK(reloaded.header.masters[1].key.filename() == "MasterA.esm");
    const auto links = fo4::collect_form_links(reloaded);
    REQUIRE(links.size() == 1);
    CHECK(links[0].key.to_string() == "000800:MasterB.esp");
}

TEST_CASE("Masters reorder rejects incomplete or duplicate orders without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterA = root / "MasterA.esm";
    const auto masterB = root / "MasterB.esp";
    const auto patch = root / "Patch.esp";
    writeBytes(masterA, makeMaster("MasterAKeyword"));
    writeBytes(masterB, makeMaster("MasterBKeyword"));
    writeBytes(patch, makePatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterA, masterB, patch);

    const auto incomplete = backend.invoke(
        "masters.Reorder",
        nlohmann::json::array({"Patch.esp", "[\"MasterA.esm\"]"}));
    REQUIRE_FALSE(incomplete.has_value());
    CHECK(incomplete.error().type == "InvalidMasterOrder");

    const auto duplicate = backend.invoke(
        "masters.Reorder",
        nlohmann::json::array({"Patch.esp", "[\"MasterA.esm\",\"MasterA.esm\"]"}));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().type == "InvalidMasterOrder");

    const auto reloaded = fo4::read_plugin_file(patch);
    REQUIRE(reloaded.header.masters.size() == 2);
    CHECK(reloaded.header.masters[0].key.filename() == "MasterA.esm");
    CHECK(reloaded.header.masters[1].key.filename() == "MasterB.esp");
}

TEST_CASE("Masters light toggle is in memory until Save Plugin persists it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterA = root / "MasterA.esm";
    const auto masterB = root / "MasterB.esp";
    const auto patch = root / "Patch.esp";
    writeBytes(masterA, makeMaster("MasterAKeyword"));
    writeBytes(masterB, makeMaster("MasterBKeyword"));
    writeBytes(patch, makePatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterA, masterB, patch);
    REQUIRE_FALSE(fo4::read_plugin_file(patch).header.tes4_flags.is_small_file());

    const auto toggled = textResult(
        backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true})));
    CHECK(toggled.rfind("Set the ESL flag", 0) == 0);

    auto inMemory = parseStringResult(
        backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    CHECK(inMemory.at("light") == true);
    CHECK_FALSE(fo4::read_plugin_file(patch).header.tes4_flags.is_small_file());

    const auto saved = textResult(
        backend.invoke("masters.SavePlugin", nlohmann::json::array({"Patch.esp"})));
    CHECK(saved.rfind("Saved ", 0) == 0);
    CHECK(fo4::read_plugin_file(patch).header.tes4_flags.is_small_file());

    const auto cleared = textResult(
        backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", false})));
    CHECK(cleared.rfind("Cleared the ESL flag", 0) == 0);
    REQUIRE(backend.invoke("masters.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(fo4::read_plugin_file(patch).header.tes4_flags.is_small_file());
}

TEST_CASE("Native plugin kind follows the filename when the TES4 flags are clear") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto lightPath = root / "Light.esl";
    const auto basePath = root / "Base.esm";
    const auto plainPath = root / "Plain.esp";
    const auto userPath = root / "User.esp";
    writeBytes(lightPath, fo4::test::make_tes4_plugin(0, 1.0f, {}));
    writeBytes(basePath, fo4::test::make_tes4_plugin(0, 1.0f, {}));
    writeBytes(plainPath, fo4::test::make_tes4_plugin(0, 1.0f, {}));
    writeBytes(userPath, fo4::test::make_tes4_plugin(0, 1.0f, {"Light.esl"}));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Light.esl", lightPath),
            pluginEntry("Base.esm", basePath),
            pluginEntry("Plain.esp", plainPath),
            pluginEntry("User.esp", userPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    REQUIRE(active.size() == 4);
    const std::vector<std::string> kinds{"light", "master", "plugin", "plugin"};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        CHECK(active.at(index).at("Kind") == kinds[index]);
    }

    const auto light = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Light.esl"})));
    CHECK(light.at("light") == true);
    const auto plain = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"Plain.esp"})));
    CHECK(plain.at("light") == false);
    const auto user = parseStringResult(backend.invoke("masters.List", nlohmann::json::array({"User.esp"})));
    REQUIRE(user.at("masters").size() == 1);
    CHECK(user.at("masters")[0].at("kind") == "light");
}

// Detects: explicit HEDR repair must stage and persist the recursive count without changing the plugin first.
TEST_CASE("Native HEDR repair stages and saves a stale count") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto masterA = root / "MasterA.esm";
    const auto masterB = root / "MasterB.esp";
    writeBytes(masterA, makeMaster("MasterAKeyword"));
    writeBytes(masterB, makeMaster("MasterBKeyword"));
    writeBytes(path, makePatch());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterA, masterB, path);
    REQUIRE(fo4::read_plugin_file(path).header.record_count == 1u);
    REQUIRE(backend.invoke("backend.RepairHedrCount", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fo4::read_plugin_file(path).header.record_count == 1u);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fo4::read_plugin_file(path).header.record_count == 2u);
}

TEST_CASE("Native UpdateOnam treats a filename .esm without the master flag as a master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto flaglessPath = root / "Flagless.esm";
    const auto plainPath = root / "Plain.esp";
    writeBytes(flaglessPath, fo4::test::make_tes4_plugin(0, 1.0f, {}));
    writeBytes(plainPath, fo4::test::make_tes4_plugin(0, 1.0f, {}));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Flagless.esm", flaglessPath),
            pluginEntry("Plain.esp", plainPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto plain = textResult(backend.updateOnam(nlohmann::json::array({"Plain.esp"})));
    CHECK(plain == "Plain.esp is not master-flagged; the game ignores ONAM there.");
    const auto flagless = textResult(backend.updateOnam(nlohmann::json::array({"Flagless.esm"})));
    CHECK(flagless == "ONAM in Flagless.esm is already up to date.");
}
