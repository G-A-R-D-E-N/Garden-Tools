#pragma once

#include "f4lod/terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

inline constexpr float kExteriorCellSize = 4096.0F;
inline constexpr float kLandscapeVertexSpacing = kExteriorCellSize / 32.0F;

struct TerrainVertex {
    float x{};
    float y{};
    float z{};
};

struct TerrainMesh {
    bool valid{};
    std::string error;
    std::uint32_t sample_stride{};
    std::size_t vertices_per_side{};
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] TerrainMesh build_terrain_cell_mesh(
    const TerrainCell& cell,
    std::uint32_t sample_stride = 1);

} // namespace f4lod
