#include "f4lod/terrain_preview.hpp"

#include "f4lod/terrain_compile.hpp"

#include <algorithm>
#include <utility>

namespace f4lod {

TerrainGenerationPreview preview_terrain_generation(
    const TerrainGenerationPlan& plan,
    const TerrainLodProfile& profile) {
    TerrainGenerationPreview preview;
    preview.worldspace_editor_id = plan.worldspace_editor_id;
    preview.profile_name = profile.name;

    if (!plan.valid) {
        preview.error = "Cannot preview an invalid terrain generation plan: " + plan.error;
        return preview;
    }
    if (plan.levels.empty()) {
        preview.error = "Terrain generation plan contains no LOD levels.";
        return preview;
    }

    const auto profile_validation = validate_terrain_lod_profile(profile);
    preview.profile_diagnostics = profile_validation.diagnostics;
    if (!profile_validation.valid) {
        preview.error = "Terrain LOD profile contains blocking validation errors.";
        return preview;
    }

    preview.levels.reserve(plan.levels.size());
    for (const auto& level : plan.levels) {
        const auto* level_profile = find_terrain_lod_level(profile, level.lod_level);
        if (!level_profile) {
            preview.error = "Terrain LOD profile does not contain LOD " +
                            std::to_string(level.lod_level) + '.';
            return preview;
        }

        TerrainLevelPreview level_preview;
        level_preview.lod_level = level.lod_level;
        level_preview.requested_quality = level_profile->quality;
        level_preview.water_delta = level_profile->water_delta;
        level_preview.protect_cell_borders = level_profile->protect_cell_borders;
        level_preview.blocks.reserve(level.blocks.size());

        for (const auto& block : level.blocks) {
            const auto options = terrain_block_options_from_level_profile(
                plan.worldspace_editor_id, block, *level_profile);
            const auto compiled = compile_terrain_block_in_memory(
                plan.terrain, block, plan.worldspace_editor_id, options);
            if (!compiled.valid) {
                preview.error = "LOD " + std::to_string(level.lod_level) +
                                " block " + std::to_string(block.x) + "," +
                                std::to_string(block.y) + " failed compile preview: " + compiled.error;
                return preview;
            }

            TerrainBlockPreview block_preview;
            block_preview.lod_level = level.lod_level;
            block_preview.block_x = block.x;
            block_preview.block_y = block.y;
            block_preview.relative_output_path = compiled.relative_output_path;
            block_preview.source_vertices = compiled.source_vertices;
            block_preview.source_triangles = compiled.source_triangles;
            block_preview.output_vertices = compiled.output_vertices;
            block_preview.output_triangles = compiled.output_triangles;
            block_preview.requested_quality = compiled.requested_quality;
            block_preview.simplification_error = compiled.simplification_error;
            block_preview.simplification_error_world_units = compiled.simplification_error_world_units;
            block_preview.greedy_insertions = compiled.greedy_insertions;
            block_preview.forced_by_limits = compiled.forced_by_limits;
            block_preview.optimize_unseen = compiled.optimize_unseen;
            block_preview.water_delta = compiled.water_delta;
            block_preview.shifted_underwater_vertices = compiled.shifted_underwater_vertices;
            block_preview.underwater_triangles = compiled.underwater_triangles;
            block_preview.renderable = compiled.renderable;

            level_preview.source_vertices += block_preview.source_vertices;
            level_preview.source_triangles += block_preview.source_triangles;
            level_preview.output_vertices += block_preview.output_vertices;
            level_preview.output_triangles += block_preview.output_triangles;
            level_preview.greedy_insertions += block_preview.greedy_insertions;
            level_preview.shifted_underwater_vertices += block_preview.shifted_underwater_vertices;
            level_preview.underwater_triangles += block_preview.underwater_triangles;
            level_preview.max_simplification_error = std::max(
                level_preview.max_simplification_error,
                block_preview.simplification_error);
            level_preview.max_simplification_error_world_units = std::max(
                level_preview.max_simplification_error_world_units,
                block_preview.simplification_error_world_units);
            if (block_preview.forced_by_limits) {
                ++level_preview.forced_blocks;
                ++preview.forced_blocks;
            }
            if (block_preview.optimize_unseen) {
                ++level_preview.optimize_unseen_blocks;
                ++preview.optimize_unseen_blocks;
            }
            preview.greedy_insertions += block_preview.greedy_insertions;
            preview.shifted_underwater_vertices += block_preview.shifted_underwater_vertices;
            if (block_preview.renderable) ++preview.renderable_blocks;
            ++preview.total_blocks;
            level_preview.blocks.push_back(std::move(block_preview));
        }

        preview.levels.push_back(std::move(level_preview));
    }

    if (preview.total_blocks != plan.total_block_count()) {
        preview.error = "Terrain compile preview block count does not match the generation plan.";
        return preview;
    }

    preview.valid = true;
    return preview;
}

TerrainGenerationPreview preview_terrain_generation(
    const TerrainGenerationPlan& plan) {
    return preview_terrain_generation(plan, default_v01_terrain_lod_profile());
}

} // namespace f4lod
