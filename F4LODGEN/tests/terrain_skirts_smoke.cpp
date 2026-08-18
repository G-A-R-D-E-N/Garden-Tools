#include "f4lod/terrain_skirts.hpp"

#include <cassert>
#include <cmath>

namespace {

f4lod::TerrainSimplifiedMesh make_block(float span, float z) {
    f4lod::TerrainSimplifiedMesh mesh;
    mesh.valid = true;
    mesh.vertices = {
        {0.0F, 0.0F, z},
        {span, 0.0F, z},
        {span, span, z},
        {0.0F, span, z},
    };
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.source_vertices = 4U;
    mesh.source_triangles = 2U;
    mesh.target_triangles = 2U;
    return mesh;
}

} // namespace

int main() {
    assert(f4lod::recovered_terrain_reducer_budget(32767U, 256.0F) == 30767U);
    assert(f4lod::recovered_terrain_reducer_budget(2000U, 256.0F) == 512U);
    assert(f4lod::recovered_terrain_reducer_budget(100U, -14098.0F) == 512U);
    assert(f4lod::recovered_terrain_reducer_budget(32767U, 0.0F) == 32767U);

    f4lod::TerrainSkirtOptions lod4;
    lod4.lod_level = 4U;
    lod4.skirts = 256.0F;
    const auto lod4_result = f4lod::add_recovered_terrain_skirts(
        make_block(4.0F * 4096.0F, 100.0F), lod4);
    assert(lod4_result.valid);
    assert(lod4_result.boundary_edges == 4U);
    assert(lod4_result.added_vertices == 8U);
    assert(lod4_result.added_triangles == 8U);
    assert(std::fabs(lod4_result.effective_local_depth - 256.0F) < 0.001F);
    for (std::size_t i = 4U; i < lod4_result.mesh.vertices.size(); ++i) {
        assert(std::fabs(lod4_result.mesh.vertices[i].z + 924.0F) < 0.001F);
    }

    f4lod::TerrainSkirtOptions lod8;
    lod8.lod_level = 8U;
    lod8.skirts = 256.0F;
    const auto lod8_result = f4lod::add_recovered_terrain_skirts(
        make_block(8.0F * 4096.0F, 100.0F), lod8);
    assert(lod8_result.valid);
    assert(std::fabs(lod8_result.effective_local_depth - 193.0F) < 0.001F);
    for (std::size_t i = 4U; i < lod8_result.mesh.vertices.size(); ++i) {
        assert(std::fabs(lod8_result.mesh.vertices[i].z + 1444.0F) < 0.001F);
    }

    f4lod::TerrainSkirtOptions absolute;
    absolute.lod_level = 4U;
    absolute.skirts = -14098.0F;
    const auto absolute_result = f4lod::add_recovered_terrain_skirts(
        make_block(4.0F * 4096.0F, 100.0F), absolute);
    assert(absolute_result.valid);
    assert(std::fabs(absolute_result.effective_local_depth + 3524.5F) < 0.001F);
    for (std::size_t i = 4U; i < absolute_result.mesh.vertices.size(); ++i) {
        assert(std::fabs(absolute_result.mesh.vertices[i].z + 14098.0F) < 0.001F);
    }

    f4lod::TerrainSkirtOptions off = lod4;
    off.skirts = 0.0F;
    const auto off_result = f4lod::add_recovered_terrain_skirts(
        make_block(4.0F * 4096.0F, 100.0F), off);
    assert(off_result.valid);
    assert(off_result.added_vertices == 0U);
    assert(off_result.added_triangles == 0U);
    assert(off_result.mesh.vertices.size() == 4U);
    assert(off_result.mesh.indices.size() == 6U);

    auto invalid = lod4;
    invalid.lod_level = 2U;
    assert(!f4lod::add_recovered_terrain_skirts(
        make_block(4.0F * 4096.0F, 100.0F), invalid).valid);
    return 0;
}
