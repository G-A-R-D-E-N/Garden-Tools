#include "f4lod/terrain_quality.hpp"

#include "f4lod/terrain_greedy.hpp"
#include "f4lod/terrain_skirts.hpp"
#include "f4lod/terrain_unseen.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace f4lod {
namespace {

TerrainSimplifiedMesh copy_source(const TerrainBlockMesh& source) {
    TerrainSimplifiedMesh result;
    result.valid = true;
    result.source_vertices = source.vertices.size();
    result.source_triangles = source.indices.size() / 3U;
    result.target_triangles = result.source_triangles;
    result.result_error = 0.0F;
    result.vertices = source.vertices;
    result.indices = source.indices;
    return result;
}

} // namespace

TerrainQualityResult simplify_terrain_xlodgen_quality(
    const TerrainBlockMesh& source,
    const TerrainQualityOptions& options) {
    TerrainQualityResult result;
    result.requested_quality = options.quality;
    result.water_delta = options.water_delta;

    if (!source.valid) {
        result.error = "Terrain source block is invalid: " + source.error;
        return result;
    }
    if (source.vertices.empty() || source.indices.empty() || source.indices.size() % 3U != 0U) {
        result.error = "Terrain source block does not contain a valid triangle list.";
        return result;
    }
    if (source.outer_border.size() != source.vertices.size() ||
        source.cell_border.size() != source.vertices.size() ||
        source.vertex_water_height.size() != source.vertices.size()) {
        result.error = "Terrain source boundary/water metadata does not match its vertex count.";
        return result;
    }
    if (options.quality < -1) {
        result.error = "xLODGen terrain Quality must be -1 or greater.";
        return result;
    }
    if (options.max_vertices < 4U || options.max_vertices > 65535U) {
        result.error = "xLODGen terrain MaxVerts must be between 4 and 65535.";
        return result;
    }
    const auto max_fo4_triangles = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (options.max_triangles == 0U || options.max_triangles > max_fo4_triangles) {
        result.error = "FO4 terrain triangle ceiling must fit the format's uint32 triangle count.";
        return result;
    }
    if (!std::isfinite(options.water_delta) || options.water_delta < -1.0F) {
        result.error = "WaterDelta must be -1 (off), 0, or a positive distance.";
        return result;
    }
    if (!std::isfinite(options.skirts)) {
        result.error = "Skirts must be finite.";
        return result;
    }

    TerrainUnseenOptions unseen_options;
    unseen_options.water_delta = options.water_delta;
    unseen_options.protect_cell_borders = options.protect_cell_borders;
    unseen_options.regularize = options.regularize;
    auto unseen = optimize_unseen_terrain(source, unseen_options);
    if (!unseen.valid) {
        result.error = "Optimize Unseen heightfield preparation failed: " + unseen.error;
        return result;
    }
    result.optimize_unseen = unseen.enabled;
    result.shifted_underwater_vertices = unseen.shifted_vertices;
    result.underwater_triangles = unseen.underwater_triangles;
    const auto& working = unseen.mesh;

    // Only LOD4 has the pre-triangulated full-geometry path. MaxVerts is a
    // greedy insertion target and does not constrain this special path; the
    // BTR writer splits final land geometry when the ushort index range needs it.
    if (options.quality == -1 && source.lod_level == 4U) {
        result.mesh = copy_source(working);
        if (result.mesh.indices.size() / 3U > options.max_triangles) {
            result.error = "Full LOD4 terrain geometry exceeds the configured FO4 triangle ceiling.";
            result.mesh.valid = false;
            return result;
        }
        result.full_geometry = true;
        result.effective_error = 0.0F;
        result.effective_error_quad_units = 0.0F;
        result.effective_error_world_units = 0.0F;
        result.valid = true;
        return result;
    }

    TerrainGreedyOptions greedy_options;
    greedy_options.quality = options.quality < 0 ? 0 : options.quality;
    greedy_options.point_limit = recovered_terrain_reducer_budget(
        options.max_vertices, options.skirts);
    greedy_options.protect_cell_borders = options.protect_cell_borders;
    greedy_options.forced_points = unseen.forced_vertices;
    greedy_options.ignored_points = unseen.ignored_vertices;
    auto greedy = reduce_terrain_greedy(working, greedy_options);
    if (!greedy.valid) {
        result.error = "Greedy terrain Quality reduction failed: " + greedy.error;
        return result;
    }

    if (greedy.mesh.indices.size() / 3U > options.max_triangles) {
        result.error = "Greedy terrain output exceeds the configured FO4 triangle ceiling.";
        return result;
    }

    result.effective_error = greedy.max_error_quad_units;
    result.effective_error_quad_units = greedy.max_error_quad_units;
    result.effective_error_world_units = greedy.max_error_world_units;
    result.forced_by_limits = greedy.point_limit_reached ||
                              greedy.selected_points > greedy_options.point_limit;
    result.greedy_insertions = greedy.greedy_insertions;
    result.full_geometry = greedy.mesh.vertices.size() == working.vertices.size();
    result.mesh = std::move(greedy.mesh);
    result.valid = true;
    (void) options.regularize;
    return result;
}

} // namespace f4lod
