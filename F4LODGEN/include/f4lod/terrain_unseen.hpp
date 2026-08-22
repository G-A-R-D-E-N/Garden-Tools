#pragma once

#include "f4lod/terrain_block_mesh.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainUnseenOptions {
    // Recovered LODGen WaterDelta semantics:
    // -1 = off; 0 flattens interior underwater samples to the water plane and
    // makes them ineligible for greedy insertion; positive values also lower
    // shallow underwater terrain by WaterDelta and apply the small above-water
    // anti-z-fighting nudge used by the reference engine.
    float water_delta{-1.0F};
    bool protect_cell_borders{false};
    bool regularize{false};
};

struct TerrainUnseenResult {
    bool valid{};
    std::string error;
    bool enabled{};
    std::size_t shifted_vertices{};
    std::size_t raised_vertices{};
    std::size_t underwater_triangles{};
    std::size_t source_triangles{};
    std::size_t output_triangles{};
    // Source-vertex masks consumed by the greedy reducer. Ignored samples are
    // never candidates. Forced samples are pre-inserted to preserve seams.
    std::vector<unsigned char> ignored_vertices;
    std::vector<unsigned char> forced_vertices;
    TerrainBlockMesh mesh;
};

[[nodiscard]] TerrainUnseenResult optimize_unseen_terrain(
    const TerrainBlockMesh& source,
    const TerrainUnseenOptions& options);

} // namespace f4lod
