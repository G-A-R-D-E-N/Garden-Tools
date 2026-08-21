#include "f4lod/terrain_block_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace f4lod {
namespace {

using SampleKey = std::pair<std::int64_t, std::int64_t>;

struct GridVertex {
    std::uint32_t index{};
    float height{};
    bool authored{};
};

bool supported_level(std::uint32_t level) noexcept {
    return level == 4U || level == 8U || level == 16U || level == 32U;
}

float merge_water(float current, bool has_water, float water_height) noexcept {
    if (!has_water || !std::isfinite(water_height)) return current;
    if (!std::isfinite(current)) return water_height;
    return std::max(current, water_height);
}

constexpr float kNoWaterHeightThreshold = 16777216.0F;

} // namespace

TerrainBlockMesh build_terrain_block_source_mesh(
    const TerrainWorld& world,
    const TerrainBlockPlan& plan,
    float shared_height_tolerance) {
    TerrainBlockMesh mesh;
    mesh.lod_level = plan.lod_level;
    mesh.block_x = plan.x;
    mesh.block_y = plan.y;

    if (!supported_level(plan.lod_level)) {
        mesh.error = "Terrain block LOD level must be 4, 8, 16, or 32.";
        return mesh;
    }
    if (plan.cells.empty()) {
        mesh.error = "Terrain block plan contains no resolved cells.";
        return mesh;
    }
    if (!std::isfinite(shared_height_tolerance) || shared_height_tolerance < 0.0F) {
        mesh.error = "Shared-height tolerance must be finite and non-negative.";
        return mesh;
    }
    if (!std::isfinite(world.default_land_height) || !std::isfinite(world.default_water_height)) {
        mesh.error = "Terrain world default land/water heights must be finite before block assembly.";
        return mesh;
    }

    const auto block_min_gx = static_cast<std::int64_t>(plan.x) * 32;
    const auto block_min_gy = static_cast<std::int64_t>(plan.y) * 32;
    const auto block_max_gx = static_cast<std::int64_t>(plan.x + static_cast<std::int32_t>(plan.lod_level)) * 32;
    const auto block_max_gy = static_cast<std::int64_t>(plan.y + static_cast<std::int32_t>(plan.lod_level)) * 32;

    std::map<SampleKey, GridVertex> grid;
    mesh.min_height = std::numeric_limits<float>::infinity();
    mesh.max_height = -std::numeric_limits<float>::infinity();

    const auto level_side = static_cast<std::size_t>(plan.lod_level);
    std::vector<unsigned char> authored_cells(level_side * level_side, 0U);

    auto emit_cell = [&](std::int32_t cell_x,
                         std::int32_t cell_y,
                         const LandscapeHeightmap* heightmap,
                         bool has_water,
                         float water_height,
                         bool authored) -> bool {
        std::uint32_t local_indices[33][33]{};
        for (std::uint32_t sy = 0; sy <= 32U; ++sy) {
            for (std::uint32_t sx = 0; sx <= 32U; ++sx) {
                const auto gx = static_cast<std::int64_t>(cell_x) * 32 + sx;
                const auto gy = static_cast<std::int64_t>(cell_y) * 32 + sy;
                const SampleKey key{gx, gy};
                const float requested_height = heightmap
                    ? heightmap->at(sx, sy)
                    : world.default_land_height;

                const auto found = grid.find(key);
                if (found != grid.end()) {
                    if (authored && found->second.authored &&
                        std::fabs(found->second.height - requested_height) > shared_height_tolerance) {
                        std::ostringstream message;
                        message << "Shared LAND sample disagreement at global terrain sample ("
                                << gx << ", " << gy << "): " << found->second.height
                                << " vs " << requested_height << '.';
                        mesh.error = message.str();
                        return false;
                    }

                    // Real LAND samples are emitted before fallback cells. A
                    // missing-cell fallback therefore inherits an already
                    // populated shared edge instead of creating a crack where
                    // flat WRLD default terrain meets authored terrain.
                    local_indices[sy][sx] = found->second.index;
                    mesh.vertex_water_height[found->second.index] = merge_water(
                        mesh.vertex_water_height[found->second.index],
                        has_water,
                        water_height);
                    continue;
                }

                if (mesh.vertices.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    mesh.error = "Terrain block source mesh exceeds the 32-bit working index range.";
                    return false;
                }

                const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(TerrainVertex{
                    static_cast<float>(gx) * kLandscapeVertexSpacing,
                    static_cast<float>(gy) * kLandscapeVertexSpacing,
                    requested_height,
                });
                mesh.outer_border.push_back(static_cast<unsigned char>(
                    gx == block_min_gx || gx == block_max_gx ||
                    gy == block_min_gy || gy == block_max_gy));
                mesh.cell_border.push_back(static_cast<unsigned char>(
                    (gx % 32) == 0 || (gy % 32) == 0));
                mesh.vertex_water_height.push_back(
                    has_water && std::isfinite(water_height)
                        ? water_height
                        : std::numeric_limits<float>::quiet_NaN());
                grid.emplace(key, GridVertex{index, requested_height, authored});
                local_indices[sy][sx] = index;
                mesh.min_height = std::min(mesh.min_height, requested_height);
                mesh.max_height = std::max(mesh.max_height, requested_height);
            }
        }

        mesh.indices.reserve(mesh.indices.size() + 32U * 32U * 6U);
        for (std::uint32_t y = 0; y < 32U; ++y) {
            for (std::uint32_t x = 0; x < 32U; ++x) {
                const auto bottom_left = local_indices[y][x];
                const auto bottom_right = local_indices[y][x + 1U];
                const auto top_left = local_indices[y + 1U][x];
                const auto top_right = local_indices[y + 1U][x + 1U];

                mesh.indices.push_back(bottom_left);
                mesh.indices.push_back(bottom_right);
                mesh.indices.push_back(top_right);
                mesh.indices.push_back(bottom_left);
                mesh.indices.push_back(top_right);
                mesh.indices.push_back(top_left);
            }
        }
        return true;
    };

    // Real LAND first so it owns shared samples against synthesized neighbors.
    for (const auto& cell_ref : plan.cells) {
        if (cell_ref.terrain_cell_index >= world.cells.size()) {
            mesh.error = "Terrain block plan references a cell index outside TerrainWorld.";
            return mesh;
        }
        const auto& cell = world.cells[cell_ref.terrain_cell_index];
        if (!cell.heightmap.valid) {
            mesh.error = "Terrain block contains a cell without a decoded heightmap.";
            return mesh;
        }
        if (cell.x - plan.x != cell_ref.local_x || cell.y - plan.y != cell_ref.local_y) {
            mesh.error = "Terrain block cell local coordinates do not match the referenced TerrainWorld cell.";
            return mesh;
        }
        if (cell_ref.local_x < 0 || cell_ref.local_y < 0 ||
            cell_ref.local_x >= static_cast<std::int32_t>(plan.lod_level) ||
            cell_ref.local_y >= static_cast<std::int32_t>(plan.lod_level)) {
            mesh.error = "Terrain block contains a cell outside its LOD extent.";
            return mesh;
        }

        const auto slot = static_cast<std::size_t>(cell_ref.local_y) * level_side +
                          static_cast<std::size_t>(cell_ref.local_x);
        if (authored_cells[slot] != 0U) {
            mesh.error = "Terrain block plan contains duplicate references for one local CELL coordinate.";
            return mesh;
        }
        authored_cells[slot] = 1U;

        if (!emit_cell(
                cell.x,
                cell.y,
                &cell.heightmap,
                cell.has_water,
                cell.water_height,
                true)) {
            return mesh;
        }
    }

    // LODGen fills missing terrain-data cells from the world defaults. Treat a
    // default water value above 2^24 as the engine's no-water sentinel.
    const bool fallback_has_water = world.default_water_height <= kNoWaterHeightThreshold;
    for (std::uint32_t local_y = 0; local_y < plan.lod_level; ++local_y) {
        for (std::uint32_t local_x = 0; local_x < plan.lod_level; ++local_x) {
            const auto slot = static_cast<std::size_t>(local_y) * level_side + local_x;
            if (authored_cells[slot] != 0U) continue;
            if (!emit_cell(
                    plan.x + static_cast<std::int32_t>(local_x),
                    plan.y + static_cast<std::int32_t>(local_y),
                    nullptr,
                    fallback_has_water,
                    world.default_water_height,
                    false)) {
                return mesh;
            }
        }
    }

    const auto expected_side = static_cast<std::size_t>(plan.lod_level) * 32U + 1U;
    const auto expected_vertices = expected_side * expected_side;
    const auto expected_triangles =
        static_cast<std::size_t>(plan.lod_level) * 32U *
        static_cast<std::size_t>(plan.lod_level) * 32U * 2U;
    if (mesh.vertices.size() != expected_vertices || mesh.indices.size() / 3U != expected_triangles) {
        mesh.error = "Terrain block assembly did not produce the complete regular heightfield expected by LODGen.";
        return mesh;
    }
    if (mesh.outer_border.size() != mesh.vertices.size() ||
        mesh.cell_border.size() != mesh.vertices.size() ||
        mesh.vertex_water_height.size() != mesh.vertices.size()) {
        mesh.error = "Terrain block boundary/water metadata does not match vertex count.";
        return mesh;
    }

    mesh.valid = true;
    return mesh;
}

} // namespace f4lod
