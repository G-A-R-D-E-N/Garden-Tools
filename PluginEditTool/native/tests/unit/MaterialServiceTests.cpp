#include <bit>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/material/MaterialService.hpp>

namespace {

void u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) { bytes.push_back(value); }
void boolean(std::vector<std::uint8_t>& bytes, bool value) { u8(bytes, value ? 1U : 0U); }
void u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}
void f32(std::vector<std::uint8_t>& bytes, float value) { u32(bytes, std::bit_cast<std::uint32_t>(value)); }
void string(std::vector<std::uint8_t>& bytes, std::string value) {
    if (value.empty() || value.back() != '\0') value.push_back('\0');
    u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void color(std::vector<std::uint8_t>& bytes, float r, float g, float b) {
    f32(bytes, r); f32(bytes, g); f32(bytes, b);
}

std::vector<std::uint8_t> minimalBgem(bool trailing = false, std::uint32_t tileFlags = 3U, std::uint8_t effectLighting = 1U) {
    std::vector<std::uint8_t> bytes;
    u32(bytes, 0x4D454742U); // BGEM
    u32(bytes, 0); // version
    u32(bytes, tileFlags); // tile flags
    f32(bytes, 0.0F); f32(bytes, 0.0F); f32(bytes, 1.0F); f32(bytes, 1.0F);
    f32(bytes, 1.0F); // alpha
    u8(bytes, 0); u32(bytes, 0); u32(bytes, 0); u8(bytes, 0);
    boolean(bytes, false); // alpha test
    boolean(bytes, true);  // z write
    boolean(bytes, true);  // z test
    boolean(bytes, false); // ssr
    boolean(bytes, false); // wet ssr
    boolean(bytes, false); // decal
    boolean(bytes, false); // two sided
    boolean(bytes, false); // decal no fade
    boolean(bytes, false); // non occluder
    boolean(bytes, false); // refraction
    boolean(bytes, false); // refraction falloff
    f32(bytes, 0.0F);      // refraction power
    boolean(bytes, false); // pre-v10 env mapping
    f32(bytes, 0.0F);      // env map mask scale
    boolean(bytes, false); // grayscale palette

    string(bytes, "textures/base.dds");
    string(bytes, "");
    string(bytes, "");
    string(bytes, "textures/normal.dds");
    string(bytes, "");

    boolean(bytes, false); // blood
    u8(bytes, effectLighting); // effect lighting
    boolean(bytes, false); // falloff
    boolean(bytes, false); // falloff color
    boolean(bytes, false); // grayscale alpha
    boolean(bytes, false); // soft
    color(bytes, 1.0F, 0.5F, 0.25F);
    f32(bytes, 1.0F); // base color scale
    f32(bytes, 0.0F); // falloff start angle
    f32(bytes, 1.0F); // falloff stop angle
    f32(bytes, 1.0F); // falloff start opacity
    f32(bytes, 0.0F); // falloff stop opacity
    f32(bytes, 1.0F); // lighting influence
    u8(bytes, 0);     // env LOD
    f32(bytes, 0.0F); // soft depth

    if (trailing) {
        bytes.push_back(0xAA);
        bytes.push_back(0xBB);
        bytes.push_back(0xCC);
    }
    return bytes;
}

std::filesystem::path tempFile(std::string_view suffix) {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
        ("fo4ide_material_" + id + std::string(suffix));
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

struct Cleanup {
    std::vector<std::filesystem::path> paths;
    ~Cleanup() {
        std::error_code ec;
        for (const auto& path : paths) std::filesystem::remove(path, ec);
    }
};

const nlohmann::json* field(const nlohmann::json& document, std::string_view name) {
    for (const auto& row : document.at("fields")) {
        if (row.at("name").get_ref<const std::string&>() == name) return &row;
    }
    return nullptr;
}

}

TEST_CASE("Native material service inspects BGEM fields") {
    const auto path = tempFile(".bgem");
    Cleanup cleanup{{path}};
    writeFile(path, minimalBgem());

    fo4ide::material::MaterialService service;
    const auto inspected = service.inspect(path);
    REQUIRE(inspected.has_value());
    CHECK(inspected->at("format") == "BGEM");
    CHECK(inspected->at("version") == 0);
    REQUIRE(field(*inspected, "BaseTexture") != nullptr);
    CHECK(field(*inspected, "BaseTexture")->at("value") == "textures/base.dds");
    CHECK(field(*inspected, "TileU")->at("value") == "true");
    CHECK(field(*inspected, "BaseColor")->at("value") == "1, 0.5, 0.25");
}

TEST_CASE("Native material service edits and reloads fields") {
    const auto source = tempFile(".bgem");
    const auto output = tempFile("_edited.bgem");
    Cleanup cleanup{{source, output}};
    writeFile(source, minimalBgem());

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{
        {"SoftEnabled", "true"},
        {"BaseColorScale", "2.5"},
        {"BaseTexture", "textures/changed.dds"},
        {"TileV", "false"}
    };
    const auto changed = service.setFields(source, changes, output);
    REQUIRE(changed.has_value());

    const auto inspected = service.inspect(output);
    REQUIRE(inspected.has_value());
    CHECK(field(*inspected, "SoftEnabled")->at("value") == "true");
    CHECK(field(*inspected, "BaseColorScale")->at("value") == "2.5");
    CHECK(field(*inspected, "BaseTexture")->at("value") == "textures/changed.dds");
    CHECK(field(*inspected, "TileV")->at("value") == "false");
}

TEST_CASE("Native material writer preserves unknown trailing bytes") {
    const auto source = tempFile(".bgem");
    const auto output = tempFile("_edited.bgem");
    Cleanup cleanup{{source, output}};
    writeFile(source, minimalBgem(true));

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{{"SoftDepth", "3.25"}};
    REQUIRE(service.setFields(source, changes, output).has_value());

    const auto bytes = readFile(output);
    REQUIRE(bytes.size() >= 3);
    CHECK(bytes[bytes.size() - 3] == 0xAA);
    CHECK(bytes[bytes.size() - 2] == 0xBB);
    CHECK(bytes[bytes.size() - 1] == 0xCC);

    const auto inspected = service.inspect(output);
    REQUIRE(inspected.has_value());
    CHECK(inspected->at("trailingBytes") == 3);
}

TEST_CASE("Native material service refuses layout-changing version edits") {
    const auto source = tempFile(".bgem");
    Cleanup cleanup{{source}};
    writeFile(source, minimalBgem());

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{{"Version", "22"}};
    const auto changed = service.setFields(source, changes);
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "MaterialFieldParseFailed");
}

TEST_CASE("Native material edit keeps raw bool and tile flag bytes on unrelated fields") {
    const auto source = tempFile(".bgem");
    const auto output = tempFile("_edited.bgem");
    Cleanup cleanup{{source, output}};
    const auto original = minimalBgem(false, 0xF3U, 2);
    writeFile(source, original);

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{{"SoftDepth", "3.25"}};
    REQUIRE(service.setFields(source, changes, output).has_value());

    auto expected = original;
    expected.resize(expected.size() - 4);
    f32(expected, 3.25F);
    CHECK(readFile(output) == expected);
}

TEST_CASE("Native material service refuses an embedded NUL in a string field without writing") {
    const auto source = tempFile(".bgem");
    Cleanup cleanup{{source}};
    const auto original = minimalBgem();
    writeFile(source, original);

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{{"BaseTexture", std::string("a\0b", 3)}};
    const auto changed = service.setFields(source, changes);
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "MaterialFieldParseFailed");
    CHECK(readFile(source) == original);
}

TEST_CASE("Native material save keeps an existing destination when the rename fails") {
    const auto source = tempFile(".bgem");
    const auto output = tempFile("_blocked.bgem");
    Cleanup cleanup{{source, output}};
    writeFile(source, minimalBgem());
    std::filesystem::create_directories(output);

    fo4ide::material::MaterialService service;
    const std::map<std::string, std::string, std::less<>> changes{{"SoftDepth", "3.25"}};
    const auto changed = service.setFields(source, changes, output);
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "MaterialWriteFailed");
    CHECK(std::filesystem::is_directory(output));
    const auto stagePrefix = output.filename().string() + ".radicle-tmp";
    for (const auto& entry : std::filesystem::directory_iterator(output.parent_path())) {
        CHECK_FALSE(entry.path().filename().string().starts_with(stagePrefix));
    }
}
