#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/cell.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_exterior_cell_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> worldspacePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WRLD", 0);
    const auto world = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
    fo4::test::end_record(out, world);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ExteriorCellTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("PluginEditTool stores a CELL UTF-8 EditorID as Windows-1252") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"worldspace", "000800:Patch.esp"},
        {"x", -33},
        {"y", 32},
        {"editorId", "Caf\xC3\xA9"}
    };
    REQUIRE(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "CELL", payload})).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(record != nullptr);
    const auto edid = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& sub) { return sub.signature == fo4::sig_edid; });
    REQUIRE(edid != record->subrecords.end());
    CHECK(edid->data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}});
}

TEST_CASE("PluginEditTool creates a new exterior CELL in the WRLD hierarchy") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"worldspace", "000800:Patch.esp"},
        {"x", -33},
        {"y", 32},
        {"editorId", "CreatedExterior"}
    };
    const auto created = parseStringResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "CELL", payload})));
    CHECK(created.at("signature") == "CELL");
    CHECK(created.at("formKey") == "000900:Patch.esp");
    CHECK(backend.getPlacedReferencesAtGrid(
        nlohmann::json::array({"000800:Patch.esp", -33, 32})).has_value());
    const auto wrapped = backend.getPlacedReferencesAtGrid(
        nlohmann::json::array({"000800:Patch.esp", 4294967263u, 32}));
    REQUIRE_FALSE(wrapped.has_value());
    CHECK(wrapped.error().type == "CellGridArgumentsMissing");
    CHECK_FALSE(backend.getPlacedReferencesAtGrid(
        nlohmann::json::array({"000800:Patch.esp", -33, -2147483649LL})).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.next_form_id == 0x901u);
    CHECK(disk.header.record_count == 6u);
    const auto world = fo4::FormKey::parse("000800:Patch.esp");
    const auto* cell = fo4::forms::find_exterior_cell(disk, world, -33, 32);
    REQUIRE(cell != nullptr);
    CHECK(cell->form_key == fo4::FormKey::parse("000900:Patch.esp"));
}

TEST_CASE("PluginEditTool refuses an occupied exterior CELL coordinate transactionally") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const nlohmann::json payload{
        {"worldspace", "000800:Patch.esp"},
        {"x", 1},
        {"y", 2}
    };
    REQUIRE(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "CELL", payload})).has_value());
    const auto before = readBytes(path);

    const auto duplicate = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "CELL", payload}));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().type == "CellCreateFailed");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool rejects invalid exterior CELL payloads before writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto missingWorld = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "CELL",
            nlohmann::json{{"worldspace", "000801:Patch.esp"}, {"x", 0}, {"y", 0}}
        }));
    REQUIRE_FALSE(missingWorld.has_value());
    CHECK(missingWorld.error().type == "CellCreateFailed");

    const auto outOfBounds = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "CELL",
            nlohmann::json{{"worldspace", "000800:Patch.esp"}, {"x", 111}, {"y", 0}}
        }));
    REQUIRE_FALSE(outOfBounds.has_value());
    CHECK(outOfBounds.error().type == "CellCreateFailed");

    const auto wrapped = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "CELL",
            nlohmann::json{{"worldspace", "000800:Patch.esp"}, {"x", 18446744073709551583ULL}, {"y", 32}}
        }));
    REQUIRE_FALSE(wrapped.has_value());
    CHECK(wrapped.error().type == "InvalidRecordCreationPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool places a REFR in a CELL the plugin owns") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto cellResult = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "CELL",
            nlohmann::json{{"worldspace", "000800:Patch.esp"}, {"x", -33}, {"y", 32}}
        }));
    const auto cell = parseStringResult(cellResult)["formKey"].get<std::string>();
    const auto baseResult = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EYES",
            nlohmann::json{{"editorId", "PlacedBase"}, {"fullText", "Base"}, {"icon", "Textures\\Eyes\\base.dds"}}
        }));
    const auto base = parseStringResult(baseResult)["formKey"].get<std::string>();

    const auto placed = backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, 10.0, 20.0, 30.0, 0.25, -0.5, 1.75, false
    }));
    REQUIRE(placed.has_value());
    const auto placedKey = parseStringResult(placed)["formKey"].get<std::string>();
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto refs = fo4::forms::placed_references_for_cell(disk, fo4::FormKey::parse(cell));
    REQUIRE(refs.size() == 1u);
    CHECK(refs[0].record->form_key == fo4::FormKey::parse(placedKey));
    CHECK_FALSE(refs[0].persistent);
    CHECK(refs[0].base == fo4::FormKey::parse(base));
    CHECK(refs[0].transform.x == 10.0f);
    CHECK(refs[0].transform.y == 20.0f);
    CHECK(refs[0].transform.z == 30.0f);
    CHECK(refs[0].transform.rx == 0.25f);
    CHECK(refs[0].transform.ry == -0.5f);
    CHECK(refs[0].transform.rz == 1.75f);

    const auto foreign = backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", base, 0.0, 0.0, 0.0
    }));
    REQUIRE_FALSE(foreign.has_value());
    CHECK(foreign.error().type == "PlacedReferenceCreateFailed");

    const auto missing = backend.createPlacedObject(nlohmann::json::array({"Patch.esp", cell, base}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PlacedObjectArgumentsMissing");
}
