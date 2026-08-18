#pragma once

#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace f4lod {

struct TerrainSkirtOptions {
    std::uint32_t lod_level{};
    float block_origin_x{};
    float block_origin_y{};
    // Recovered TerrainLOD semantics: 0 disables skirts; positive values are
    // relative quad-local depths with a 63-unit decay per LOD index; negative
    // values specify an absolute world-space Z after the reference /LOD + node
    // scale pair cancels out.
    float skirts{256.0F};
};

struct TerrainSkirtResult {
    bool valid{};
    std::string error;
    std::size_t boundary_edges{};
    std::size_t added_vertices{};
    std::size_t added_triangles{};
    float effective_local_depth{};
    TerrainSimplifiedMesh mesh;
};

[[nodiscard]] TerrainSkirtResult add_recovered_terrain_skirts(
    const TerrainSimplifiedMesh& source,
    const TerrainSkirtOptions& options);

[[nodiscard]] std::size_t recovered_terrain_reducer_budget(
    std::size_t configured_max_vertices,
    float skirts) noexcept;

} // namespace f4lod
