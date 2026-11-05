#include <array>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_lvsp_grid_" + id);
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

std::vector<std::byte> lvlo(std::uint16_t level, std::uint32_t formId,
                            std::uint16_t count, std::uint8_t chanceNone) {
    fo4::test::ByteWriter out;
    out.u16(level);
    out.u16(0);
    out.u32(formId);
    out.u16(count);
    out.u8(chanceNone);
    out.u8(0);
    return out.bytes;
}

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVSP", 0);
    const auto record = fo4::test::begin_record(out, "LVSP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"GridList\0", 9});
    const std::array<std::byte, 1> lvld{std::byte{15}};
    const std::array<std::byte, 1> lvlm{std::byte{0}};
    const std::array<std::byte, 1> lvlf{std::byte{3}};
    const std::array<std::byte, 1> llct{std::byte{1}};
    fo4::test::write_subrecord(out, "LVLD", lvld);
    fo4::test::write_subrecord(out, "LVLM", lvlm);
    fo4::test::write_subrecord(out, "LVLF", lvlf);
    fo4::test::write_subrecord(out, "LLCT", llct);
    fo4::test::write_subrecord(out, "LVLO", lvlo(5, 0x00000810, 2, 7));
    fo4::test::end_record(out, record);

    const auto nested = fo4::test::begin_record(out, "LVSP", 0, 0x00000810);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"NestedList\0", 11});
    const std::array<std::byte, 1> zero{std::byte{0}};
    fo4::test::write_subrecord(out, "LVLD", zero);
    fo4::test::write_subrecord(out, "LVLM", zero);
    fo4::test::write_subrecord(out, "LVLF", zero);
    fo4::test::write_subrecord(out, "LLCT", zero);
    fo4::test::end_record(out, nested);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
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

}

TEST_CASE("LVSP spreadsheet exposes typed list entries with read-only missing slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto grid = parseStringResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Patch.esp", "LVSP", 200, 0})));

    CHECK(grid.at("columns") == nlohmann::json::array({
        "EDID",
        "LVSP\\ChanceNone",
        "LVSP\\MaxCount",
        "LVSP\\Flags",
        "LVSP\\EntryCount",
        "LVSP\\Entries[0]\\Level",
        "LVSP\\Entries[0]\\Spell",
        "LVSP\\Entries[0]\\Count",
        "LVSP\\Entries[0]\\ChanceNone"
    }));
    REQUIRE(grid.at("rows").size() == 2);

    const auto& row = grid.at("rows")[0];
    CHECK(row.at("formKey") == "000800:Patch.esp");
    CHECK(row.at("editorId") == "GridList");
    CHECK(row.at("cells") == nlohmann::json::array({
        "GridList", "15", "0", "3", "1", "5", "000810:Patch.esp", "2", "7"
    }));
    CHECK(row.at("editable") == nlohmann::json::array({
        true, true, true, true, false, true, true, true, true
    }));

    const auto& nested = grid.at("rows")[1];
    CHECK(nested.at("formKey") == "000810:Patch.esp");
    CHECK(nested.at("cells") == nlohmann::json::array({
        "NestedList", "0", "0", "0", "0", "", "", "", ""
    }));
    CHECK(nested.at("editable") == nlohmann::json::array({
        true, true, true, true, false, false, false, false, false
    }));
}

TEST_CASE("LVSP spreadsheet column name round-trips through SetField") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto edit = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            "LVSP\\Entries[0]\\Level",
            "44"
        }));
    REQUIRE(edit.has_value());

    const auto grid = parseStringResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Patch.esp", "LVSP", 200, 0})));
    CHECK(grid.at("rows")[0].at("cells")[5] == "44");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(path);
    const auto model = fo4::forms::decode_leveled_spell(
        reloaded, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(model.entries.size() == 1);
    CHECK(model.entries[0].level == 44u);
}
