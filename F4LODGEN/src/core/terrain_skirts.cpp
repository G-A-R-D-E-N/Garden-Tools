#include "f4lod/terrain_skirts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace f4lod {
namespace {

constexpr float kBoundaryEpsilon = 0.01F;

enum class BoundarySide {
    none,
    south,
    north,
    west,
    east,
};

bool valid_lod(std::uint32_t lod) noexcept {
    return lod == 4U || lod == 8U || lod == 16U || lod == 32U;
}

bool near(float value, float target) noexcept {
    return std::fabs(value - target) <= kBoundaryEpsilon;
}

BoundarySide shared_boundary(
    const TerrainVertex& a,
    const TerrainVertex& b,
    float min_x,
    float min_y,
    float max_x,
    float max_y) noexcept {
    if (near(a.y, min_y) && near(b.y, min_y)) return BoundarySide::south;
    if (near(a.y, max_y) && near(b.y, max_y)) return BoundarySide::north;
    if (near(a.x, min_x) && near(b.x, min_x)) return BoundarySide::west;
    if (near(a.x, max_x) && near(b.x, max_x)) return BoundarySide::east;
    return BoundarySide::none;
}

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) noexcept {
    const auto low = std::min(a, b);
    const auto high = std::max(a, b);
    return (static_cast<std::uint64_t>(low) << 32U) | high;
}

float effective_positive_depth(float skirts, std::uint32_t lod_level) noexcept {
    unsigned int decay_steps = 0U;
    switch (lod_level) {
    case 4U: decay_steps = 0U; break;
    case 8U: decay_steps = 1U; break;
    case 16U: decay_steps = 2U; break;
    case 32U: decay_steps = 3U; break;
    default: return skirts;
    }
    return skirts - static_cast<float>(decay_steps * 63U);
}

float skirt_world_z(
    float source_z,
    float skirts,
    float effective_local_depth,
    std::uint32_t lod_level) noexcept {
    if (skirts < 0.0F) {
        // TerrainLOD divides negative Skirts by lodLevel before assigning the
        // local vertex Z; the terrain node scale multiplies it by lodLevel on
        // render, restoring the configured absolute world Z.
        return skirts;
    }
    return source_z - effective_local_depth * static_cast<float>(lod_level);
}

bool winding_a(
    BoundarySide side,
    const TerrainVertex& a,
    const TerrainVertex& b) noexcept {
    switch (side) {
    case BoundarySide::south: return b.x > a.x;
    case BoundarySide::north: return b.x < a.x;
    case BoundarySide::west: return b.y < a.y;
    case BoundarySide::east: return b.y > a.y;
    case BoundarySide::none: return false;
    }
    return false;
}

} // namespace

std::size_t recovered_terrain_reducer_budget(
    std::size_t configured_max_vertices,
    float skirts) noexcept {
    if (skirts == 0.0F) return configured_max_vertices;
    const std::size_t reduced = configured_max_vertices > 2000U
        ? configured_max_vertices - 2000U
        : 0U;
    return std::max<std::size_t>(512U, reduced);
}

TerrainSkirtResult add_recovered_terrain_skirts(
    const TerrainSimplifiedMesh& source,
    const TerrainSkirtOptions& options) {
    TerrainSkirtResult result;
    if (!source.valid) {
        result.error = "Terrain skirt source mesh is invalid: " + source.error;
        return result;
    }
    if (source.vertices.empty() || source.indices.empty() || source.indices.size() % 3U != 0U) {
        result.error = "Terrain skirt source mesh does not contain a valid triangle list.";
        return result;
    }
    if (!valid_lod(options.lod_level)) {
        result.error = "Terrain skirts require LOD4, LOD8, LOD16, or LOD32.";
        return result;
    }
    if (!std::isfinite(options.block_origin_x) || !std::isfinite(options.block_origin_y) ||
        !std::isfinite(options.skirts)) {
        result.error = "Terrain skirt settings contain non-finite values.";
        return result;
    }
    for (const auto index : source.indices) {
        if (index >= source.vertices.size()) {
            result.error = "Terrain skirt source contains an out-of-range index.";
            return result;
        }
    }

    result.mesh = source;
    if (options.skirts == 0.0F) {
        result.valid = true;
        return result;
    }

    const float block_span = static_cast<float>(options.lod_level) * 4096.0F;
    const float min_x = options.block_origin_x;
    const float min_y = options.block_origin_y;
    const float max_x = min_x + block_span;
    const float max_y = min_y + block_span;
    result.effective_local_depth = options.skirts > 0.0F
        ? effective_positive_depth(options.skirts, options.lod_level)
        : options.skirts / static_cast<float>(options.lod_level);

    std::unordered_set<std::uint64_t> emitted;
    emitted.reserve(source.indices.size() / 3U);

    for (std::size_t tri = 0; tri < source.indices.size(); tri += 3U) {
        const std::uint32_t triangle[3]{
            source.indices[tri],
            source.indices[tri + 1U],
            source.indices[tri + 2U],
        };
        for (unsigned int edge = 0U; edge < 3U; ++edge) {
            const auto a_index = triangle[edge];
            const auto b_index = triangle[(edge + 1U) % 3U];
            const auto key = edge_key(a_index, b_index);
            if (emitted.contains(key)) continue;

            const auto& a = source.vertices[a_index];
            const auto& b = source.vertices[b_index];
            const auto side = shared_boundary(a, b, min_x, min_y, max_x, max_y);
            if (side == BoundarySide::none) continue;
            emitted.insert(key);

            if (result.mesh.vertices.size() + 2U >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                result.error = "Terrain skirt vertex count exceeds the 32-bit working range.";
                return result;
            }

            const auto skirt_a = static_cast<std::uint32_t>(result.mesh.vertices.size());
            auto lowered_a = a;
            auto lowered_b = b;
            lowered_a.z = skirt_world_z(a.z, options.skirts, result.effective_local_depth, options.lod_level);
            lowered_b.z = skirt_world_z(b.z, options.skirts, result.effective_local_depth, options.lod_level);
            result.mesh.vertices.push_back(lowered_a);
            result.mesh.vertices.push_back(lowered_b);
            const auto skirt_b = skirt_a + 1U;

            if (winding_a(side, a, b)) {
                result.mesh.indices.insert(result.mesh.indices.end(), {
                    b_index, a_index, skirt_a,
                    b_index, skirt_a, skirt_b,
                });
            } else {
                result.mesh.indices.insert(result.mesh.indices.end(), {
                    a_index, b_index, skirt_b,
                    a_index, skirt_b, skirt_a,
                });
            }
            ++result.boundary_edges;
        }
    }

    result.added_vertices = result.mesh.vertices.size() - source.vertices.size();
    result.added_triangles = result.mesh.indices.size() / 3U - source.indices.size() / 3U;
    result.mesh.target_triangles = result.mesh.indices.size() / 3U;
    result.valid = true;
    return result;
}

} // namespace f4lod
