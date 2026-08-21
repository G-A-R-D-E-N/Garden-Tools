#include "f4lod/terrain_mesh.hpp"

#include <limits>

namespace f4lod {
namespace {

bool valid_stride(std::uint32_t stride) noexcept {
    return stride != 0 && stride <= 32 && (32U % stride) == 0;
}

} // namespace

TerrainMesh build_terrain_cell_mesh(
    const TerrainCell& cell,
    std::uint32_t sample_stride) {
    TerrainMesh mesh;
    mesh.sample_stride = sample_stride;

    if (!cell.heightmap.valid) {
        mesh.error = "Terrain cell does not contain a valid heightmap.";
        return mesh;
    }
    if (!valid_stride(sample_stride)) {
        mesh.error = "Sample stride must be a non-zero divisor of 32.";
        return mesh;
    }

    mesh.vertices_per_side = static_cast<std::size_t>(32U / sample_stride + 1U);
    const auto vertex_count = mesh.vertices_per_side * mesh.vertices_per_side;
    if (vertex_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        mesh.error = "Terrain mesh vertex count exceeds 32-bit index range.";
        return mesh;
    }

    mesh.vertices.reserve(vertex_count);
    const float origin_x = static_cast<float>(cell.x) * kExteriorCellSize;
    const float origin_y = static_cast<float>(cell.y) * kExteriorCellSize;

    for (std::uint32_t sample_y = 0; sample_y <= 32U; sample_y += sample_stride) {
        for (std::uint32_t sample_x = 0; sample_x <= 32U; sample_x += sample_stride) {
            mesh.vertices.push_back(TerrainVertex{
                origin_x + static_cast<float>(sample_x) * kLandscapeVertexSpacing,
                origin_y + static_cast<float>(sample_y) * kLandscapeVertexSpacing,
                cell.heightmap.at(sample_x, sample_y),
            });
        }
    }

    const auto quads_per_side = mesh.vertices_per_side - 1;
    mesh.indices.reserve(quads_per_side * quads_per_side * 6);
    for (std::size_t y = 0; y < quads_per_side; ++y) {
        for (std::size_t x = 0; x < quads_per_side; ++x) {
            const auto bottom_left = static_cast<std::uint32_t>(y * mesh.vertices_per_side + x);
            const auto bottom_right = bottom_left + 1U;
            const auto top_left = bottom_left + static_cast<std::uint32_t>(mesh.vertices_per_side);
            const auto top_right = top_left + 1U;

            mesh.indices.push_back(bottom_left);
            mesh.indices.push_back(bottom_right);
            mesh.indices.push_back(top_right);

            mesh.indices.push_back(bottom_left);
            mesh.indices.push_back(top_right);
            mesh.indices.push_back(top_left);
        }
    }

    mesh.valid = true;
    return mesh;
}

} // namespace f4lod
