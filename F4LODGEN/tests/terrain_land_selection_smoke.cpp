#include "f4lod/terrain.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}
void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}
void f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(out, bits);
}
void sig(std::vector<unsigned char>& out, std::string_view value) {
    assert(value.size() == 4U);
    out.insert(out.end(), value.begin(), value.end());
}

std::uint32_t write_flat_land(const std::filesystem::path& path, float vhgt_offset) {
    std::vector<unsigned char> vhgt;
    f32(vhgt, vhgt_offset);
    vhgt.resize(4U + f4lod::kLandscapeVertexCount, 0U);

    std::vector<unsigned char> payload;
    sig(payload, "VHGT");
    u16(payload, static_cast<std::uint16_t>(vhgt.size()));
    payload.insert(payload.end(), vhgt.begin(), vhgt.end());

    std::ofstream file(path, std::ios::binary);
    const std::vector<unsigned char> header(24U, 0U);
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    return static_cast<std::uint32_t>(payload.size());
}

f4lod::WinningRecord make_land(
    const std::filesystem::path& path,
    const f4lod::FormKey& land,
    const f4lod::FormKey& cell,
    std::uint32_t size,
    const char* source,
    std::size_t load_order) {
    f4lod::WinningRecord record;
    record.key = land;
    record.type = "LAND";
    record.source_plugin = source;
    record.source_path = path;
    record.source_load_order_index = load_order;
    record.data_size = size;
    record.cell = cell;
    return record;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-land-selection-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const f4lod::FormKey world{"Fallout4.esm", 0x100U};
    const f4lod::FormKey cell{"Fallout4.esm", 0x200U};
    const f4lod::FormKey land{"Fallout4.esm", 0x300U};

    const auto base_path = root / "base.bin";
    const auto override_path = root / "override.bin";
    const auto base_size = write_flat_land(base_path, 0.0F);
    const auto override_size = write_flat_land(override_path, 2.0F);

    const auto base = make_land(base_path, land, cell, base_size, "Fallout4.esm", 0U);
    const auto override_record = make_land(
        override_path, land, cell, override_size, "LandscapeMod.esp", 1U);

    f4lod::RecordGraph graph;
    graph.valid = true;
    graph.candidates_low_to_high = {base, override_record};
    graph.winners = {override_record};

    f4lod::WorldspaceCatalog catalog;
    catalog.complete = true;
    f4lod::WorldspaceInfo world_info;
    world_info.key = world;
    world_info.editor_id = "Commonwealth";
    world_info.default_land_height = 0.0F;
    world_info.default_water_height = 20000000.0F;
    catalog.worldspaces.push_back(world_info);

    f4lod::ExteriorCellInfo exterior;
    exterior.key = cell;
    exterior.worldspace = world;
    exterior.x = 0;
    exterior.y = 0;
    exterior.has_landscape = true;
    catalog.exterior_cells.push_back(exterior);

    const auto winning = f4lod::build_terrain_world(
        graph, catalog, world, 0.01F, f4lod::TerrainLandSelection::winning);
    assert(winning.complete);
    assert(winning.cells.size() == 1U);
    assert(winning.cells[0].land_source_plugin == "LandscapeMod.esp");
    assert(winning.cells[0].heightmap.at(0, 0) == 16.0F);

    const auto first = f4lod::build_terrain_world(
        graph, catalog, world, 0.01F, f4lod::TerrainLandSelection::first_candidate);
    assert(first.complete);
    assert(first.cells.size() == 1U);
    assert(first.cells[0].land_source_plugin == "Fallout4.esm");
    assert(first.cells[0].heightmap.at(0, 0) == 0.0F);

    assert(std::string(f4lod::terrain_land_selection_name(
        f4lod::TerrainLandSelection::winning)) == "winning");
    assert(std::string(f4lod::terrain_land_selection_name(
        f4lod::TerrainLandSelection::first_candidate)) == "first-candidate");

    std::filesystem::remove_all(root);
    return 0;
}
