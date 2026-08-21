#include "f4lod/terrain_unseen.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

f4lod::TerrainBlockMesh make_water_mesh() {
    constexpr std::size_t side = 9;
    f4lod::TerrainBlockMesh mesh;
    mesh.valid = true;
    mesh.lod_level = 8;
    mesh.block_x = 0;
    mesh.block_y = 0;

    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const bool shoreline = x == 0 || y == 0 || x + 1 == side || y + 1 == side;
            const float z = shoreline ? 20.0F : 8.0F;
            mesh.vertices.push_back({
                static_cast<float>(x) * 128.0F,
                static_cast<float>(y) * 128.0F,
                z,
            });
            mesh.outer_border.push_back(shoreline ? 1U : 0U);
            mesh.cell_border.push_back(0U);
            mesh.vertex_water_height.push_back(10.0F);
        }
    }

    for (std::size_t y = 0; y + 1 < side; ++y) {
        for (std::size_t x = 0; x + 1 < side; ++x) {
            const auto bl = static_cast<std::uint32_t>(y * side + x);
            const auto br = bl + 1U;
            const auto tl = bl + static_cast<std::uint32_t>(side);
            const auto tr = tl + 1U;
            mesh.indices.insert(mesh.indices.end(), {bl, br, tr, bl, tr, tl});
        }
    }
    mesh.min_height = 8.0F;
    mesh.max_height = 20.0F;
    return mesh;
}

} // namespace

int main() {
    const auto source = make_water_mesh();

    f4lod::TerrainUnseenOptions off;
    off.water_delta = -1.0F;
    const auto unchanged = f4lod::optimize_unseen_terrain(source, off);
    assert(unchanged.valid);
    assert(!unchanged.enabled);
    assert(unchanged.shifted_vertices == 0U);
    assert(unchanged.mesh.vertices.size() == source.vertices.size());
    assert(unchanged.mesh.indices.size() == source.indices.size());

    f4lod::TerrainUnseenOptions shallow;
    shallow.water_delta = 5.0F;
    const auto optimized = f4lod::optimize_unseen_terrain(source, shallow);
    assert(optimized.valid);
    assert(optimized.enabled);
    assert(optimized.shifted_vertices > 0U);
    assert(optimized.underwater_triangles > 0U);
    assert(optimized.output_triangles <= optimized.source_triangles);

    // Visible shoreline is never shifted below its original height.
    for (const auto& vertex : optimized.mesh.vertices) {
        const bool shoreline = vertex.x == 0.0F || vertex.y == 0.0F ||
                               vertex.x == 1024.0F || vertex.y == 1024.0F;
        if (shoreline) {
            assert(std::fabs(vertex.z - 20.0F) < 0.001F);
        } else if (vertex.z < 10.0F) {
            // Shallow submerged samples are pushed to at least water - delta.
            assert(vertex.z <= 5.001F);
        }
    }

    auto invalid = shallow;
    invalid.water_delta = -2.0F;
    assert(!f4lod::optimize_unseen_terrain(source, invalid).valid);
    return 0;
}
