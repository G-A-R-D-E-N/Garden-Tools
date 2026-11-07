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

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_eyes_grid_" + id);
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

void addEyes(fo4::test::ByteWriter& out,
             std::uint32_t formId,
             std::string_view edid,
             bool opaqueFull,
             std::uint8_t flags) {
    const auto record = fo4::test::begin_record(out, "EYES", 0, formId);
    fo4::test::write_subrecord(out, "EDID", edid);
    if (opaqueFull) {
        const std::array<std::byte, 4> full{
            std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11}};
        fo4::test::write_subrecord(out, "FULL", full);
    } else {
        fo4::test::write_subrecord(out, "FULL", std::string_view{"Inline Eyes\0", 12});
    }
    fo4::test::write_subrecord(out, "ICON", std::string_view{"Textures\\Eyes\\eye.dds\0", 22});
    const std::array<std::byte, 1> data{std::byte{flags}};
    fo4::test::write_subrecord(out, "DATA", data);
    fo4::test::end_record(out, record);
}

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "EYES", 0);
    addEyes(out, 0x00000800, std::string_view{"InlineEye\0", 10}, false, 3);
    addEyes(out, 0x00000801, std::string_view{"LocalizedEye\0", 13}, true, 4);
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
            {"name", "EyesPatch.esp"},
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

TEST_CASE("EYES spreadsheet exposes typed writable columns") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto grid = parseStringResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"EyesPatch.esp", "EYES", 200, 0})));

    CHECK(grid.at("columns") == nlohmann::json::array({
        "EDID", "EYES\\Full", "EYES\\Icon", "EYES\\DataFlags"
    }));
    REQUIRE(grid.at("rows").size() == 2);

    const auto& inlineRow = grid.at("rows")[0];
    CHECK(inlineRow.at("formKey") == "000800:EyesPatch.esp");
    CHECK(inlineRow.at("cells") == nlohmann::json::array({
        "InlineEye", "Inline Eyes", "Textures\\Eyes\\eye.dds", "3"
    }));
    CHECK(inlineRow.at("editable") == nlohmann::json::array({true, true, true, true}));
}

TEST_CASE("EYES spreadsheet marks opaque FULL storage read-only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto grid = parseStringResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"EyesPatch.esp", "EYES", 200, 0})));
    REQUIRE(grid.at("rows").size() == 2);

    const auto& localizedRow = grid.at("rows")[1];
    CHECK(localizedRow.at("formKey") == "000801:EyesPatch.esp");
    CHECK(localizedRow.at("cells")[1] == "<opaque/localized>");
    CHECK(localizedRow.at("editable") == nlohmann::json::array({true, false, true, true}));
}
