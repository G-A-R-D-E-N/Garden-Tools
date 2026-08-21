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
    if (options.water_delta < 0.0F) {
        result.output_triangles = result.source_triangles;
        result.valid = true;
        return result;
    }

    result.enabled = true;

    // Recovered LODGen order applies WaterDelta to the regular terrain
    // heightfield before greedy triangulation. Do not pre-simplify or compact
    // the mesh here: doing so destroys the (32*LOD+1)^2 grid that Terrain/
    // GreedySubdivision consumes.
    if (options.water_delta > 0.0F) {
        for (std::size_t i = 0; i < result.mesh.vertices.size(); ++i) {
            const float water = result.mesh.vertex_water_height[i];
            if (!std::isfinite(water)) continue;
            auto& z = result.mesh.vertices[i].z;
            if (z >= water) continue;
            const float target = water - options.water_delta;
            if (z > target) {
                z = target;
                ++result.shifted_vertices;
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

    // Coarsening is intentionally left to the same terrain Quality reducer as
    // visible terrain, matching the recovered engine flow. The exact
    // WaterDelta=0 heightfield mutation remains an oracle item until the
    // decompiled routine is fully characterized.
    result.output_triangles = result.source_triangles;
    result.valid = true;
    (void) options.protect_cell_borders;
    (void) options.regularize;
    return result;
}

} // namespace f4lod
