#include "f4lod/terrain_job.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace f4lod {

std::size_t TerrainGenerationPlan::total_block_count() const noexcept {
    std::size_t count = 0;
    for (const auto& level : levels) count += level.blocks.size();
    return count;
}

TerrainGenerationPlan make_terrain_generation_plan(
    TerrainWorld terrain,
    std::string worldspace_editor_id,
    const AssetResolver& assets) {
    TerrainGenerationPlan result;
    result.worldspace_editor_id = std::move(worldspace_editor_id);
    result.worldspace = terrain.worldspace;
    result.land_selection = terrain.land_selection;
    result.terrain = std::move(terrain);

    if (result.worldspace_editor_id.empty()) {
        result.error = "Terrain generation requires a worldspace EditorID.";
        return result;
    }
    if (!result.terrain.complete) {
        result.error = "Terrain world contains blocking record or LAND decoding diagnostics.";
        return result;
    }
    if (!result.terrain.seam_issues.empty()) {
        result.error = "Terrain world contains mismatched LAND borders; generation is blocked to prevent visible cracks.";
        return result;
    }
    if (!result.terrain.has_bounds || result.terrain.cells.empty()) {
        result.error = "Terrain world contains no decoded exterior LAND cells.";
        return result;
    }

    const std::string lod_path = "lodsettings/" + result.worldspace_editor_id + ".lod";
    const auto lod_asset = assets.read(lod_path);
    if (!lod_asset.valid) {
        result.error = "Could not resolve " + lod_path + ": " + lod_asset.error;
        return result;
    }
    result.lod_settings_source = lod_asset.source;
    result.lod_settings = parse_fallout4_lod_settings(
        std::span<const unsigned char>(lod_asset.data.data(), lod_asset.data.size()));
    if (!result.lod_settings.valid) {
        result.error = "Invalid Fallout 4 LOD settings for " + result.worldspace_editor_id + ": " +
                       result.lod_settings.error;
        return result;
    }

    constexpr std::array<std::uint32_t, 4> levels{4U, 8U, 16U, 32U};
    for (const auto level : levels) {
        if (!result.lod_settings.supports_level(level)) continue;

        auto blocks = plan_terrain_blocks(
            result.terrain,
            level,
            result.lod_settings.southwest_x,
            result.lod_settings.southwest_y);

        std::size_t covered_cells = 0;
        for (const auto& block : blocks) covered_cells += block.cells.size();
        if (covered_cells != result.terrain.cells.size()) {
            result.error = "LOD settings southwest anchor does not cover every decoded terrain cell at level " +
                           std::to_string(level) + '.';
            return result;
        }
        if (blocks.empty()) {
            result.error = "LOD level " + std::to_string(level) + " produced no terrain blocks.";
            return result;
        }

        result.levels.push_back(TerrainGenerationLevel{level, std::move(blocks)});
    }

    if (result.levels.empty()) {
        result.error = "LOD settings do not enable any v0.1 terrain levels (4, 8, 16, 32).";
        return result;
    }

    result.valid = true;
    return result;
}

TerrainGenerationPlan prepare_terrain_generation_plan(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const AssetResolver& assets,
    const std::string& worldspace_editor_id,
    TerrainLandSelection land_selection) {
    TerrainGenerationPlan result;
    result.worldspace_editor_id = worldspace_editor_id;
    result.land_selection = land_selection;

    if (!catalog.complete) {
        result.error = "Worldspace catalog is incomplete; terrain generation cannot safely continue.";
        return result;
    }
    const auto* worldspace = find_worldspace_by_editor_id(catalog, worldspace_editor_id);
    if (!worldspace) {
        result.error = "Worldspace EditorID not found: " + worldspace_editor_id;
        return result;
    }

    auto terrain = build_terrain_world(graph, catalog, worldspace->key, 0.01F, land_selection);
    return make_terrain_generation_plan(std::move(terrain), worldspace->editor_id, assets);
}

} // namespace f4lod
