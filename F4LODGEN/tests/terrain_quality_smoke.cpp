#include "f4lod/terrain_quality.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

f4lod::TerrainBlockMesh make_heightfield() {
    constexpr std::uint32_t side = 129U;
    f4lod::TerrainBlockMesh mesh;
    mesh.valid = true;
    mesh.lod_level = 4U;
    mesh.block_x = 0;
    mesh.block_y = 0;
    mesh.vertices.reserve(static_cast<std::size_t>(side) * side);
    mesh.outer_border.reserve(static_cast<std::size_t>(side) * side);
    mesh.cell_border.reserve(static_cast<std::size_t>(side) * side);
    mesh.vertex_water_height.reserve(static_cast<std::size_t>(side) * side);

    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            float z = 0.0F;
            if (x == 50U && y == 50U) z = 40.0F;
            mesh.vertices.push_back({
                static_cast<float>(x) * 128.0F,
                static_cast<float>(y) * 128.0F,
                z,
            });
            mesh.outer_border.push_back(static_cast<unsigned char>(
                x == 0U || y == 0U || x + 1U == side || y + 1U == side));
            mesh.cell_border.push_back(static_cast<unsigned char>(x % 32U == 0U || y % 32U == 0U));
            mesh.vertex_water_height.push_back(std::numeric_limits<float>::quiet_NaN());
        }
    }

    for (std::uint32_t y = 0; y + 1U < side; ++y) {
        for (std::uint32_t x = 0; x + 1U < side; ++x) {
            const auto bl = y * side + x;
            const auto br = bl + 1U;
            const auto tl = (y + 1U) * side + x;
            const auto tr = tl + 1U;
            mesh.indices.insert(mesh.indices.end(), {bl, br, tr, bl, tr, tl});
        }
    }
    return mesh;
}

} // namespace

int main() {
    const auto source = make_heightfield();

    f4lod::TerrainQualityOptions full;
    full.quality = -1;
    const auto full_result = f4lod::simplify_terrain_xlodgen_quality(source, full);
    assert(full_result.valid);
    assert(full_result.full_geometry);
    assert(!full_result.forced_by_limits);
    assert(full_result.mesh.vertices.size() == source.vertices.size());
    assert(full_result.mesh.indices.size() == source.indices.size());
    assert(full_result.effective_error_quad_units == 0.0F);
    assert(full_result.effective_error_world_units == 0.0F);

    f4lod::TerrainQualityOptions quality4;
    quality4.quality = 4;
    quality4.protect_cell_borders = false;
    const auto quality4_result = f4lod::simplify_terrain_xlodgen_quality(source, quality4);
    assert(quality4_result.valid);
    assert(!quality4_result.forced_by_limits);
    assert(quality4_result.greedy_insertions > 0U);
    assert(quality4_result.effective_error_quad_units < 4.0F);
    assert(quality4_result.effective_error_world_units < 16.0F);

    auto quality20 = quality4;
    quality20.quality = 20;
    const auto quality20_result = f4lod::simplify_terrain_xlodgen_quality(source, quality20);
    assert(quality20_result.valid);
    assert(quality20_result.greedy_insertions == 0U);
    assert(quality20_result.mesh.vertices.size() < quality4_result.mesh.vertices.size());
    assert(quality20_result.effective_error_quad_units < 20.0F);

    // MaxVerts is a stopping target, not a post-result ceiling. With skirts
    // disabled and MaxVerts=4, the recovered scripted outline already puts the
    // point count above the target. GoalMet therefore reports a limit stop while
    // preserving those required boundary points.
    auto forced = quality4;
    forced.quality = 0;
    forced.max_vertices = 4U;
    forced.max_triangles = 65535U;
    forced.skirts = 0.0F;
    const auto forced_result = f4lod::simplify_terrain_xlodgen_quality(source, forced);
    assert(forced_result.valid);
    assert(forced_result.forced_by_limits);
    assert(forced_result.mesh.vertices.size() > forced.max_vertices);
    assert(forced_result.effective_error_quad_units >= 0.0F);

    auto protected_lod4 = quality4;
    protected_lod4.protect_cell_borders = true;
    const auto protected_result = f4lod::simplify_terrain_xlodgen_quality(source, protected_lod4);
    assert(protected_result.valid);
    assert(protected_result.mesh.vertices.size() >= 1265U);
    assert(protected_result.effective_error_quad_units < 1.0F);
    assert(protected_result.effective_error_world_units < 4.0F);

    auto invalid = quality4;
    invalid.quality = -2;
    assert(!f4lod::simplify_terrain_xlodgen_quality(source, invalid).valid);

    invalid = quality4;
    invalid.max_vertices = 32768U;
    assert(!f4lod::simplify_terrain_xlodgen_quality(source, invalid).valid);

    return 0;
}
