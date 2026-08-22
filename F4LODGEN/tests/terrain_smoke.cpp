#include "f4lod/terrain.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

void append_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void append_f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void append_subrecord(
    std::vector<unsigned char>& out,
    std::string_view type,
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::uint32_t write_flat_land(const std::filesystem::path& path, float offset) {
    std::vector<unsigned char> vhgt;
    append_f32(vhgt, offset);
    vhgt.resize(4 + f4lod::kLandscapeVertexCount, 0);

    std::vector<unsigned char> payload;
    append_subrecord(payload, "VHGT", vhgt);

    std::ofstream file(path, std::ios::binary);
    const std::vector<unsigned char> header(24, 0);
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    return static_cast<std::uint32_t>(payload.size());
}

f4lod::WinningRecord make_land(
    const std::filesystem::path& path,
    const f4lod::FormKey& land,
    const f4lod::FormKey& cell,
    std::uint32_t data_size) {
    f4lod::WinningRecord record;
    record.key = land;
    record.type = "LAND";
    record.source_plugin = land.plugin;
    record.source_path = path;
    record.data_size = data_size;
    record.cell = cell;
    return record;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-terrain-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const f4lod::FormKey world{"World.esp", 0x100U};
    const f4lod::FormKey west_cell{"World.esp", 0x200U};
    const f4lod::FormKey east_cell{"World.esp", 0x201U};
    const f4lod::FormKey west_land{"World.esp", 0x300U};
    const f4lod::FormKey east_land{"World.esp", 0x301U};

    const auto west_path = root / "west.bin";
    const auto east_path = root / "east.bin";
    const auto west_size = write_flat_land(west_path, 0.0F);
    auto east_size = write_flat_land(east_path, 0.0F);

    f4lod::RecordGraph graph;
    graph.winners.push_back(make_land(west_path, west_land, west_cell, west_size));
    graph.winners.push_back(make_land(east_path, east_land, east_cell, east_size));

    f4lod::WorldspaceCatalog catalog;
    f4lod::WorldspaceInfo world_info;
    world_info.key = world;
    world_info.editor_id = "World";
    world_info.default_land_height = 0.0F;
    world_info.default_water_height = 20000000.0F;
    catalog.worldspaces.push_back(world_info);
    catalog.exterior_cells.push_back({west_cell, world, 0, 0, true});
    catalog.exterior_cells.push_back({east_cell, world, 1, 0, true});

    auto terrain = f4lod::build_terrain_world(graph, catalog, world);
    assert(terrain.complete);
    assert(terrain.default_land_height == 0.0F);
    assert(terrain.default_water_height == 20000000.0F);
    assert(terrain.cells.size() == 2);
    assert(terrain.has_bounds);
    assert(terrain.min_x == 0);
    assert(terrain.max_x == 1);
    assert(terrain.min_y == 0);
    assert(terrain.max_y == 0);
    assert(terrain.seam_issues.empty());

    east_size = write_flat_land(east_path, 1.0F);
    graph.winners[1].data_size = east_size;
    terrain = f4lod::build_terrain_world(graph, catalog, world);
    assert(terrain.complete);
    assert(terrain.seam_issues.size() == 1);
    assert(terrain.seam_issues[0].axis == 'x');
    assert(terrain.seam_issues[0].mismatched_samples == f4lod::kLandscapeVertexSide);
    assert(terrain.seam_issues[0].max_error == 8.0F);

    std::filesystem::remove_all(root);
    return 0;
}
