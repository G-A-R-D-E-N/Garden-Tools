#pragma once

#include "f4lod/terrain_block_mesh.hpp"

#include <cstddef>
#include <string>

namespace f4lod {

struct TerrainUnseenOptions {
    // xLODGen WaterDelta semantics:
    // -1 = off, 0 = optimize submerged-only geometry, >0 = also lower
    // shallow submerged vertices to at least this distance below water.
    float water_delta{-1.0F};
    bool protect_cell_borders{false};
    bool regularize{false};
};

struct TerrainUnseenResult {
    bool valid{};
    std::string error;
    bool enabled{};
    std::size_t shifted_vertices{};
    std::size_t underwater_triangles{};
    std::size_t source_triangles{};
    std::size_t output_triangles{};
    TerrainBlockMesh mesh;
};

[[nodiscard]] TerrainUnseenResult optimize_unseen_terrain(
    const TerrainBlockMesh& source,
    const TerrainUnseenOptions& options);

} // namespace f4lod
