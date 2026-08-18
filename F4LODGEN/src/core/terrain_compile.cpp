#include "f4lod/terrain_compile.hpp"

#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_profile.hpp"
#include "f4lod/terrain_quality.hpp"
#include "f4lod/terrain_skirts.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace f4lod {
namespace {

TerrainSimplifiedMesh copy_source_mesh(const TerrainBlockMesh& source) {
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

void set_btr_defaults(
    TerrainBlockCompileOptions& options,
    const TerrainBlockPlan& plan) {
    const float block_origin_x = static_cast<float>(plan.x) * 4096.0F;
    const float block_origin_y = static_cast<float>(plan.y) * 4096.0F;
    const float block_span = static_cast<float>(plan.lod_level) * 4096.0F;
    options.btr.shape_transform.translation_x = 0.0F;
    options.btr.shape_transform.translation_y = 0.0F;
    options.btr.shape_transform.translation_z = 0.0F;
    options.btr.shape_transform.scale = 1.0F;
    options.btr.uv_origin_x = block_origin_x;
    options.btr.uv_origin_y = block_origin_y;
    options.btr.uv_span_x = block_span;
    options.btr.uv_span_y = block_span;
    options.btr.shape_name = "Land";
    options.btr.use_recovered_terrain_block_space = true;
    options.btr.lod_level = plan.lod_level;
    options.btr.block_origin_x = block_origin_x;
    options.btr.block_origin_y = block_origin_y;
    options.btr.z_shift = 0.0F;
    options.btr.reserved_string_count = 2U; // "chunk", "Land"
}

} // namespace

TerrainBlockCompileOptions terrain_block_options_from_level_profile(
    const std::string& worldspace_editor_id,
    const TerrainBlockPlan& plan,
    const TerrainLodLevelProfile& level) {
    TerrainBlockCompileOptions options;
    options.use_xlodgen_quality = true;
    options.quality = level.quality;
    options.max_vertices = level.max_vertices;
    options.max_triangles = level.max_triangles;
    options.lock_cell_borders = level.protect_cell_borders;
    options.regularize = level.regularize;
    options.water_delta = level.water_delta;
    options.triangle_budget = 65536U;
    options.target_error = 0.01F;
    set_btr_defaults(options, plan);
    (void) worldspace_editor_id;
    return options;
}

TerrainBlockCompileOptions default_v01_terrain_block_options(
    const std::string& worldspace_editor_id,
    const TerrainBlockPlan& plan) {
    const auto profile = default_v01_terrain_lod_profile();
    if (const auto* level = find_terrain_lod_level(profile, plan.lod_level)) {
        return terrain_block_options_from_level_profile(
            worldspace_editor_id, plan, *level);
    }

    TerrainBlockCompileOptions fallback;
    set_btr_defaults(fallback, plan);
    (void) worldspace_editor_id;
    return fallback;
}

TerrainBlockCompileResult compile_terrain_block_in_memory(
    const TerrainWorld& world,
    const TerrainBlockPlan& plan,
    const std::string& worldspace_editor_id,
    const TerrainBlockCompileOptions& options) {
    TerrainBlockCompileResult result;
    result.relative_output_path = terrain_btr_path(
        worldspace_editor_id, plan.lod_level, plan.x, plan.y);
    result.requested_quality = options.quality;
    result.water_delta = options.water_delta;
    result.skirts = options.skirts;

    if (worldspace_editor_id.empty()) {
        result.error = "Terrain block compilation requires a worldspace EditorID.";
        return result;
    }

    const auto source = build_terrain_block_source_mesh(world, plan);
    if (!source.valid) {
        result.error = "Could not assemble terrain block source mesh: " + source.error;
        return result;
    }
    result.source_vertices = source.vertices.size();
    result.source_triangles = source.indices.size() / 3U;

    if (options.use_xlodgen_quality) {
        TerrainQualityOptions quality;
        quality.quality = options.quality;
        quality.max_vertices = options.max_vertices;
        quality.max_triangles = options.max_triangles;
        quality.protect_cell_borders = options.lock_cell_borders;
        quality.regularize = options.regularize;
        quality.water_delta = options.water_delta;
        quality.skirts = options.skirts;

        auto quality_result = simplify_terrain_xlodgen_quality(source, quality);
        if (!quality_result.valid) {
            result.error = "Could not simplify terrain using recovered LODGen Quality/Optimize Unseen behavior: " +
                           quality_result.error;
            return result;
        }
        result.simplification_error = quality_result.effective_error_quad_units;
        result.simplification_error_world_units = quality_result.effective_error_world_units;
        result.forced_by_limits = quality_result.forced_by_limits;
        result.optimize_unseen = quality_result.optimize_unseen;
        result.shifted_underwater_vertices = quality_result.shifted_underwater_vertices;
        result.underwater_triangles = quality_result.underwater_triangles;
        result.greedy_insertions = quality_result.greedy_insertions;
        result.mesh = std::move(quality_result.mesh);

        TerrainSkirtOptions skirt_options;
        skirt_options.lod_level = plan.lod_level;
        skirt_options.block_origin_x = static_cast<float>(plan.x) * 4096.0F;
        skirt_options.block_origin_y = static_cast<float>(plan.y) * 4096.0F;
        skirt_options.skirts = options.skirts;
        auto skirted = add_recovered_terrain_skirts(result.mesh, skirt_options);
        if (!skirted.valid) {
            result.error = "Could not add recovered terrain skirts: " + skirted.error;
            return result;
        }
        result.skirt_vertices = skirted.added_vertices;
        result.skirt_triangles = skirted.added_triangles;
        result.mesh = std::move(skirted.mesh);
    } else {
        if (options.triangle_budget == 0U) {
            result.error = "Terrain block triangle budget must be greater than zero.";
            return result;
        }
        if (!std::isfinite(options.target_error) || options.target_error < 0.0F || options.target_error > 1.0F) {
            result.error = "Terrain block target error must be finite and between 0 and 1.";
            return result;
        }

        if (source.vertices.size() <= options.max_vertices &&
            result.source_triangles <= options.triangle_budget) {
            result.mesh = copy_source_mesh(source);
        } else {
            TerrainSimplifyOptions simplify;
            simplify.target_triangles = std::min(options.triangle_budget, result.source_triangles);
            simplify.target_error = options.target_error;
            simplify.lock_outer_border = true;
            simplify.lock_cell_borders = options.lock_cell_borders;
            simplify.regularize = options.regularize;
            simplify.max_vertices = options.max_vertices;
            result.mesh = simplify_terrain_block(source, simplify);
            if (!result.mesh.valid) {
                result.error = "Could not simplify terrain block within configured legacy limits: " + result.mesh.error;
                return result;
            }
        }
        result.simplification_error = result.mesh.result_error;
        result.simplification_error_world_units = result.mesh.result_error;
    }

    result.output_vertices = result.mesh.vertices.size();
    result.output_triangles = result.mesh.indices.size() / 3U;
    // MaxVerts is the recovered greedy point target, not a post-generation
    // serialization ceiling. GoalMet permits one final insertion before
    // PointCount > MaxVerts stops refinement. Keep the configured limit as an
    // absolute ceiling only for the unrelated legacy simplifier.
    if (!options.use_xlodgen_quality && result.output_vertices > options.max_vertices) {
        result.error = "Legacy terrain simplification exceeded its configured vertex ceiling.";
        return result;
    }
    if (result.output_vertices > 65535U) {
        result.error = "Terrain block requires 65535-vertex BTR chunk splitting that is not enabled yet.";
        return result;
    }
    if (result.output_triangles > options.max_triangles) {
        result.error = "Terrain block compiler exceeded the configured terrain triangle safety ceiling.";
        return result;
    }

    result.document = build_fallout4_terrain_btr_document(result.mesh, options.btr);
    if (!result.document.valid) {
        result.error = "Could not build Fallout 4 BTR document: " + result.document.error;
        return result;
    }

    result.renderable = result.document.renderable;
    result.valid = true;
    return result;
}

} // namespace f4lod
