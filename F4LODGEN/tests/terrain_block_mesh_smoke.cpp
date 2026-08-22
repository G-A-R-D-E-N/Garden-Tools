#include "f4lod/terrain_block_mesh.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace {

f4lod::TerrainCell make_cell(std::int32_t x, std::int32_t y, float height) {
    f4lod::TerrainCell cell;
    cell.cell = {"Fixture.esm", static_cast<std::uint32_t>(0x1000 + (y + 16) * 32 + (x + 16))};
    cell.land = {"Fixture.esm", static_cast<std::uint32_t>(0x2000 + (y + 16) * 32 + (x + 16))};
    cell.x = x;
    cell.y = y;
    cell.heightmap.valid = true;
    cell.heightmap.land = cell.land;
    cell.heightmap.min_height = height;
    cell.heightmap.max_height = height;
    cell.heightmap.heights.fill(height);
    return cell;
}

} // namespace

int main() {
    f4lod::TerrainWorld world;
    world.worldspace = {"Fallout4.esm", 0x18AA2};
    world.default_land_height = 128.0F;
    world.default_water_height = 20000000.0F;
    world.has_bounds = true;
    world.min_x = 0;
    world.min_y = 0;
    world.max_x = 3;
    world.max_y = 3;

    f4lod::TerrainBlockPlan plan;
    plan.lod_level = 4;
    plan.x = 0;
    plan.y = 0;

    for (std::int32_t y = 0; y < 4; ++y) {
        for (std::int32_t x = 0; x < 4; ++x) {
            const auto index = world.cells.size();
            world.cells.push_back(make_cell(x, y, 128.0F));
            plan.cells.push_back({index, x, y});
        }
    }

    const auto mesh = f4lod::build_terrain_block_source_mesh(world, plan);
    assert(mesh.valid);
    assert(mesh.vertices.size() == 129U * 129U);
    assert(mesh.indices.size() == 16U * 32U * 32U * 6U);
    assert(mesh.min_height == 128.0F);
    assert(mesh.max_height == 128.0F);

    const auto outer_count = static_cast<std::size_t>(
        std::count(mesh.outer_border.begin(), mesh.outer_border.end(), static_cast<unsigned char>(1)));
    const auto cell_border_count = static_cast<std::size_t>(
        std::count(mesh.cell_border.begin(), mesh.cell_border.end(), static_cast<unsigned char>(1)));
    assert(outer_count == 512U);
    assert(cell_border_count == 1265U);

    // Recovered LODGen behavior fills missing terrain-data CELLs from WRLD
    // LANDHEIGHT/WATERHEIGHT. A block with one real LAND cell must therefore
    // still become the same complete 129x129 LOD4 heightfield.
    f4lod::TerrainWorld partial_world;
    partial_world.worldspace = world.worldspace;
    partial_world.default_land_height = 128.0F;
    partial_world.default_water_height = 20000000.0F;
    partial_world.cells.push_back(make_cell(0, 0, 128.0F));

    f4lod::TerrainBlockPlan partial_plan;
    partial_plan.lod_level = 4U;
    partial_plan.x = 0;
    partial_plan.y = 0;
    partial_plan.cells.push_back({0U, 0, 0});

    const auto partial = f4lod::build_terrain_block_source_mesh(partial_world, partial_plan);
    assert(partial.valid);
    assert(partial.vertices.size() == 129U * 129U);
    assert(partial.indices.size() / 3U == 128U * 128U * 2U);
    assert(partial.min_height == 128.0F);
    assert(partial.max_height == 128.0F);

    // Adjacent real cells must still agree exactly on their shared LAND edge.
    auto broken_world = world;
    for (std::size_t y = 0; y < f4lod::kLandscapeVertexSide; ++y) {
        broken_world.cells[1].heightmap.heights[y * f4lod::kLandscapeVertexSide] += 32.0F;
    }
    const auto broken = f4lod::build_terrain_block_source_mesh(broken_world, plan);
    assert(!broken.valid);
    assert(!broken.error.empty());

    return 0;
}
