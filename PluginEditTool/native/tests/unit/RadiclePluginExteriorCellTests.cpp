#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/cell.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_exterior_cell_" + id);
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

bool pendingPlugin(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

std::string placedBaseKey(fo4ide::plugin::RadiclePluginBackend& backend) {
    return parseStringResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EYES",
            nlohmann::json{{"editorId", "PlacedBase"}, {"fullText", "Base"}, {"icon", "Textures\\Eyes\\base.dds"}}
        })))["formKey"].get<std::string>();
}

std::pair<std::string, std::string> interiorCellAndBase(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto cell = parseStringResult(backend.createInteriorCell(
        nlohmann::json::array({"Patch.esp", "Kitchen"})))["formKey"].get<std::string>();
    return {cell, placedBaseKey(backend)};
}

std::string lowerEditorId(const fo4::Record& record) {
    std::string text;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid) continue;
        for (const auto byte : subrecord.data) {
            if (byte == std::byte{0}) break;
            text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(byte))));
        }
    }
    return text;
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

TEST_CASE("Native resolveWorldspace names the worldspace that owns an exterior CELL") {
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
    REQUIRE(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "CELL", payload})).has_value());

    const auto resolved = parseStringResult(backend.resolveWorldspace(
        nlohmann::json::array({"000900:Patch.esp"})));
    CHECK(resolved.at("cellFormKey") == "000900:Patch.esp");
    CHECK(resolved.at("worldspaceFormKey") == "000800:Patch.esp");

    const auto missing = parseStringResult(backend.resolveWorldspace(
        nlohmann::json::array({"000999:Patch.esp"})));
    CHECK(missing.at("error") == "Cell not found: 000999:Patch.esp");
}

TEST_CASE("Native resolveWorldspaceId maps a worldspace EditorID or FormKey to its FormKey") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto byEditorId = parseStringResult(backend.resolveWorldspaceId(
        nlohmann::json::array({"TestWorld"})));
    CHECK(byEditorId.at("worldspaceFormKey") == "000800:Patch.esp");

    const auto byFormKey = parseStringResult(backend.resolveWorldspaceId(
        nlohmann::json::array({"000800:Patch.esp"})));
    CHECK(byFormKey.at("worldspaceFormKey") == "000800:Patch.esp");

    const auto missing = parseStringResult(backend.resolveWorldspaceId(
        nlohmann::json::array({"NoSuchWorld"})));
    CHECK(missing.at("error") == "Worldspace not found: NoSuchWorld");
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
        "Patch.esp", cell, base, -135068.0, 131172.0, 30.0, 0.25, -0.5, 1.75, false
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
    CHECK(refs[0].transform.x == -135068.0f);
    CHECK(refs[0].transform.y == 131172.0f);
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

TEST_CASE("PluginEditTool places a persistent initially disabled REFR in a new interior CELL through the RPC target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::app::PluginBackendTarget target(backend, "backend");

    const auto cell = parseStringResult(target.invoke(
        "CreateInteriorCell",
        nlohmann::json::array({"Patch.esp", "Kitchen"})))["formKey"].get<std::string>();
    const auto base = parseStringResult(target.invoke(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EYES",
            nlohmann::json{{"editorId", "PlacedBase"}, {"fullText", "Base"}, {"icon", "Textures\\Eyes\\base.dds"}}
        })))["formKey"].get<std::string>();
    const auto placed = target.invoke(
        "CreatePlacedObject",
        nlohmann::json::array({"Patch.esp", cell, base, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, true, true}));
    REQUIRE(placed.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto refs = fo4::forms::placed_references_for_cell(disk, fo4::FormKey::parse(cell));
    REQUIRE(refs.size() == 1u);
    CHECK(refs[0].persistent);
    CHECK(refs[0].record->header.flags.test(fo4::RecordFlags::persistent));
    CHECK(refs[0].record->header.flags.test(fo4::RecordFlags::initially_disabled));
}

TEST_CASE("PluginEditTool createPlacedObject defaults to a persistent enabled reference with zero rotation") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto [cell, base] = interiorCellAndBase(backend);

    const auto first = parseStringResult(backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, 1.0, 2.0, 3.0})))["formKey"].get<std::string>();
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto refs = fo4::forms::placed_references_for_cell(disk, fo4::FormKey::parse(cell));
    REQUIRE(refs.size() == 1u);
    CHECK(refs[0].record->form_key == fo4::FormKey::parse(first));
    CHECK(refs[0].persistent);
    CHECK(refs[0].record->header.flags.test(fo4::RecordFlags::persistent));
    CHECK_FALSE(refs[0].record->header.flags.test(fo4::RecordFlags::initially_disabled));
    CHECK(refs[0].transform.x == 1.0f);
    CHECK(refs[0].transform.y == 2.0f);
    CHECK(refs[0].transform.z == 3.0f);
    CHECK(refs[0].transform.rx == 0.0f);
    CHECK(refs[0].transform.ry == 0.0f);
    CHECK(refs[0].transform.rz == 0.0f);

    const auto second = parseStringResult(backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, 4.0, 5.0, 6.0, 0.0, 0.0, 0.0, false, nullptr})))["formKey"].get<std::string>();
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(path);
    const auto twoRefs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse(cell));
    REQUIRE(twoRefs.size() == 2u);
    const auto secondKey = fo4::FormKey::parse(second);
    const auto found = std::find_if(twoRefs.begin(), twoRefs.end(), [&](const auto& ref) {
        return ref.record->form_key == secondKey;
    });
    REQUIRE(found != twoRefs.end());
    CHECK_FALSE(found->persistent);
    CHECK_FALSE(found->record->header.flags.test(fo4::RecordFlags::persistent));
    CHECK_FALSE(found->record->header.flags.test(fo4::RecordFlags::initially_disabled));
}

TEST_CASE("PluginEditTool createPlacedObject refuses malformed arguments without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto [cell, base] = interiorCellAndBase(backend);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = readBytes(path);

    const std::vector<std::pair<nlohmann::json, std::string>> refusals{
        {nlohmann::json::array({"Patch.esp", "zz", base, 1.0, 2.0, 3.0}), "InvalidFormKey"},
        {nlohmann::json::array({"Patch.esp", cell, "zz", 1.0, 2.0, 3.0}), "InvalidFormKey"},
        {nlohmann::json::array({"Patch.esp", cell, base, "1", 2.0, 3.0}), "PlacedObjectArgumentsMissing"},
        {nlohmann::json::array({"Patch.esp", cell, base, 1.0, nullptr, 3.0}), "PlacedObjectArgumentsMissing"},
        {nlohmann::json::array({"Patch.esp", cell, base, 1.0, 2.0}), "PlacedObjectArgumentsMissing"},
        {nlohmann::json::array({"", "", "", 1.0, 2.0, 3.0}), "PlacedObjectArgumentsMissing"},
        {nlohmann::json::array({"Nope.esp", cell, base, 1.0, 2.0, 3.0}), "PluginNotLoaded"},
        {nlohmann::json::array({"Patch.esp", cell, "000899:Patch.esp", 1.0, 2.0, 3.0}), "PlacedReferenceCreateFailed"},
        {nlohmann::json::array({"Patch.esp", cell, "000800:Other.esm", 1.0, 2.0, 3.0}), "PlacedReferenceCreateFailed"},
        {nlohmann::json::array({"Patch.esp", base, base, 1.0, 2.0, 3.0}), "PlacedReferenceCreateFailed"},
        {nlohmann::json::array({"Patch.esp", cell, base, 1e39, 2.0, 3.0}), "PlacedReferenceCreateFailed"}
    };
    for (const auto& [arguments, type] : refusals) {
        INFO(type);
        const auto refused = backend.createPlacedObject(arguments);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == type);
    }
    CHECK_FALSE(pendingPlugin(backend, 0));
    const bool unchanged = readBytes(path) == before;
    CHECK(unchanged);
}

TEST_CASE("PluginEditTool createInteriorCell refuses empty, duplicate and NUL editor IDs without staging a second cell") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const std::vector<std::pair<nlohmann::json, std::string>> missing{
        {nlohmann::json::array({"Patch.esp", ""}), "InteriorCellArgumentsMissing"},
        {nlohmann::json::array({"", "Kitchen"}), "InteriorCellArgumentsMissing"},
        {nlohmann::json::array({"Nope.esp", "Kitchen"}), "PluginNotLoaded"}
    };
    for (const auto& [arguments, type] : missing) {
        INFO(type);
        const auto refused = backend.createInteriorCell(arguments);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == type);
    }

    REQUIRE(backend.createInteriorCell(nlohmann::json::array({"Patch.esp", "Kitchen"})).has_value());
    const std::vector<std::pair<nlohmann::json, std::string>> duplicates{
        {nlohmann::json::array({"Patch.esp", "Kitchen"}), "InteriorCellCreateFailed"},
        {nlohmann::json::array({"Patch.esp", "KITCHEN"}), "InteriorCellCreateFailed"},
        {nlohmann::json::array({"Patch.esp", std::string("Bad\0Id", 6)}), "InteriorCellCreateFailed"}
    };
    for (const auto& [arguments, type] : duplicates) {
        INFO(type);
        const auto refused = backend.createInteriorCell(arguments);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == type);
    }

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto records = fo4::collect_records(disk);
    const auto kitchens = std::count_if(records.begin(), records.end(), [](const fo4::Record* record) {
        return record->header.signature == fo4::Signature::from_string("CELL") &&
               lowerEditorId(*record) == "kitchen";
    });
    CHECK(kitchens == 1);
}

TEST_CASE("PluginEditTool createPlacedObject refuses a persistent reference and a position outside its exterior CELL grid") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(path, worldspacePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto cell = parseStringResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "CELL",
            nlohmann::json{{"worldspace", "000800:Patch.esp"}, {"x", -33}, {"y", 32}}
        })))["formKey"].get<std::string>();
    const auto base = placedBaseKey(backend);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto persistent = backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, -135068.0, 131172.0, 30.0, 0.0, 0.0, 0.0, true
    }));
    REQUIRE_FALSE(persistent.has_value());
    CHECK(persistent.error().type == "PlacedReferenceCreateFailed");

    const auto outside = backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, 0.0, 0.0, 0.0
    }));
    REQUIRE_FALSE(outside.has_value());
    CHECK(outside.error().type == "PlacedReferenceCreateFailed");
    CHECK_FALSE(pendingPlugin(backend, 0));

    REQUIRE(backend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", cell, base, -135068.0, 131172.0, 30.0, 0.25, -0.5, 1.75, false
    })).has_value());
    CHECK(pendingPlugin(backend, 0));
}

TEST_CASE("PluginEditTool placement and interior cell creation are refused on a protected plugin and when the file changed on disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto protectedPath = root / "protected" / "Patch.esp";
    writeBytes(protectedPath, worldspacePlugin());
    const auto protectedBefore = readBytes(protectedPath);
    fo4ide::plugin::RadiclePluginBackend protectedBackend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            nlohmann::json{
                {"name", "Patch.esp"},
                {"path", protectedPath.string()},
                {"source", "data"},
                {"resolved", true}
            }
        })}
    };
    REQUIRE(protectedBackend.invoke("environment.LoadMo2", environment).has_value());
    const auto protectedCell = protectedBackend.createInteriorCell(
        nlohmann::json::array({"Patch.esp", "Kitchen"}));
    REQUIRE_FALSE(protectedCell.has_value());
    CHECK(protectedCell.error().type == "PluginReadOnly");
    const auto protectedPlaced = protectedBackend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", "000801:Patch.esp", "000802:Patch.esp", 0.0, 0.0, 0.0
    }));
    REQUIRE_FALSE(protectedPlaced.has_value());
    CHECK(protectedPlaced.error().type == "PluginReadOnly");
    CHECK_FALSE(pendingPlugin(protectedBackend, 0));
    const bool protectedUnchanged = readBytes(protectedPath) == protectedBefore;
    CHECK(protectedUnchanged);

    const auto changedPath = root / "changed" / "Patch.esp";
    writeBytes(changedPath, worldspacePlugin());
    fo4ide::plugin::RadiclePluginBackend changedBackend;
    load(changedBackend, changedPath);
    const auto loadedTime = std::filesystem::last_write_time(changedPath);
    writeBytes(changedPath, worldspacePlugin());
    std::filesystem::last_write_time(changedPath, loadedTime + std::chrono::seconds(2));
    const auto changedBefore = readBytes(changedPath);

    const auto changedCell = changedBackend.createInteriorCell(
        nlohmann::json::array({"Patch.esp", "Kitchen"}));
    REQUIRE_FALSE(changedCell.has_value());
    CHECK(changedCell.error().type == "PluginChangedOnDisk");
    const auto changedPlaced = changedBackend.createPlacedObject(nlohmann::json::array({
        "Patch.esp", "000801:Patch.esp", "000802:Patch.esp", 0.0, 0.0, 0.0
    }));
    REQUIRE_FALSE(changedPlaced.has_value());
    CHECK(changedPlaced.error().type == "PluginChangedOnDisk");
    CHECK_FALSE(pendingPlugin(changedBackend, 0));
    const bool changedUnchanged = readBytes(changedPath) == changedBefore;
    CHECK(changedUnchanged);
}
