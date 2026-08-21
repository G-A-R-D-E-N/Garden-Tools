#include <array>
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

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_scalar_" + std::to_string(id));
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

std::vector<std::byte> makePlugin(bool withXhlt = true,
                                  bool miscRecord = false,
                                  std::uint32_t healthValue = 50,
                                  bool duplicateXhlt = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto signature = miscRecord ? std::string_view{"MISC"} : std::string_view{"REFR"};
    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800);
    if (withXhlt) {
        fo4::test::ByteWriter health;
        health.u32(healthValue);
        fo4::test::write_subrecord(out, "XHLT", health.bytes);
        if (duplicateXhlt) {
            fo4::test::ByteWriter duplicate;
            duplicate.u32(healthValue);
            fo4::test::write_subrecord(out, "XHLT", duplicate.bytes);
        }
    }
    const std::array<std::byte, 2> unknown{std::byte{0xAA}, std::byte{0xBB}};
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
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", std::move(source)},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);
}

std::string readHealth(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        plugin, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("XHLT")) continue;
        const auto descriptor = fo4::schema::scalar_descriptor(subrecord.signature);
        REQUIRE(descriptor.has_value());
        return fo4::schema::decode_scalar(subrecord.data, *descriptor);
    }
    return {};
}

nlohmann::json grid(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Patch.esp", "REFR", 200, 0}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native schema SetField writes REFR XHLT as human percentage points") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    }));
    REQUIRE(result.has_value());
    CHECK(readHealth(path) == "75");
}

TEST_CASE("Native schema SetField rejects invalid REFR percentage without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    const auto before = fo4::read_plugin_file(path).original_bytes;

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "101"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldWriteFailed");
    CHECK(fo4::read_plugin_file(path).original_bytes == before);
}

TEST_CASE("Native schema SetField refuses missing XHLT rather than guessing insertion order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(false));
    const auto before = fo4::read_plugin_file(path).original_bytes;

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "50"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldMissing");
    CHECK(fo4::read_plugin_file(path).original_bytes == before);
}

TEST_CASE("Native schema SetField requires REFR record type") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "50"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldRecordMismatch");
}

TEST_CASE("Native schema SetField refuses protected Data plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    const auto before = fo4::read_plugin_file(path).original_bytes;

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path, "Data");

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginReadOnly");
    CHECK(fo4::read_plugin_file(path).original_bytes == before);
}

TEST_CASE("REFR spreadsheet exposes HealthPercent from existing XHLT") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 50));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    REQUIRE(value.at("columns").size() == 2);
    CHECK(value.at("columns")[0] == "EDID");
    CHECK(value.at("columns")[1] == "REFR\\HealthPercent");
    REQUIRE(value.at("rows").size() == 1);
    CHECK(value.at("rows")[0].at("cells")[1] == "50");
    CHECK(value.at("rows")[0].at("editable")[1] == true);
}

TEST_CASE("REFR spreadsheet keeps missing XHLT blank and read-only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(false));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    REQUIRE(value.at("rows").size() == 1);
    CHECK(value.at("rows")[0].at("cells")[1] == "");
    CHECK(value.at("rows")[0].at("editable")[1] == false);
}

TEST_CASE("REFR spreadsheet preserves unusual existing XHLT values") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 150));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    REQUIRE(value.at("rows").size() == 1);
    CHECK(value.at("rows")[0].at("cells")[1] == "150");
    CHECK(value.at("rows")[0].at("editable")[1] == true);
}

TEST_CASE("REFR spreadsheet rejects duplicate XHLT rather than choosing one") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 50, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Patch.esp", "REFR", 200, 0}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarReadFailed");
}
