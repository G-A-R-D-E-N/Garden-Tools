#include "f4lod/terrain_job.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

void write_lodsettings(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> data;
    u16(data, 0U);   // southwest x
    u16(data, 0U);   // southwest y
    u32(data, 32U);  // stride
    u32(data, 4U);   // min
    u32(data, 32U);  // max
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

f4lod::TerrainWorld make_world() {
    f4lod::TerrainWorld world;
    world.complete = true;
    world.worldspace = {"Fallout4.esm", 0x18AA2};
    world.has_bounds = true;
    world.min_x = 0;
    world.min_y = 0;
    world.max_x = 1;
    world.max_y = 0;

    for (std::int32_t x = 0; x <= 1; ++x) {
        f4lod::TerrainCell cell;
        cell.cell = {"Fallout4.esm", static_cast<std::uint32_t>(0x100 + x)};
        cell.land = {"Fallout4.esm", static_cast<std::uint32_t>(0x200 + x)};
        cell.x = x;
        cell.y = 0;
        cell.heightmap.valid = true;
        cell.heightmap.land = cell.land;
        cell.heightmap.heights.fill(64.0F);
        cell.heightmap.min_height = 64.0F;
        cell.heightmap.max_height = 64.0F;
        world.cells.push_back(std::move(cell));
    }
    return world;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-terrain-job-smoke";
    std::filesystem::remove_all(root);
    const auto data = root / "Data";
    write_lodsettings(data / "lodsettings" / "Commonwealth.lod");

    const f4lod::AssetResolver assets(data, {});
    const auto plan = f4lod::make_terrain_generation_plan(make_world(), "Commonwealth", assets);
    assert(plan.valid);
    assert(plan.worldspace_editor_id == "Commonwealth");
    assert(plan.lod_settings.valid);
    assert(plan.lod_settings_source.kind == f4lod::AssetSourceKind::loose);
    assert(plan.levels.size() == 4);
    assert(plan.levels[0].lod_level == 4);
    assert(plan.levels[1].lod_level == 8);
    assert(plan.levels[2].lod_level == 16);
    assert(plan.levels[3].lod_level == 32);
    assert(plan.total_block_count() == 4);
    for (const auto& level : plan.levels) {
        assert(level.blocks.size() == 1);
        assert(level.blocks[0].cells.size() == 2);
    }

    auto cracked = make_world();
    cracked.seam_issues.push_back({
        cracked.cells[0].cell,
        cracked.cells[1].cell,
        'x',
        33,
        32.0F,
    });
    const auto seam_rejected = f4lod::make_terrain_generation_plan(
        std::move(cracked), "Commonwealth", assets);
    assert(!seam_rejected.valid);

    const f4lod::AssetResolver missing_assets(root / "MissingData", {});
    const auto missing = f4lod::make_terrain_generation_plan(
        make_world(), "Commonwealth", missing_assets);
    assert(!missing.valid);
    assert(!missing.error.empty());

    std::filesystem::remove_all(root);
    return 0;
}
