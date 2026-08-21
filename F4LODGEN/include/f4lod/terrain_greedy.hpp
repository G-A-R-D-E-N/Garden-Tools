#pragma once

#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <string>

namespace f4lod {

struct TerrainGreedyOptions {
    // xLODGen/LODGen contract. -1 is handled by the caller as full geometry;
    // this reducer accepts non-negative thresholds only.
    int quality{};
    std::size_t point_limit{32767U};
    bool protect_cell_borders{};
};

struct TerrainGreedyResult {
    bool valid{};
    std::string error;
    float threshold_quad_units{};
    float max_error_quad_units{};
    float max_error_world_units{};
    bool point_limit_reached{};
    std::size_t selected_points{};
    std::size_t greedy_insertions{};
    TerrainSimplifiedMesh mesh;
};

// Clean-room implementation of the recovered LODGen terrain contract:
// - regular heightfield
// - Z is evaluated in quad-local units (world Z / LOD level)
// - start from constrained outer borders
// - repeatedly insert the grid sample with maximum |height - triangle plane|
// - restore local Delaunay legality after insertion
// - stop when MaxError < Quality or PointCount reaches MaxVerts
//
// For LOD4 + ProtectCellBorders, each 33x33 CELL region is reduced separately
// at Quality/4 and stitched by shared source-grid indices, matching the
// recovered GenerateTerrain/AddScaled strategy.
[[nodiscard]] TerrainGreedyResult reduce_terrain_greedy(
    const TerrainBlockMesh& source,
    const TerrainGreedyOptions& options);

} // namespace f4lod
