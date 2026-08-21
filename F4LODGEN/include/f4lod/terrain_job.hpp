#pragma once

#include "f4lod/assets.hpp"
#include "f4lod/lod_settings.hpp"
#include "f4lod/record_graph.hpp"
#include "f4lod/terrain.hpp"
#include "f4lod/terrain_blocks.hpp"
#include "f4lod/worldspace.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainGenerationLevel {
    std::uint32_t lod_level{};
    std::vector<TerrainBlockPlan> blocks;
};

struct TerrainGenerationPlan {
    bool valid{};
    std::string error;
    std::string worldspace_editor_id;
    FormKey worldspace;
    TerrainLandSelection land_selection{TerrainLandSelection::winning};
    AssetSource lod_settings_source;
    LodSettings lod_settings;
    TerrainWorld terrain;
    std::vector<TerrainGenerationLevel> levels;

    [[nodiscard]] std::size_t total_block_count() const noexcept;
};

[[nodiscard]] TerrainGenerationPlan make_terrain_generation_plan(
    TerrainWorld terrain,
    std::string worldspace_editor_id,
    const AssetResolver& assets);

[[nodiscard]] TerrainGenerationPlan prepare_terrain_generation_plan(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const AssetResolver& assets,
    const std::string& worldspace_editor_id,
    TerrainLandSelection land_selection = TerrainLandSelection::winning);

} // namespace f4lod
