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

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_scalar_" + id);
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
                                  bool duplicateXhlt = false,
                                  bool withXscl = false,
                                  float scaleValue = 1.0f) {
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
    if (withXscl) {
        fo4::test::ByteWriter scale;
        scale.f32(scaleValue);
        fo4::test::write_subrecord(out, "XSCL", scale.bytes);
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

std::string readScalar(const std::filesystem::path& path,
                       fo4::Signature signature) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        plugin, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    const std::string_view fieldPath = signature == fo4::Signature::from_string("XSCL") ? "REFR\\Scale" : "REFR\\HealthPercent";
    const auto descriptor = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("REFR"), fieldPath);
    REQUIRE(descriptor.has_value());
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != signature) continue;
        return fo4::schema::decode_scalar(subrecord.data, descriptor->scalar);
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

std::size_t columnIndex(const nlohmann::json& value, std::string_view column) {
    const auto& columns = value.at("columns");
    std::size_t index = 0;
    while (index < columns.size() && columns[index].get<std::string>() != column) ++index;
    if (index == columns.size()) FAIL("Spreadsheet column is missing: " << column);
    return index;
}

}

TEST_CASE("Native schema SetField defers REFR XHLT persistence until SavePlugin") {
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
    CHECK(readScalar(path, fo4::Signature::from_string("XHLT")) == "50");

    const auto invalid = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "101"
    }));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "ScalarFieldWriteFailed");
    CHECK(readScalar(path, fo4::Signature::from_string("XHLT")) == "50");

    const auto pending = grid(backend);
    const auto health = columnIndex(pending, "REFR\\HealthPercent");
    REQUIRE(pending.at("rows").size() == 1);
    CHECK(pending.at("rows")[0].at("cells")[health] == "75");

    const auto saved = backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(saved.has_value());
    CHECK(readScalar(path, fo4::Signature::from_string("XHLT")) == "75");
}

TEST_CASE("Native schema SavePlugin failure preserves pending scalar state") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto loadedTime = std::filesystem::last_write_time(path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    }));
    REQUIRE(result.has_value());

    writeBytes(path, makePlugin(true, false, 60));
    std::filesystem::last_write_time(path, loadedTime + std::chrono::seconds(2));

    const auto saved = backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "PluginChangedOnDisk");
    CHECK(readScalar(path, fo4::Signature::from_string("XHLT")) == "60");

    const auto pending = grid(backend);
    const auto health = columnIndex(pending, "REFR\\HealthPercent");
    REQUIRE(pending.at("rows").size() == 1);
    CHECK(pending.at("rows")[0].at("cells")[health] == "75");
}

TEST_CASE("Native schema SetField defers generated REFR Scale until SavePlugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 50, false, true, 1.0f));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\Scale", "1.25"
    }));
    REQUIRE(result.has_value());
    CHECK(readScalar(path, fo4::Signature::from_string("XSCL")) == "1");

    const auto pending = grid(backend);
    const auto scale = columnIndex(pending, "REFR\\Scale");
    REQUIRE(pending.at("rows").size() == 1);
    CHECK(pending.at("rows")[0].at("cells")[scale] == "1.25");

    const auto saved = backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(saved.has_value());
    CHECK(readScalar(path, fo4::Signature::from_string("XSCL")) == "1.25");
}

TEST_CASE("Native schema SetField rejects invalid generated Scale without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 50, false, true, 1.0f));
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\Scale", "10.01"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldWriteFailed");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native schema SetField rejects invalid REFR percentage without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "101"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldWriteFailed");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

// Invariant: a scalar whose optional subrecord is absent must be written by creating that subrecord where the authoring schema places it.
TEST_CASE("Native schema SetField creates a missing optional scalar subrecord at its authoring position") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(false));
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto health = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "50"
    }));
    if (!health.has_value()) FAIL("health set failed: " << health.error().type);
    const auto scale = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\Scale", "1"
    }));
    if (!scale.has_value()) FAIL("scale set failed: " << scale.error().type);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    const auto has = [&](std::string_view signature) {
        const auto wanted = fo4::Signature::from_string(signature);
        for (const auto& sub : record->subrecords) {
            if (sub.signature == wanted) return !sub.data.empty();
        }
        return false;
    };
    CHECK(has("XHLT"));
    CHECK(has("XSCL"));
    CHECK(fo4::test::original_bytes(disk) != before);
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
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path, "Data");

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginReadOnly");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("REFR spreadsheet exposes generated scalar fields") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 50, false, true, 1.25f));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    const auto health = columnIndex(value, "REFR\\HealthPercent");
    const auto scale = columnIndex(value, "REFR\\Scale");
    REQUIRE(value.at("rows").size() == 1);
    CHECK(value.at("rows")[0].at("cells")[health] == "50");
    CHECK(value.at("rows")[0].at("editable")[health] == true);
    CHECK(value.at("rows")[0].at("cells")[scale] == "1.25");
    CHECK(value.at("rows")[0].at("editable")[scale] == true);
}

TEST_CASE("REFR spreadsheet keeps missing generated scalars blank and read-only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(false));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    REQUIRE(value.at("rows").size() == 1);
    for (const auto& field :
         fo4::schema::scalar_fields(fo4::Signature::from_string("REFR"))) {
        const auto& address = field.scalar.address;
        const bool counted = address.repeated || address.stride != 0;
        const auto index = columnIndex(
            value, std::string(field.path) + (counted ? "\\Count" : ""));
        CHECK(value.at("rows")[0].at("cells")[index] == (counted ? "0" : ""));
        CHECK(value.at("rows")[0].at("editable")[index] == false);
    }
}

TEST_CASE("REFR spreadsheet preserves unusual existing scalar values") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, false, 150, false, true, 12.0f));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto value = grid(backend);
    REQUIRE(value.at("rows").size() == 1);
    const auto health = columnIndex(value, "REFR\\HealthPercent");
    const auto scale = columnIndex(value, "REFR\\Scale");
    CHECK(value.at("rows")[0].at("cells")[health] == "150");
    CHECK(value.at("rows")[0].at("editable")[health] == true);
    CHECK(value.at("rows")[0].at("cells")[scale] == "12");
    CHECK(value.at("rows")[0].at("editable")[scale] == true);
}

TEST_CASE("REFR spreadsheet rejects duplicate generated scalar subrecords") {
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
