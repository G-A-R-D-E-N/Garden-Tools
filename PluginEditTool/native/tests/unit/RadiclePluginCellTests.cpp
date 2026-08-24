#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_cell_" + std::to_string(id));
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

void writeHeader(fo4::test::ByteWriter& out,
                 std::initializer_list<std::string_view> masters) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        name.reserve(master.size() + 1);
        for (const char ch : master) {
            name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::size_t beginGroupRaw(fo4::test::ByteWriter& out,
                          std::uint32_t label,
                          fo4::GroupType type) {
    const auto offset = out.size();
    out.sig("GRUP");
    out.u32(0);
    out.u32(label);
    out.i32(static_cast<std::int32_t>(type));
    out.u16(0);
    out.u16(0);
    out.u16(0);
    out.u16(0);
    return offset;
}

void endGroupRaw(fo4::test::ByteWriter& out, std::size_t offset) {
    out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

std::vector<std::byte> basePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto statTop = fo4::test::begin_group(out, "STAT", 0);
    const auto firstStat = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PartA\0", 6});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\PartA.nif\0", 17});
    fo4::test::end_record(out, firstStat);
    const auto secondStat = fo4::test::begin_record(out, "STAT", 0, 0x00000811u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PartB\0", 6});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\PartB.nif\0", 17});
    fo4::test::end_record(out, secondStat);
    fo4::test::end_group(out, statTop);

    const auto scolTop = fo4::test::begin_group(out, "SCOL", 0);
    const auto scol = fo4::test::begin_record(out, "SCOL", 0, 0x00000802u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Collection\0", 11});
    fo4::test::ByteWriter partA;
    partA.u32(0x00000810u);
    fo4::test::write_subrecord(out, "ONAM", partA.bytes);
    fo4::test::ByteWriter placementsA;
    for (int field = 0; field < 7; ++field) {
        placementsA.f32(1.0f + static_cast<float>(field));
    }
    fo4::test::write_subrecord(out, "DATA", placementsA.bytes);
    fo4::test::ByteWriter partB;
    partB.u32(0x00000811u);
    fo4::test::write_subrecord(out, "ONAM", partB.bytes);
    fo4::test::ByteWriter placementsB;
    for (int field = 0; field < 7; ++field) {
        placementsB.f32(10.0f + static_cast<float>(field));
    }
    fo4::test::write_subrecord(out, "DATA", placementsB.bytes);
    fo4::test::end_record(out, scol);
    fo4::test::end_group(out, scolTop);
    return out.bytes;
}

std::vector<std::byte> sourcePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});

    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto subblock = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);

    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800u);
    const std::vector<std::byte> cellData{std::byte{1}, std::byte{0}};
    fo4::test::write_subrecord(out, "DATA", cellData);
    fo4::test::end_record(out, cell);

    const auto children = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(
        out, 0x00000800u, fo4::GroupType::cell_temporary_children);
    const auto reference = fo4::test::begin_record(out, "REFR", 0, 0x00000801u);
    fo4::test::ByteWriter name;
    name.u32(0x00000802u);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::ByteWriter transform;
    for (int index = 0; index < 6; ++index) {
        transform.f32(10.0f + static_cast<float>(index));
    }
    fo4::test::write_subrecord(out, "DATA", transform.bytes);
    fo4::test::end_record(out, reference);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> patchPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Other.esm"});
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

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}  // namespace

TEST_CASE("Native cell transform creates a nested override before moving the reference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto sourcePath = root / "Source.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, basePlugin());
    writeBytes(sourcePath, sourcePlugin());
    writeBytes(patchPath, patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath),
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("Patch.esp", patchPath)
        })}
    };
    auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 3);

    const auto beforeMove = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    REQUIRE(beforeMove.at("references").size() == 1);
    const auto& renderRef = beforeMove.at("references")[0];
    CHECK(renderRef.at("baseType") == "StaticCollection");
    REQUIRE(renderRef.at("scolParts").is_array());
    REQUIRE(renderRef.at("scolParts").size() == 2);
    CHECK(renderRef.at("scolParts")[0].at("modelPath") == "Meshes\\PartA.nif");
    CHECK(renderRef.at("scolParts")[0].at("placements")[0].at("x") == 1.0f);
    CHECK(renderRef.at("scolParts")[0].at("placements")[0].at("scale") == 7.0f);
    CHECK(renderRef.at("scolParts")[1].at("modelPath") == "Meshes\\PartB.nif");
    CHECK(renderRef.at("scolParts")[1].at("placements")[0].at("x") == 10.0f);

    const auto moved = backend.setPlacedReferenceTransform(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp",
        100.0, 200.0, 300.0, 1.5, 2.5, 3.5
    }));
    REQUIRE(moved.has_value());
    REQUIRE(moved->is_string());
    CHECK(moved->get<std::string>().find("Created the nested override from Source.esp") !=
          std::string::npos);

    const auto reread = fo4::read_plugin_file(patchPath);
    REQUIRE(reread.header.masters.size() == 2);
    CHECK(reread.header.masters[0].key.filename() == "Other.esm");
    CHECK(reread.header.masters[1].key.filename() == "Base.esm");

    const auto cellKey = fo4::FormKey::parse("000800:Base.esm");
    const auto referenceKey = fo4::FormKey::parse("000801:Base.esm");
    REQUIRE(fo4::find_record(reread, cellKey) != nullptr);
    REQUIRE(fo4::find_record(reread, referenceKey) != nullptr);

    const auto refs = fo4::forms::placed_references_for_cell(reread, cellKey);
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].record->form_key == referenceKey);
    CHECK(refs[0].transform.x == 100.0f);
    CHECK(refs[0].transform.y == 200.0f);
    CHECK(refs[0].transform.z == 300.0f);
    CHECK(refs[0].transform.rx == 1.5f);
    CHECK(refs[0].transform.ry == 2.5f);
    CHECK(refs[0].transform.rz == 3.5f);

    const auto view = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    REQUIRE(view.at("references").size() == 1);
    CHECK(view.at("references")[0].at("formKey") == "000801:Base.esm");
    CHECK(view.at("references")[0].at("position").at("x") == 100.0f);
}
