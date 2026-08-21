#include "f4lod/terrain_greedy.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

f4lod::TerrainBlockMesh make_heightfield(float spike_height) {
    f4lod::TerrainBlockMesh mesh;
    mesh.valid = true;
    mesh.lod_level = 4U;
    mesh.block_x = 0;
    mesh.block_y = 0;

    constexpr std::uint32_t side = 129U;
    mesh.vertices.reserve(static_cast<std::size_t>(side) * side);
    mesh.outer_border.reserve(static_cast<std::size_t>(side) * side);
    mesh.cell_border.reserve(static_cast<std::size_t>(side) * side);
    mesh.vertex_water_height.reserve(static_cast<std::size_t>(side) * side);

    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const float z = x == 50U && y == 50U ? spike_height : 0.0F;
            mesh.vertices.push_back(f4lod::TerrainVertex{
                static_cast<float>(x) * f4lod::kLandscapeVertexSpacing,
                static_cast<float>(y) * f4lod::kLandscapeVertexSpacing,
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
            mesh.indices.push_back(bl);
            mesh.indices.push_back(br);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(bl);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(tl);
        }
    }
    mesh.min_height = 0.0F;
    mesh.max_height = spike_height;
    return mesh;
}

} // namespace

int main() {
    const auto source = make_heightfield(40.0F);

    f4lod::TerrainGreedyOptions coarse;
    coarse.quality = 20;
    coarse.point_limit = 32767U;
    coarse.protect_cell_borders = false;
    const auto coarse_result = f4lod::reduce_terrain_greedy(source, coarse);
    assert(coarse_result.valid);
    assert(coarse_result.threshold_quad_units == 20.0F);
    assert(coarse_result.max_error_quad_units < 20.0F);
    assert(coarse_result.selected_points == 512U); // constrained 129x129 perimeter
    assert(coarse_result.greedy_insertions == 0U);
    assert(!coarse_result.point_limit_reached);

    f4lod::TerrainGreedyOptions detailed = coarse;
    detailed.quality = 4;
    const auto detailed_result = f4lod::reduce_terrain_greedy(source, detailed);
    assert(detailed_result.valid);
    assert(detailed_result.threshold_quad_units == 4.0F);
    assert(detailed_result.selected_points > coarse_result.selected_points);
    assert(detailed_result.greedy_insertions > 0U);
    assert(detailed_result.max_error_quad_units < 4.0F);
    assert(detailed_result.max_error_world_units < 16.0F);

    f4lod::TerrainGreedyOptions protected_lod4 = coarse;
    protected_lod4.quality = 4;
    protected_lod4.protect_cell_borders = true;
    const auto protected_result = f4lod::reduce_terrain_greedy(source, protected_lod4);
    assert(protected_result.valid);
    assert(std::fabs(protected_result.threshold_quad_units - 1.0F) < 0.0001F);
    assert(protected_result.selected_points >= 1265U); // all 32-sample cell grid lines survive
    assert(protected_result.max_error_quad_units < 1.0F);
    assert(protected_result.max_error_world_units < 4.0F);

    auto invalid = coarse;
    invalid.quality = -1;
    assert(!f4lod::reduce_terrain_greedy(source, invalid).valid);

    auto incomplete = source;
    incomplete.vertices.pop_back();
    incomplete.outer_border.pop_back();
    incomplete.cell_border.pop_back();
    incomplete.vertex_water_height.pop_back();
    assert(!f4lod::reduce_terrain_greedy(incomplete, coarse).valid);

    return 0;
}
