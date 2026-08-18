#include "f4lod/terrain_unseen.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace f4lod {
namespace {

bool underwater_vertex(const TerrainBlockMesh& mesh, std::uint32_t index) noexcept {
    if (index >= mesh.vertices.size() || index >= mesh.vertex_water_height.size()) return false;
    const float water = mesh.vertex_water_height[index];
    return std::isfinite(water) && mesh.vertices[index].z < water;
}

std::size_t square_side(std::size_t count) noexcept {
    const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(count))));
    return side != 0U && side * side == count ? side : 0U;
}

bool diagonals_all_below_water(
    const TerrainBlockMesh& mesh,
    std::size_t side,
    std::size_t x,
    std::size_t y) noexcept {
    for (int dy : {-1, 1}) {
        for (int dx : {-1, 1}) {
            const auto nx_signed = static_cast<std::ptrdiff_t>(x) + dx;
            const auto ny_signed = static_cast<std::ptrdiff_t>(y) + dy;
            if (nx_signed < 0 || ny_signed < 0 ||
                nx_signed >= static_cast<std::ptrdiff_t>(side) ||
                ny_signed >= static_cast<std::ptrdiff_t>(side)) {
                continue;
            }
            const auto nx = static_cast<std::size_t>(nx_signed);
            const auto ny = static_cast<std::size_t>(ny_signed);
            const auto index = ny * side + nx;
            if (index >= mesh.vertices.size() || index >= mesh.vertex_water_height.size()) return false;
            const float water = mesh.vertex_water_height[index];
            if (!std::isfinite(water) || mesh.vertices[index].z >= water) return false;
        }
    }
    return true;
}

} // namespace

TerrainUnseenResult optimize_unseen_terrain(
    const TerrainBlockMesh& source,
    const TerrainUnseenOptions& options) {
    TerrainUnseenResult result;
    result.source_triangles = source.indices.size() / 3U;

    if (!source.valid) {
        result.error = "Optimize Unseen source is invalid: " + source.error;
        return result;
    }
    if (source.indices.empty() || source.indices.size() % 3U != 0U || source.vertices.empty()) {
        result.error = "Optimize Unseen source does not contain a valid triangle list.";
        return result;
    }
    if (source.outer_border.size() != source.vertices.size() ||
        source.cell_border.size() != source.vertices.size() ||
        source.vertex_water_height.size() != source.vertices.size()) {
        result.error = "Optimize Unseen source metadata does not match vertex count.";
        return result;
    }
    if (!std::isfinite(options.water_delta) || options.water_delta < -1.0F) {
        result.error = "WaterDelta must be -1 (off), 0, or a positive distance.";
        return result;
    }

    result.mesh = source;
    result.ignored_vertices.assign(source.vertices.size(), 0U);
    result.forced_vertices.assign(source.vertices.size(), 0U);
    if (options.water_delta < 0.0F) {
        result.output_triangles = result.source_triangles;
        result.valid = true;
        return result;
    }

    const auto side = square_side(source.vertices.size());
    if (side == 0U) {
        result.error = "Optimize Unseen requires the complete regular terrain heightfield.";
        return result;
    }

    result.enabled = true;

    // TerrainLOD.GenerateQuad operates in quad-local Z. F4LODGen keeps the
    // working mesh in world units until the greedy reducer, so the reference
    // delta/lodLevel conversion cancels when expressed here: underwater target
    // = water - WaterDelta, above-water nudge = WaterDelta / 100.
    const float above_water_nudge = options.water_delta / 100.0F;
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const auto i = y * side + x;
            const float water = result.mesh.vertex_water_height[i];
            if (!std::isfinite(water)) continue;

            auto& z = result.mesh.vertices[i].z;
            if (z >= water) {
                if (above_water_nudge > 0.0F) {
                    z += above_water_nudge;
                    ++result.raised_vertices;
                }
                continue;
            }

            const float target = water - options.water_delta;
            if (z > target) {
                z = target;
                ++result.shifted_vertices;
            }

            const bool protected_cell_border =
                options.protect_cell_borders && source.lod_level == 4U &&
                source.cell_border[i] != 0U;
            // The reference classifies the point from the original water/land
            // relation before the current sample is flattened or lowered.
            if (diagonals_all_below_water(source, side, x, y) && !protected_cell_border) {
                result.ignored_vertices[i] = 1U;
            }

            // The reference engine forces underwater points on the outer quad
            // boundary when Optimize Unseen is active so neighboring blocks
            // retain matching seams.
            if (source.outer_border[i] != 0U) {
                result.forced_vertices[i] = 1U;
                result.ignored_vertices[i] = 0U;
            }
        }
    }

    for (std::size_t tri = 0; tri < result.mesh.indices.size(); tri += 3U) {
        const auto a = result.mesh.indices[tri];
        const auto b = result.mesh.indices[tri + 1U];
        const auto c = result.mesh.indices[tri + 2U];
        if (a >= result.mesh.vertices.size() || b >= result.mesh.vertices.size() ||
            c >= result.mesh.vertices.size()) {
            result.error = "Optimize Unseen source contains an out-of-range index.";
            return result;
        }
        if (underwater_vertex(result.mesh, a) &&
            underwater_vertex(result.mesh, b) &&
            underwater_vertex(result.mesh, c)) {
            ++result.underwater_triangles;
        }
    }

    // Geometry remains a complete regular heightfield here. Ignored/forced
    // point states are consumed by the greedy reducer rather than deleting or
    // compacting source triangles before Quality refinement.
    result.output_triangles = result.source_triangles;
    result.valid = true;
    (void) options.regularize;
    return result;
}

} // namespace f4lod
