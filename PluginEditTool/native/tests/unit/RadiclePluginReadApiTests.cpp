#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
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
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_readapi_" + id);
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

void writeEdidRecord(fo4::test::ByteWriter& out,
                     std::string_view signature,
                     std::uint32_t formId,
                     std::string_view editorId) {
    const auto record = fo4::test::begin_record(out, signature, 0, formId);
    std::vector<std::byte> editor;
    for (const char ch : editorId) editor.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    editor.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", editor);
    fo4::test::end_record(out, record);
}

std::vector<std::byte> makeBase() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto misc = fo4::test::begin_group(out, "MISC", 0);
    writeEdidRecord(out, "MISC", 0x00000800, "BaseMisc");
    fo4::test::end_group(out, misc);

    const auto weap = fo4::test::begin_group(out, "WEAP", 0);
    writeEdidRecord(out, "WEAP", 0x00000801, "BaseWeapon");
    fo4::test::end_group(out, weap);
    return out.bytes;
}

std::vector<std::byte> makeOverride() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esp\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    writeEdidRecord(out, "WEAP", 0x00000801, "OverrideWeapon");
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

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& base,
                 const std::filesystem::path& overridePath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", base),
            pluginEntry("Override.esp", overridePath)
        })}
    };
    auto result = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(result.has_value());
    CHECK(result->at("loadedPlugins") == 2);
}

}

TEST_CASE("Native read API lists plugin record types in Spreadsheet format") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto over = root / "Override.esp";
    writeBytes(base, makeBase());
    writeBytes(over, makeOverride());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, base, over);

    const auto types = parseResult(backend.invokeBackend(
        "GetPluginRecordTypes", nlohmann::json::array({"Base.esp"})));
    REQUIRE(types.size() == 2);
    CHECK(types[0] == "MISC (1)");
    CHECK(types[1] == "WEAP (1)");
}

TEST_CASE("Native Spreadsheet grid exposes only safe EDID writes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto over = root / "Override.esp";
    writeBytes(base, makeBase());
    writeBytes(over, makeOverride());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, base, over);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Base.esp", "MISC", 200, 0})));
    CHECK(grid.at("total") == 1);
    CHECK(grid.at("offset") == 0);
    REQUIRE(grid.at("rows").size() == 1);
    const auto& columns = grid.at("columns");
    const auto& row = grid.at("rows")[0];
    REQUIRE(columns[0] == "EDID");
    CHECK(row.at("formKey") == "000800:Base.esp");
    CHECK(row.at("editorId") == "BaseMisc");
    CHECK(row.at("cells")[0] == "BaseMisc");
    REQUIRE(row.at("cells").size() == columns.size());
    REQUIRE(row.at("editable").size() == columns.size());

    const auto keywords = std::find(columns.begin(), columns.end(), "MISC\\Keywords\\Count");
    REQUIRE(keywords != columns.end());
    CHECK(row.at("cells")[static_cast<std::size_t>(keywords - columns.begin())] == "0");

    for (std::size_t index = 0; index < columns.size(); ++index) {
        CHECK(row.at("editable")[index] == (index == 0));
    }
}

TEST_CASE("Native grid refuses all cell edits on protected plugins", "[w0]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Fallout4.esm";
    writeBytes(path, makeBase());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
        {"plugins", nlohmann::json::array({pluginEntry("Fallout4.esm", path)})}
    }).has_value());
    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Fallout4.esm", "MISC", 200, 0})));
    REQUIRE(grid.at("rows").size() == 1);
    for (const auto& editable : grid.at("rows")[0].at("editable")) CHECK(editable == false);
    const auto refused = backend.setFieldValue(nlohmann::json::array({
        "Fallout4.esm", "000800:Fallout4.esm", "EDID", "Changed"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginReadOnly");
}

TEST_CASE("Native records-of-type returns winning load-order version") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto over = root / "Override.esp";
    writeBytes(base, makeBase());
    writeBytes(over, makeOverride());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, base, over);

    const auto records = parseResult(backend.invokeBackend(
        "GetRecordsOfType", nlohmann::json::array({"WEAP", "weapon", 100, 0})));
    REQUIRE(records.size() == 1);
    CHECK(records[0].at("FormKey") == "000801:Base.esp");
    CHECK(records[0].at("EditorID") == "OverrideWeapon");
    CHECK(records[0].at("Plugin") == "Override.esp");
}

TEST_CASE("Native record details and plugin matrix describe the winner chain") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto over = root / "Override.esp";
    writeBytes(base, makeBase());
    writeBytes(over, makeOverride());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, base, over);

    const auto details = parseResult(backend.invokeBackend(
        "GetRecordDetails", nlohmann::json::array({"000801:Base.esp"})));
    CHECK(details.at("FormKey") == "000801:Base.esp");
    CHECK(details.at("EditorId") == "OverrideWeapon");
    CHECK(details.at("Signature") == "WEAP");
    CHECK(details.at("File") == "Override.esp");
    CHECK(details.at("Winner") == "Override.esp");
    CHECK(details.at("OverrideCount") == 1);
    CHECK(details.at("FormId") == "0x00000801");
    // Override records must resolve their base form to the origin definition.
    CHECK(details.at("BaseFormKey") == "000801:Base.esp");
    CHECK(details.at("BaseForm") == "BaseWeapon (Base.esp)");

    const auto matrix = parseResult(backend.invokeBackend(
        "GetRecordPluginMatrix", nlohmann::json::array({"000801:Base.esp"})));
    REQUIRE(matrix.size() == 2);
    CHECK(matrix[0].at("Plugin") == "Base.esp");
    CHECK(matrix[0].at("IsOverride") == false);
    CHECK(matrix[0].at("IsWinner") == false);
    CHECK(matrix[1].at("Plugin") == "Override.esp");
    CHECK(matrix[1].at("IsOverride") == true);
    CHECK(matrix[1].at("IsWinner") == true);
    CHECK(matrix[1].at("Changes").get<std::size_t>() >= 1);
}

TEST_CASE("Native containment path gives plugin type and record breadcrumbs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "Base.esp";
    const auto over = root / "Override.esp";
    writeBytes(base, makeBase());
    writeBytes(over, makeOverride());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, base, over);

    const auto path = parseResult(backend.invokeBackend(
        "GetContainmentPath", nlohmann::json::array({"000801:Base.esp"})));
    REQUIRE(path.size() == 3);
    CHECK(path[0].at("Kind") == "Plugin");
    CHECK(path[0].at("Label") == "Override.esp");
    CHECK(path[1].at("Kind") == "RecordType");
    CHECK(path[1].at("Label") == "WEAP");
    CHECK(path[2].at("Kind") == "Record");
    CHECK(path[2].at("Label") == "OverrideWeapon");
    CHECK(path[2].at("FormKey") == "000801:Base.esp");
}
