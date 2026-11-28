#include <array>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_eyes_" + id);
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

std::vector<std::byte> makeEyesPlugin(bool opaqueFull = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "EYES", 0);
    const auto record = fo4::test::begin_record(out, "EYES", 0x00000004u, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"OldEyes\0", 8});
    if (opaqueFull) {
        const std::array<std::byte, 4> full{
            std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
        fo4::test::write_subrecord(out, "FULL", full);
    } else {
        fo4::test::write_subrecord(out, "FULL", std::string_view{"Old Eyes\0", 9});
    }
    fo4::test::write_subrecord(
        out, "ICON", std::string_view{"Textures\\Actors\\Character\\Eyes\\old.dds\0", 39});
    const std::array<std::byte, 1> data{std::byte{0x03}};
    fo4::test::write_subrecord(out, "DATA", data);
    const std::array<std::byte, 3> unknown{
        std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    fo4::test::write_subrecord(out, "ZZZZ", unknown);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& path,
                 std::string source = "Test Mod") {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "EyesPatch.esp"},
            {"path", path.string()},
            {"source", std::move(source)},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);
}

std::vector<std::byte> unknownData(const fo4::Plugin& plugin) {
    const auto* record = fo4::find_record(
        plugin, fo4::FormKey::parse("000800:EyesPatch.esp"));
    REQUIRE(record != nullptr);
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() == "ZZZZ") return subrecord.data;
    }
    return {};
}

}

TEST_CASE("Native EYES SetField persists FULL ICON and DATA while preserving record flags") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makeEyesPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto full = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\Full", "New Eyes"
        }));
    REQUIRE(full.has_value());

    const auto icon = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\Icon",
            "Textures\\Actors\\Character\\Eyes\\new.dds"
        }));
    REQUIRE(icon.has_value());

    const auto flags = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\DataFlags", "5"
        }));
    REQUIRE(flags.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"EyesPatch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(reloaded, fo4::FormKey::parse("000800:EyesPatch.esp"));
    REQUIRE(record != nullptr);
    const auto scalar = [&](const char* path) {
        const auto field = fo4::schema::scalar_field_descriptor(path);
        REQUIRE(field.has_value());
        return fo4::record_scalar_text(reloaded, *record, *field, 0);
    };
    CHECK(scalar("EYES\\Name") == "New Eyes");
    CHECK(scalar("EYES\\Texture") == "Textures\\Actors\\Character\\Eyes\\new.dds");
    CHECK(scalar("EYES\\Flags") == "5");
    CHECK(record->header.flags.value() == 0x00000004u);
    const std::vector<std::byte> expectedUnknown{
        std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    CHECK(unknownData(reloaded) == expectedUnknown);
}

TEST_CASE("Native EYES FULL edit refuses opaque four-byte storage without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makeEyesPlugin(true));
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\Full", "Unsafe Rewrite"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "EyesWriteFailed");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native EYES SetField validates DATA byte range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makeEyesPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\DataFlags", "256"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidFieldValue");
}

TEST_CASE("Native EYES writes refuse game Data plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makeEyesPlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path, "Data");

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "EyesPatch.esp", "000800:EyesPatch.esp", "EYES\\Icon", "blocked.dds"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginReadOnly");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

// Constraint: detects the generic EYES create dropping the refusal of master, deleted, compressed and small-file record flags, or staging a half-built record after a refusal.
TEST_CASE("Native EYES create refuses compressed record flags and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "EyesPatch.esp";
    writeBytes(path, makeEyesPlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto refused = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "EyesPatch.esp", "EYES",
            nlohmann::json{{"editorId", "BadFlags"}, {"fullText", "Bad"}, {"icon", "bad.dds"},
                           {"recordFlags", 0x00040000u}}
        }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "EyesCreateFailed");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}
