#include <array>
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
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_lvsp_grid_" + std::to_string(id));
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

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVSP", 0);
    const auto record = fo4::test::begin_record(out, "LVSP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"GridList\0", 9});
    const std::array<std::byte, 1> lvld{std::byte{15}};
    const std::array<std::byte, 1> lvlm{std::byte{0}};
    const std::array<std::byte, 1> lvlf{std::byte{3}};
    const std::array<std::byte, 1> llct{std::byte{0}};
    fo4::test::write_subrecord(out, "LVLD", lvld);
    fo4::test::write_subrecord(out, "LVLM", lvlm);
    fo4::test::write_subrecord(out, "LVLF", lvlf);
    fo4::test::write_subrecord(out, "LLCT", llct);
    fo4::test::end_record(out, record);
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

TEST_CASE("LVSP spreadsheet exposes only writable typed scalar columns") {
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
        "LVSP\\Flags"
    }));
    REQUIRE(grid.at("rows").size() == 1);
    const auto& row = grid.at("rows")[0];
    CHECK(row.at("formKey") == "000800:Patch.esp");
    CHECK(row.at("editorId") == "GridList");
    CHECK(row.at("cells") == nlohmann::json::array({"GridList", "15", "0", "3"}));
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
            "LVSP\\ChanceNone",
            "61"
        }));
    REQUIRE(edit.has_value());

    const auto grid = parseStringResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Patch.esp", "LVSP", 200, 0})));
    CHECK(grid.at("rows")[0].at("cells")[1] == "61");

    const auto reloaded = fo4::read_plugin_file(path);
    const auto model = fo4::forms::decode_leveled_spell(
        reloaded, fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(model.chance_none == 61u);
}
