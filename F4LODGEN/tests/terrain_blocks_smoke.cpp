#include "f4lod/terrain_blocks.hpp"

#include <cassert>

namespace {

f4lod::TerrainCell make_cell(std::int32_t x, std::int32_t y, std::uint32_t id) {
    f4lod::TerrainCell cell;
    cell.cell = {"World.esp", id};
    cell.land = {"World.esp", id + 0x100U};
    cell.x = x;
    cell.y = y;
    cell.heightmap.valid = true;
    return cell;
}

} // namespace

int main() {
    f4lod::TerrainWorld world;
    world.worldspace = {"Fallout4.esm", 0x18AA2U};
    world.cells.push_back(make_cell(-64, -64, 0x200U));
    world.cells.push_back(make_cell(-61, -64, 0x201U));
    world.cells.push_back(make_cell(-60, -64, 0x202U));
    world.cells.push_back(make_cell(-57, -60, 0x203U));
    world.cells.push_back(make_cell(-56, -60, 0x204U));

    const auto level4 = f4lod::plan_terrain_blocks(world, {-64, -64}, 4);
    assert(level4.valid);
    assert(level4.blocks.size() == 3);
    assert(level4.blocks[0].x == -64);
    assert(level4.blocks[0].y == -64);
    assert(level4.blocks[0].cells.size() == 2);
    assert(level4.blocks[0].cells[0].local_x == 0);
    assert(level4.blocks[0].cells[1].local_x == 3);
    assert(level4.blocks[1].x == -60);
    assert(level4.blocks[1].y == -64);
    assert(level4.blocks[2].x == -60);
    assert(level4.blocks[2].y == -60);
    assert(f4lod::terrain_btr_relative_path("Commonwealth", level4.blocks[0]) ==
           "meshes/terrain/Commonwealth/Commonwealth.4.-64.-64.btr");

    const auto level8 = f4lod::plan_terrain_blocks(world, {-64, -64}, 8);
    assert(level8.valid);
    assert(level8.blocks.size() == 2);
    assert(level8.blocks[0].x == -64);
    assert(level8.blocks[0].y == -64);
    assert(level8.blocks[0].cells.size() == 4);
    assert(level8.blocks[1].x == -56);
    assert(level8.blocks[1].y == -64);

    const auto invalid_level = f4lod::plan_terrain_blocks(world, {-64, -64}, 2);
    assert(!invalid_level.valid);

    const auto bad_anchor = f4lod::plan_terrain_blocks(world, {-63, -64}, 4);
    assert(!bad_anchor.valid);

    f4lod::TerrainBlockPlan block;
    block.lod_level = 4;
    block.x = 0;
    block.y = 0;
    assert(f4lod::terrain_btr_relative_path("../bad:name", block) ==
           "meshes/terrain/..badname/..badname.4.0.0.btr");

    return 0;
}
