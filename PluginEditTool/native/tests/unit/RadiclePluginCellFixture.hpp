#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/TempRoot.hpp"
#include "plugin_bytes.hpp"

namespace cell_fixture {


inline std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_cell_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

inline void writeHeader(fo4::test::ByteWriter& out,
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

inline std::size_t beginGroupRaw(fo4::test::ByteWriter& out,
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

inline void endGroupRaw(fo4::test::ByteWriter& out, std::size_t offset) {
    out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

inline std::vector<std::byte> basePlugin() {
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

inline void writeInteriorCell(fo4::test::ByteWriter& out,
                       std::uint32_t cellId,
                       std::uint32_t referenceId,
                       std::uint32_t baseId,
                       std::string_view cellEditorId = {}) {
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto subblock = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);

    const auto cell = fo4::test::begin_record(out, "CELL", 0, cellId);
    if (!cellEditorId.empty()) {
        std::string text(cellEditorId);
        text.push_back('\0');
        fo4::test::write_subrecord(out, "EDID", std::string_view{text});
    }
    const std::vector<std::byte> cellData{std::byte{1}, std::byte{0}};
    fo4::test::write_subrecord(out, "DATA", cellData);
    fo4::test::end_record(out, cell);

    const auto children = beginGroupRaw(out, cellId, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(
        out, cellId, fo4::GroupType::cell_temporary_children);
    const auto reference = fo4::test::begin_record(out, "REFR", 0, referenceId);
    fo4::test::ByteWriter name;
    name.u32(baseId);
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
}

inline std::vector<std::byte> sourcePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    writeInteriorCell(out, 0x00000800u, 0x00000801u, 0x00000802u);
    return out.bytes;
}

inline std::vector<std::byte> movedPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    writeInteriorCell(out, 0x01000900u, 0x00000801u, 0x00000802u);
    return out.bytes;
}

inline std::vector<std::byte> decalCellPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    const auto top = fo4::test::begin_group(out, "TXST", 0);
    const auto decal = fo4::test::begin_record(out, "TXST", 0, 0x00000810u);
    fo4::test::ByteWriter bounds;
    bounds.u16(0);
    bounds.u16(0);
    bounds.u16(0);
    bounds.u16(128);
    bounds.u16(64);
    bounds.u16(0);
    fo4::test::write_subrecord(out, "OBND", bounds.bytes);
    std::string diffuse = "Textures\\Decal.dds";
    diffuse.push_back('\0');
    fo4::test::write_subrecord(out, "TX00", std::string_view{diffuse});
    fo4::test::end_record(out, decal);
    fo4::test::end_group(out, top);
    writeInteriorCell(out, 0x00000800u, 0x00000811u, 0x00000810u);
    return out.bytes;
}

inline void writeModelBase(fo4::test::ByteWriter& out,
                    std::string_view type,
                    std::uint32_t formId,
                    std::string_view subrecord,
                    std::string model) {
    const auto top = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, formId);
    if (!model.empty()) {
        model.push_back('\0');
        fo4::test::write_subrecord(out, subrecord, std::string_view{model});
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, top);
}

inline std::vector<std::byte> typedCellPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    writeModelBase(out, "STAT", 0x00000810u, "MODL", "Meshes\\StatFixture.nif");
    writeModelBase(out, "MSTT", 0x00000811u, "MODL", "Meshes\\MsttFixture.nif");
    writeModelBase(out, "FURN", 0x00000812u, "MODL", "Meshes\\FurnFixture.nif");
    writeModelBase(out, "ACTI", 0x00000813u, "MODL", "Meshes\\ActiFixture.nif");
    writeModelBase(out, "CONT", 0x00000814u, "MODL", "");
    writeModelBase(out, "SCOL", 0x00000815u, "MODL", "Meshes\\ScolFixture.nif");
    writeModelBase(out, "ARMO", 0x00000816u, "MOD2", "Armor\\ArmoFixture.nif");

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
    for (std::uint32_t base = 0x00000810u; base <= 0x00000816u; ++base) {
        const auto reference = fo4::test::begin_record(out, "REFR", 0, base + 0x10u);
        fo4::test::ByteWriter name;
        name.u32(base);
        fo4::test::write_subrecord(out, "NAME", name.bytes);
        fo4::test::ByteWriter transform;
        for (int index = 0; index < 6; ++index) {
            transform.f32(0.0f);
        }
        fo4::test::write_subrecord(out, "DATA", transform.bytes);
        fo4::test::end_record(out, reference);
    }
    const auto hazard = fo4::test::begin_record(out, "PHZD", 0, 0x00000830u);
    fo4::test::ByteWriter hazardTransform;
    for (int index = 0; index < 6; ++index) {
        hazardTransform.f32(0.0f);
    }
    fo4::test::write_subrecord(out, "DATA", hazardTransform.bytes);
    fo4::test::end_record(out, hazard);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

inline std::vector<std::byte> primitiveCellPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    writeModelBase(out, "STAT", 0x00000810u, "MODL", "Meshes\\Box.nif");

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
    const auto room = fo4::test::begin_record(out, "REFR", 0, 0x00000801u);
    fo4::test::ByteWriter roomName;
    roomName.u32(0x00000810u);
    fo4::test::write_subrecord(out, "NAME", roomName.bytes);
    fo4::test::ByteWriter roomTransform;
    for (int index = 0; index < 6; ++index) roomTransform.f32(0.0f);
    fo4::test::write_subrecord(out, "DATA", roomTransform.bytes);
    fo4::test::ByteWriter primitive;
    primitive.f32(512.0f);
    primitive.f32(288.0f);
    primitive.f32(192.0f);
    primitive.f32(0.0f);
    primitive.f32(0.5f);
    primitive.f32(1.0f);
    primitive.f32(0.4f);
    primitive.u32(1u);
    fo4::test::write_subrecord(out, "XPRM", primitive.bytes);
    fo4::test::end_record(out, room);

    const auto plain = fo4::test::begin_record(out, "REFR", 0, 0x00000802u);
    fo4::test::ByteWriter plainName;
    plainName.u32(0x00000810u);
    fo4::test::write_subrecord(out, "NAME", plainName.bytes);
    fo4::test::ByteWriter plainTransform;
    for (int index = 0; index < 6; ++index) plainTransform.f32(0.0f);
    fo4::test::write_subrecord(out, "DATA", plainTransform.bytes);
    fo4::test::end_record(out, plain);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

inline std::vector<std::byte> exteriorCellPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    writeModelBase(out, "STAT", 0x00000810u, "MODL", "Meshes\\StatFixture.nif");

    const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
    const auto worldspace = fo4::test::begin_record(out, "WRLD", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
    fo4::test::end_record(out, worldspace);
    const auto worldChildren = beginGroupRaw(out, 0x00000800u, fo4::GroupType::world_children);
    const auto block = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_block);
    const auto subblock = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_subblock);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000901u);
    fo4::test::ByteWriter cellFlags;
    cellFlags.u16(0x0002);
    fo4::test::write_subrecord(out, "DATA", cellFlags.bytes);
    fo4::test::ByteWriter grid;
    grid.i32(0);
    grid.i32(0);
    grid.u32(0);
    fo4::test::write_subrecord(out, "XCLC", grid.bytes);
    fo4::test::end_record(out, cell);
    const auto children = beginGroupRaw(out, 0x00000901u, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(out, 0x00000901u, fo4::GroupType::cell_temporary_children);
    const auto reference = fo4::test::begin_record(out, "REFR", 0, 0x00000902u);
    fo4::test::ByteWriter name;
    name.u32(0x00000810u);
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
    endGroupRaw(out, worldChildren);
    fo4::test::end_group(out, wrld);
    return out.bytes;
}

// Constraint: VHGT is a float offset, 1089 chained int8 deltas of 8 world units each and 3 trailing bytes.
inline std::vector<std::byte> vhgtBytes(float offset) {
    std::vector<std::byte> bytes(4 + 1089 + 3, std::byte{0});
    std::memcpy(bytes.data(), &offset, sizeof(offset));
    return bytes;
}

inline void writeExteriorCell(fo4::test::ByteWriter& out, std::uint32_t cellId, std::int32_t gridX,
                              std::int32_t gridY, std::uint16_t recordFlags = 0,
                              std::optional<float> waterHeight = std::nullopt) {
    const auto cell = fo4::test::begin_record(out, "CELL", recordFlags, cellId);
    fo4::test::ByteWriter cellFlags;
    cellFlags.u16(0x0002);
    fo4::test::write_subrecord(out, "DATA", cellFlags.bytes);
    fo4::test::ByteWriter grid;
    grid.i32(gridX);
    grid.i32(gridY);
    grid.u32(0);
    fo4::test::write_subrecord(out, "XCLC", grid.bytes);
    if (waterHeight) {
        fo4::test::ByteWriter water;
        water.f32(*waterHeight);
        fo4::test::write_subrecord(out, "XCLW", water.bytes);
    }
    fo4::test::end_record(out, cell);
}

inline void writeLandChild(fo4::test::ByteWriter& out, std::uint32_t cellId, std::uint32_t landId,
                           const std::vector<std::byte>& vhgt) {
    const auto children = beginGroupRaw(out, cellId, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(out, cellId, fo4::GroupType::cell_temporary_children);
    const auto land = fo4::test::begin_record(out, "LAND", 0, landId);
    fo4::test::write_subrecord(out, "VHGT", vhgt);
    fo4::test::end_record(out, land);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
}

inline std::vector<std::byte> exteriorCellWithLandPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
    const auto worldspace = fo4::test::begin_record(out, "WRLD", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
    fo4::test::end_record(out, worldspace);
    const auto worldChildren = beginGroupRaw(out, 0x00000800u, fo4::GroupType::world_children);
    const auto block = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_block);
    const auto subblock = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_subblock);

    writeExteriorCell(out, 0x00000901u, 0, 0);
    writeLandChild(out, 0x00000901u, 0x00000980u, vhgtBytes(10.0f));

    writeExteriorCell(out, 0x00000903u, 1, 0);

    writeExteriorCell(out, 0x00000905u, 0, 1);
    writeLandChild(out, 0x00000905u, 0x00000981u, std::vector<std::byte>(12, std::byte{0}));

    writeExteriorCell(out, 0x00000904u, 0, 2, 0x0020u);

    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    endGroupRaw(out, worldChildren);
    fo4::test::end_group(out, wrld);
    return out.bytes;
}

inline std::vector<std::byte> exteriorCellWithNoWaterHeightPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
    const auto worldspace = fo4::test::begin_record(out, "WRLD", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
    fo4::test::end_record(out, worldspace);
    const auto worldChildren = beginGroupRaw(out, 0x00000800u, fo4::GroupType::world_children);
    const auto block = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_block);
    const auto subblock = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_subblock);

    writeExteriorCell(out, 0x00000906u, 2, 2, 0, std::numeric_limits<float>::max());
    writeExteriorCell(out, 0x00000907u, 2, 3, 0, 12.5f);

    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    endGroupRaw(out, worldChildren);
    fo4::test::end_group(out, wrld);
    return out.bytes;
}

inline std::vector<std::byte> patchPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Other.esm"});
    return out.bytes;
}

inline nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

inline nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}



inline std::vector<std::byte> navmeshGeometry() {
    fo4::test::ByteWriter geometry;
    geometry.u32(15);
    geometry.u32(0x12345678u);
    geometry.u32(0);
    geometry.u32(0x00000810u);
    geometry.u32(3);
    for (int index = 0; index < 9; ++index) {
        geometry.f32(static_cast<float>(index));
    }
    geometry.u32(1);
    geometry.u16(0);
    geometry.u16(1);
    geometry.u16(2);
    geometry.u16(0xFFFFu);
    geometry.u16(0xFFFFu);
    geometry.u16(0xFFFFu);
    geometry.f32(4.5f);
    geometry.u8(0xAA);
    geometry.u32(0);
    geometry.u32(0);
    geometry.u32(1);
    geometry.u16(0);
    geometry.u32(0x000ABCDEu);
    geometry.u32(0x00000811u);
    for (int index = 0; index < 12; ++index) {
        geometry.u32(0);
    }
    return geometry.bytes;
}

inline std::vector<std::byte> navmeshPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

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
    const auto navmesh = fo4::test::begin_record(out, "NAVM", 0, 0x00000801u);
    const auto geometry = navmeshGeometry();
    fo4::test::write_subrecord(out, "NVNM", geometry);
    fo4::test::end_record(out, navmesh);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

inline void loadNavmeshPlugin(fo4ide::plugin::RadiclePluginBackend& backend,
                              const std::filesystem::path& path) {
    writeBytes(path, navmeshPlugin());
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", path)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

inline nlohmann::json transformChange() {
    return {{"transform", {{"x", 100.0}, {"y", 200.0}, {"z", 300.0}, {"rx", 1.5}, {"ry", 2.5}, {"rz", 3.5}}}};
}

inline nlohmann::json patchEnvironment(const std::filesystem::path& root, bool withSource = true) {
    auto plugins = nlohmann::json::array({pluginEntry("Base.esm", root / "Base.esm")});
    if (withSource) plugins.push_back(pluginEntry("Source.esp", root / "Source.esp"));
    plugins.push_back(pluginEntry("Patch.esp", root / "Patch.esp"));
    return nlohmann::json{{"plugins", std::move(plugins)}};
}


}  // namespace cell_fixture
