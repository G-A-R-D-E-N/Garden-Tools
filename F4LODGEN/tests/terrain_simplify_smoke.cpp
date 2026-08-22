#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

f4lod::TerrainCell make_cell(std::int32_t x, std::int32_t y) {
    f4lod::TerrainCell cell;
    cell.cell = {"Fixture.esm", static_cast<std::uint32_t>(0x1000 + y * 8 + x)};
    cell.land = {"Fixture.esm", static_cast<std::uint32_t>(0x2000 + y * 8 + x)};
    cell.x = x;
    cell.y = y;
    cell.heightmap.valid = true;
    cell.heightmap.land = cell.land;

    float min_height = 1.0e30F;
    float max_height = -1.0e30F;
    for (std::size_t sy = 0; sy < f4lod::kLandscapeVertexSide; ++sy) {
        for (std::size_t sx = 0; sx < f4lod::kLandscapeVertexSide; ++sx) {
            const float gx = static_cast<float>(x * 32 + static_cast<std::int32_t>(sx));
            const float gy = static_cast<float>(y * 32 + static_cast<std::int32_t>(sy));
            const float height = 64.0F + std::sin(gx * 0.05F) * 16.0F + std::cos(gy * 0.04F) * 12.0F;
            cell.heightmap.heights[sy * f4lod::kLandscapeVertexSide + sx] = height;
            min_height = std::min(min_height, height);
            max_height = std::max(max_height, height);
        }
    }
    cell.heightmap.min_height = min_height;
    cell.heightmap.max_height = max_height;
    return cell;
}

bool same_vertex(const f4lod::TerrainVertex& a, const f4lod::TerrainVertex& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

int main() {
    f4lod::TerrainWorld world;
    world.worldspace = {"Fallout4.esm", 0x18AA2};

    f4lod::TerrainBlockPlan plan;
    plan.lod_level = 4;
    plan.x = 0;
    plan.y = 0;

    for (std::int32_t y = 0; y < 4; ++y) {
        for (std::int32_t x = 0; x < 4; ++x) {
            const auto index = world.cells.size();
            world.cells.push_back(make_cell(x, y));
            plan.cells.push_back({index, x, y});
        }
    }

    const auto source = f4lod::build_terrain_block_source_mesh(world, plan);
    assert(source.valid);
    assert(source.vertices.size() == 129U * 129U);
    assert(source.indices.size() / 3U == 32768U);

    f4lod::TerrainSimplifyOptions options;
    options.target_triangles = 4096;
    options.target_error = 0.05F;
    options.lock_outer_border = true;
    options.lock_cell_borders = false;
    options.max_vertices = 65535;

    const auto simplified = f4lod::simplify_terrain_block(source, options);
    assert(simplified.valid);
    assert(!simplified.vertices.empty());
    assert(!simplified.indices.empty());
    assert(simplified.indices.size() / 3U < source.indices.size() / 3U);
    assert(simplified.vertices.size() <= 65535U);

    // Every locked outer vertex must still exist at exactly the same position.
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        if (source.outer_border[i] == 0U) continue;
        const auto found = std::find_if(
            simplified.vertices.begin(), simplified.vertices.end(),
            [&](const auto& candidate) { return same_vertex(candidate, source.vertices[i]); });
        assert(found != simplified.vertices.end());
    }

    auto impossible = options;
    impossible.max_vertices = 64;
    const auto rejected = f4lod::simplify_terrain_block(source, impossible);
    assert(!rejected.valid);
    assert(!rejected.error.empty());

    auto bad_error = options;
    bad_error.target_error = 2.0F;
    assert(!f4lod::simplify_terrain_block(source, bad_error).valid);

    return 0;
}
