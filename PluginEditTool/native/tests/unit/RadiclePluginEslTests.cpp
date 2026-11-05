#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_esl_" + id);
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

std::vector<std::byte> makePlugin(const std::vector<std::uint32_t>& objectIds,
                                  bool hasMaster = false,
                                  std::uint32_t nextFormId = 0x800) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(static_cast<std::uint32_t>(objectIds.size()));
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (hasMaster) {
        fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (std::size_t index = 0; index < objectIds.size(); ++index) {
        const auto record = fo4::test::begin_record(
            out, "MISC", 0, objectIds[index] | (hasMaster ? 0x01000000u : 0u));
        const auto editor = "Record" + std::to_string(index);
        std::vector<std::byte> editorBytes;
        editorBytes.reserve(editor.size() + 1);
        for (const char ch : editor) {
            editorBytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        editorBytes.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "EDID", editorBytes);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& path) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "Test Mod"},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);
}

std::string eligibility(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "CheckEslEligibility", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

}

TEST_CASE("Native ESL eligibility reports already-compatible plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000800, 0x00000FFF}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("already ESL-compatible") != std::string::npos);
    CHECK(report.find("2 native record") != std::string::npos);
}

TEST_CASE("Native ESL eligibility reports compactable out-of-range records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000800, 0x00001000, 0x00002000}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("can fit the ESL compacted range") != std::string::npos);
    CHECK(report.find("remapping 2 of 3") != std::string::npos);
    CHECK(report.find("read-only") != std::string::npos);
}

TEST_CASE("Native ESL eligibility refuses plugins larger than the compacted range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    std::vector<std::uint32_t> ids;
    ids.reserve(2049);
    for (std::uint32_t i = 0; i < 2049; ++i) {
        ids.push_back(0x00001000u + i);
    }
    writeBytes(path, makePlugin(ids));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("CANNOT fit") != std::string::npos);
    CHECK(report.find("2049 native record") != std::string::npos);
    CHECK(report.find("2048-slot") != std::string::npos);
}

TEST_CASE("Native ESL compaction uses the target light range boundaries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x000u, 0x001u, 0x7FFu, 0x800u, 0xFFFu, 0x1000u}, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 2 record") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK_FALSE(disk.header.tes4_flags.is_small_file());
    for (const auto local : {0x001u, 0x002u, 0x003u, 0x7FFu, 0x800u, 0xFFFu}) {
        CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{local}}) != nullptr);
    }
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0u}}) == nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0x1000u}}) == nullptr);
}

TEST_CASE("Native ESL compaction can fill all 4095 target slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    std::vector<std::uint32_t> ids;
    for (std::uint32_t local = 1; local < 0xFFFu; ++local) ids.push_back(local);
    ids.push_back(0x1000u);
    writeBytes(path, makePlugin(ids, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    CHECK(eligibility(backend).find("can fit") != std::string::npos);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 1 record") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0xFFFu}}) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0x1000u}}) == nullptr);
}

TEST_CASE("Native ESL compaction refuses 4096 own records without staging changes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    std::vector<std::uint32_t> ids;
    for (std::uint32_t local = 1; local <= 0x1000u; ++local) ids.push_back(local);
    const auto original = makePlugin(ids, true);
    writeBytes(path, original);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "EslCapacityExceeded");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> actual{std::istreambuf_iterator<char>{stream}, {}};
    REQUIRE(actual.size() == original.size());
    CHECK(std::memcmp(actual.data(), original.data(), original.size()) == 0);
}

TEST_CASE("Native ESL eligibility counts masterless IDs below the full floor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000700, 0x00000800}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("remapping 1 of 2") != std::string::npos);
    CHECK(report.find("0x800-0xFFF") != std::string::npos);
}

TEST_CASE("Native ESL eligibility reports a HEDR-only lowering when every record is already in range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000005u, 0x00000900u}, true, 0x5000u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("would only lower HEDR nextFormID to 0x901.") != std::string::npos);
    CHECK(report.find("no-op") == std::string::npos);
}

TEST_CASE("Native ESL eligibility reports a no-op when HEDR next is already in range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000005u, 0x00000900u}, true, 0x901u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("would be a no-op.") != std::string::npos);
}

TEST_CASE("Native CompactToEsl lowers an out-of-range HEDR next ID when every record is already in range") {
    bool hasMaster = true;
    std::vector<std::uint32_t> ids{0x005u, 0x900u};
    std::uint32_t nextFormId = 0x5000u;
    std::uint32_t expectedNext = 0x901u;
    std::string expectedKey = "000901:Patch.esp";
    SECTION("Plugins with HEDR 1.0 and a master keep the 0x001 floor") {}
    SECTION("Masterless plugins keep the 0x800 floor") {
        hasMaster = false;
        ids = {0x800u, 0x850u};
        nextFormId = 0x2000u;
        expectedNext = 0x851u;
        expectedKey = "000851:Patch.esp";
    }
    SECTION("HEDR next exactly one past the ESL range lowers to the highest own ID") {
        ids = {0x005u, 0x850u};
        nextFormId = 0x1000u;
        expectedNext = 0x851u;
        expectedKey = "000851:Patch.esp";
    }
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(ids, hasMaster, nextFormId));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto compacted = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(compacted.has_value());
    CHECK(compacted->get<std::string>().find("lowered HEDR nextFormID") != std::string::npos);
    CHECK(compacted->get<std::string>().find("masters.SetLight") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fo4::read_plugin_file(path).header.next_form_id == expectedNext);

    REQUIRE(backend.invoke(
        "masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    const auto created = backend.invokeLegacyBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "EYES", nlohmann::json{
            {"editorId", "LowNext"},
            {"fullText", "Low Next"},
            {"icon", "Textures\\Actors\\Character\\Eyes\\low.dds"}
        }}));
    REQUIRE(created.has_value());
    CHECK(nlohmann::json::parse(created->get<std::string>()).at("formKey") == expectedKey);
}

TEST_CASE("Native CompactToEsl HEDR correction survives save and reload and later creation") {
    bool hasMaster = true;
    std::vector<std::uint32_t> ids{0x005u, 0x900u};
    std::uint32_t nextFormId = 0x5000u;
    std::uint32_t expectedNext = 0x901u;
    std::string expectedKey = "000901:Patch.esp";
    SECTION("Plugins with HEDR 1.0 and a master keep the 0x001 floor") {}
    SECTION("Masterless plugins keep the 0x800 floor") {
        hasMaster = false;
        ids = {0x800u, 0x850u};
        nextFormId = 0x2000u;
        expectedNext = 0x851u;
        expectedKey = "000851:Patch.esp";
    }
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(ids, hasMaster, nextFormId));

    {
        fo4ide::plugin::RadiclePluginBackend backend;
        loadBackend(backend, path);
        REQUIRE(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})).has_value());
        REQUIRE(backend.invoke(
            "masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }

    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadBackend(reloaded, path);
    const auto listed = reloaded.invoke("masters.List", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(listed.has_value());
    REQUIRE(listed->is_string());
    CHECK(nlohmann::json::parse(listed->get<std::string>()).at("light") == true);
    {
        const auto disk = fo4::read_plugin_file(path);
        CHECK(disk.header.tes4_flags.is_small_file());
        CHECK(disk.header.next_form_id == expectedNext);
    }

    const auto created = reloaded.invokeLegacyBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "EYES", nlohmann::json{
            {"editorId", "LaterCreate"},
            {"fullText", "Later Create"},
            {"icon", "Textures\\Actors\\Character\\Eyes\\later.dds"}
        }}));
    REQUIRE(created.has_value());
    CHECK(nlohmann::json::parse(created->get<std::string>()).at("formKey") == expectedKey);
    REQUIRE(reloaded.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    fo4ide::plugin::RadiclePluginBackend fresh;
    loadBackend(fresh, path);
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.next_form_id == expectedNext + 1u);
    const auto* newRecord = fo4::find_record(
        disk, fo4::FormKey{disk.key, fo4::LocalFormId{expectedNext}});
    CHECK(newRecord != nullptr);
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto* record = fo4::find_record(
            disk, fo4::FormKey{disk.key, fo4::LocalFormId{ids[index]}});
        REQUIRE(record != nullptr);
        const auto& edid = record->subrecords.front().data;
        CHECK(std::string(reinterpret_cast<const char*>(edid.data()), edid.size()) ==
              "Record" + std::to_string(index) + '\0');
    }
}

TEST_CASE("Native SetLight refuses a masterless plugin with owned IDs below 0x800") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000005u, 0x00000900u}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.invoke(
        "masters.SetLight", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "SetLightFailed");

    const auto listed = backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(listed.has_value());
    REQUIRE(listed->is_string());
    CHECK(nlohmann::json::parse(listed->get<std::string>()).at("light") == false);
}
