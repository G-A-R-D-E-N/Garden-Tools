#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/schema/scalar.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_schema_grid_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> makeRefrPlugin(bool includeScalars,
                                      bool duplicateXhlt = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "REFR", 0);
    const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SchemaRef\0", 10});
    if (includeScalars) {
        fo4::test::ByteWriter health;
        health.u32(50);
        fo4::test::write_subrecord(out, "XHLT", health.bytes);
        if (duplicateXhlt) {
            fo4::test::write_subrecord(out, "XHLT", health.bytes);
        }

        fo4::test::ByteWriter scale;
        scale.f32(1.25f);
        fo4::test::write_subrecord(out, "XSCL", scale.bytes);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(const std::filesystem::path& path) {
    return {
        {"name", "Schema.esp"},
        {"path", path.string()},
        {"source", "SchemaTest"},
        {"resolved", true}
    };
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({pluginEntry(path)})}})
        .has_value());
}

}

TEST_CASE("Spreadsheet scalar columns come from generated record descriptors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0})));
    const auto fields =
        fo4::schema::scalar_fields(fo4::Signature::from_string("REFR"));

    REQUIRE(grid.at("columns").size() == fields.size() + 1);
    CHECK(grid.at("columns")[0] == "EDID");
    for (std::size_t index = 0; index < fields.size(); ++index) {
        CHECK(grid.at("columns")[index + 1] == fields[index].path);
    }

    REQUIRE(grid.at("rows").size() == 1);
    const auto& row = grid.at("rows")[0];
    CHECK(row.at("cells")[0] == "SchemaRef");
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto subrecord = fields[index].scalar.subrecord;
        if (subrecord == fo4::Signature::from_string("XHLT")) {
            CHECK(row.at("cells")[index + 1] == "50");
            CHECK(row.at("editable")[index + 1] == true);
        } else if (subrecord == fo4::Signature::from_string("XSCL")) {
            CHECK(row.at("cells")[index + 1] == "1.25");
            CHECK(row.at("editable")[index + 1] == true);
        } else {
            CHECK(row.at("cells")[index + 1] == "");
            CHECK(row.at("editable")[index + 1] == false);
        }
    }
}

TEST_CASE("Missing generated scalar subrecords remain blank and read-only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(false));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0})));
    const auto fields =
        fo4::schema::scalar_fields(fo4::Signature::from_string("REFR"));
    REQUIRE(grid.at("rows").size() == 1);
    const auto& row = grid.at("rows")[0];
    for (std::size_t index = 0; index < fields.size(); ++index) {
        CHECK(row.at("cells")[index + 1] == "");
        CHECK(row.at("editable")[index + 1] == false);
    }
}

TEST_CASE("Duplicate generated scalar subrecords fail closed in Spreadsheet") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(true, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0}));
    REQUIRE_FALSE(grid.has_value());
    CHECK(grid.error().type == "ScalarReadFailed");
}
