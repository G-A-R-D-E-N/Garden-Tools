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
    assert(unchanged.raised_vertices == 0U);
    assert(unchanged.mesh.vertices.size() == source.vertices.size());
    assert(unchanged.mesh.indices.size() == source.indices.size());

    // WaterDelta=0 is active Optimize Unseen: submerged heights flatten to the
    // water plane and interior underwater samples become ineligible for greedy
    // insertion. It is not equivalent to WaterDelta=-1.
    f4lod::TerrainUnseenOptions zero;
    zero.water_delta = 0.0F;
    const auto flattened = f4lod::optimize_unseen_terrain(source, zero);
    assert(flattened.valid);
    assert(flattened.enabled);
    assert(flattened.raised_vertices == 0U);
    const std::size_t center = 4U * 9U + 4U;
    assert(std::fabs(flattened.mesh.vertices[center].z - 10.0F) < 0.001F);
    assert(flattened.ignored_vertices[center] == 1U);
    assert(flattened.forced_vertices[center] == 0U);

    f4lod::TerrainUnseenOptions shallow;
    shallow.water_delta = 5.0F;
    const auto optimized = f4lod::optimize_unseen_terrain(source, shallow);
    assert(optimized.valid);
    assert(optimized.enabled);
    assert(optimized.shifted_vertices > 0U);
    assert(optimized.raised_vertices > 0U);
    assert(optimized.underwater_triangles > 0U);
    assert(optimized.output_triangles == optimized.source_triangles);
    assert(std::fabs(optimized.mesh.vertices[center].z - 5.0F) < 0.001F);
    assert(optimized.ignored_vertices[center] == 1U);

    // Above-water samples receive the reference engine's WaterDelta/100 nudge.
    assert(std::fabs(optimized.mesh.vertices.front().z - 20.05F) < 0.001F);

    // An underwater outer-boundary sample is forced rather than ignored so
    // adjacent terrain blocks can retain a matching seam.
    auto boundary_source = source;
    boundary_source.vertices.front().z = 8.0F;
    const auto boundary = f4lod::optimize_unseen_terrain(boundary_source, shallow);
    assert(boundary.valid);
    assert(boundary.forced_vertices.front() == 1U);
    assert(boundary.ignored_vertices.front() == 0U);
    assert(std::fabs(boundary.mesh.vertices.front().z - 5.0F) < 0.001F);

    auto protected_source = source;
    protected_source.lod_level = 4U;
    protected_source.cell_border[center] = 1U;
    auto protected_options = zero;
    protected_options.protect_cell_borders = true;
    const auto protected_result = f4lod::optimize_unseen_terrain(protected_source, protected_options);
    assert(protected_result.valid);
    assert(protected_result.ignored_vertices[center] == 0U);

    auto invalid = shallow;
    invalid.water_delta = -2.0F;
    assert(!f4lod::optimize_unseen_terrain(source, invalid).valid);
    return 0;
}
