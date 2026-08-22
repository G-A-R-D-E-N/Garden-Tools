#include "f4lod/terrain_mesh.hpp"

#include <cassert>
#include <cstddef>

int main() {
    f4lod::TerrainCell cell;
    cell.cell = {"World.esp", 0x200U};
    cell.land = {"World.esp", 0x300U};
    cell.x = -1;
    cell.y = 2;
    cell.heightmap.valid = true;

    for (std::size_t y = 0; y < f4lod::kLandscapeVertexSide; ++y) {
        for (std::size_t x = 0; x < f4lod::kLandscapeVertexSide; ++x) {
            cell.heightmap.heights[y * f4lod::kLandscapeVertexSide + x] =
                static_cast<float>(x * 8 + y * 16);
        }
    }

    const auto full = f4lod::build_terrain_cell_mesh(cell, 1);
    assert(full.valid);
    assert(full.vertices_per_side == 33);
    assert(full.vertices.size() == 1089);
    assert(full.indices.size() == 32 * 32 * 6);
    assert(full.vertices.front().x == -4096.0F);
    assert(full.vertices.front().y == 8192.0F);
    assert(full.vertices.front().z == 0.0F);
    assert(full.vertices.back().x == 0.0F);
    assert(full.vertices.back().y == 12288.0F);
    assert(full.vertices.back().z == 768.0F);
    assert(full.indices[0] == 0);
    assert(full.indices[1] == 1);
    assert(full.indices[2] == 34);
    assert(full.indices[3] == 0);
    assert(full.indices[4] == 34);
    assert(full.indices[5] == 33);

    const auto stride4 = f4lod::build_terrain_cell_mesh(cell, 4);
    assert(stride4.valid);
    assert(stride4.vertices_per_side == 9);
    assert(stride4.vertices.size() == 81);
    assert(stride4.indices.size() == 8 * 8 * 6);
    assert(stride4.vertices.back().x == 0.0F);
    assert(stride4.vertices.back().y == 12288.0F);
    assert(stride4.vertices.back().z == 768.0F);

    assert(!f4lod::build_terrain_cell_mesh(cell, 0).valid);
    assert(!f4lod::build_terrain_cell_mesh(cell, 3).valid);

    return 0;
}
