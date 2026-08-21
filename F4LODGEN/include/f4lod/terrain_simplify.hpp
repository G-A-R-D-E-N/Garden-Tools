#pragma once

#include "f4lod/terrain_block_mesh.hpp"
#include "f4lod/terrain_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainSimplifyOptions {
    std::size_t target_triangles{};
    float target_error{0.01F};
    bool lock_outer_border{true};
    bool lock_cell_borders{false};
    bool regularize{false};
    std::size_t max_vertices{65535};
};

struct TerrainSimplifiedMesh {
    bool valid{};
    std::string error;
    std::size_t source_vertices{};
    std::size_t source_triangles{};
    std::size_t target_triangles{};
    float result_error{};
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] TerrainSimplifiedMesh simplify_terrain_block(
    const TerrainBlockMesh& source,
    const TerrainSimplifyOptions& options);

} // namespace f4lod
