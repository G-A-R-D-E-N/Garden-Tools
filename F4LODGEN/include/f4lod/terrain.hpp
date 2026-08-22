#pragma once

#include "f4lod/landscape.hpp"
#include "f4lod/record_graph.hpp"
#include "f4lod/worldspace.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

enum class TerrainLandSelection {
    winning,
    first_candidate,
};

struct TerrainCell {
    FormKey cell;
    FormKey land;
    std::string land_source_plugin;
    std::int32_t x{};
    std::int32_t y{};
    bool has_water{};
    float water_height{};
    LandscapeHeightmap heightmap;
};

struct TerrainDiagnostic {
    std::string code;
    std::string message;
    FormKey record;
};

struct TerrainSeamIssue {
    FormKey first_cell;
    FormKey second_cell;
    char axis{'x'};
    std::size_t mismatched_samples{};
    float max_error{};
};

struct TerrainWorld {
    bool complete{true};
    FormKey worldspace;
    TerrainLandSelection land_selection{TerrainLandSelection::winning};
    // xLODGen/LODGen uses these values when an exterior CELL has no exported
    // terrain-data record. Keep them on the world model so block assembly can
    // synthesize a complete regular heightfield instead of leaving holes.
    float default_land_height{-2048.0F};
    float default_water_height{};
    bool has_bounds{};
    std::int32_t min_x{};
    std::int32_t min_y{};
    std::int32_t max_x{};
    std::int32_t max_y{};
    std::vector<TerrainCell> cells;
    std::vector<TerrainDiagnostic> diagnostics;
    std::vector<TerrainSeamIssue> seam_issues;
};

[[nodiscard]] TerrainWorld build_terrain_world(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const FormKey& worldspace,
    float seam_tolerance = 0.01F,
    TerrainLandSelection land_selection = TerrainLandSelection::winning);

[[nodiscard]] const char* terrain_land_selection_name(TerrainLandSelection selection) noexcept;

} // namespace f4lod
