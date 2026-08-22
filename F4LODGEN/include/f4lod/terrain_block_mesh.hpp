#pragma once

#include "f4lod/terrain.hpp"
#include "f4lod/terrain_blocks.hpp"
#include "f4lod/terrain_mesh.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainBlockMesh {
    bool valid{};
    std::string error;
    std::uint32_t lod_level{};
    std::int32_t block_x{};
    std::int32_t block_y{};
    float min_height{};
    float max_height{};
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<unsigned char> outer_border;
    std::vector<unsigned char> cell_border;
    // NaN means the terrain sample is not covered by LOD water. Otherwise
    // this is the highest resolved CELL/WRLD water plane touching the sample.
    std::vector<float> vertex_water_height;
};

[[nodiscard]] TerrainBlockMesh build_terrain_block_source_mesh(
    const TerrainWorld& world,
    const TerrainBlockPlan& plan,
    float shared_height_tolerance = 0.01F);

} // namespace f4lod
