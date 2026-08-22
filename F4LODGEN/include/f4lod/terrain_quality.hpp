#pragma once

#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace f4lod {

struct TerrainQualityOptions {
    int quality{0};
    // Reference Program.cs caps configured MaxVerts at the NIF ushort index
    // range. The normal default remains 32767.
    std::size_t max_vertices{32767U};
    std::size_t max_triangles{std::numeric_limits<std::uint32_t>::max()};
    bool protect_cell_borders{false};
    bool regularize{false};
    float water_delta{-1.0F};
    // TerrainLOD reserves 2000 reducer points for any non-zero skirt setting,
    // with a floor of 512 points. FO4's GUI writes Skirts=256.
    float skirts{256.0F};
};

struct TerrainQualityResult {
    bool valid{};
    std::string error;
    int requested_quality{};
    float effective_error{};
    float effective_error_quad_units{};
    float effective_error_world_units{};
    bool full_geometry{};
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
