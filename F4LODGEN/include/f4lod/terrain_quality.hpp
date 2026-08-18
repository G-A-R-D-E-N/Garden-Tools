#pragma once

#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <string>

namespace f4lod {

struct TerrainQualityOptions {
    // Recovered LODGen semantics: -1 = full LOD4 geometry. For 0..N, Quality
    // is the greedy triangulator's maximum Z deviation in quad-local units.
    // The heightfield is scaled by 1/LOD before evaluation, so normal world
    // tolerance is Quality * LOD. With LOD4 ProtectCellBorders, the engine
    // divides the threshold by four while reducing each CELL separately.
    int quality{0};
    std::size_t max_vertices{32767};
    std::size_t max_triangles{65535};
    bool protect_cell_borders{false};
    bool regularize{false};
    // WaterDelta is applied to the regular heightfield before greedy Quality.
    float water_delta{-1.0F};
    // TerrainLOD reserves 2000 reducer points for any non-zero skirt setting,
    // with a floor of 512 points. FO4's GUI writes Skirts=256.
    float skirts{256.0F};
};

struct TerrainQualityResult {
    bool valid{};
    std::string error;
    int requested_quality{};
    // Compatibility alias: effective_error is quad-local, matching LODGen.
    float effective_error{};
    float effective_error_quad_units{};
    float effective_error_world_units{};
    bool full_geometry{};
    // True when the recovered reducer target stops greedy insertion before
    // Quality is met. The target may be lower than max_vertices when skirts
    // reserve 2000 points.
    bool forced_by_limits{};
    bool optimize_unseen{};
    float water_delta{-1.0F};
    std::size_t shifted_underwater_vertices{};
    std::size_t underwater_triangles{};
    std::size_t greedy_insertions{};
    TerrainSimplifiedMesh mesh;
};

[[nodiscard]] TerrainQualityResult simplify_terrain_xlodgen_quality(
    const TerrainBlockMesh& source,
    const TerrainQualityOptions& options);

} // namespace f4lod
