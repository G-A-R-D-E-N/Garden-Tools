#pragma once

// Constraint: a shipped combined object gives each LOD level its own contiguous vertex block, measured on 1200 objects.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fo4::previs {

inline constexpr std::size_t k_previs_lod_levels = 3;

// Constraint: the weld cell follows the mesh mean edge, the only size-free scale that tracks shipped reductions across bands.
inline constexpr float k_previs_lod1_edge_factor = 1.40f;
inline constexpr float k_previs_lod2_edge_factor = 1.20f;

// Constraint: a level keeps a subset of the source vertices, so its vertices are re-packed from the source bytes.
struct LodLevel {
  std::vector<std::uint32_t> kept_vertices;
  std::vector<std::uint16_t> triangles;
};

using LodLevels = std::array<LodLevel, k_previs_lod_levels>;

// Constraint: the object buffer holds one vertex block per level in level order, so a level's indices shift by its block start.
struct PackedLodLevels {
  std::vector<std::byte> vertices;
  std::vector<std::uint16_t> indices;
  std::array<std::vector<std::uint16_t>, k_previs_lod_levels - 1> higher_lod_indices{};
  std::array<std::uint32_t, k_previs_lod_levels> level_vertex_count{};
  std::uint32_t vertex_count{0};
};

// Constraint: a weld cell is a world-space length per level, so a caller can hold the mesh and the rule apart.
struct LodWeldCells {
  float level1{0.0f};
  float level2{0.0f};
};

// Constraint: a shipped object states its ratios as triangle fractions, so a target is the same quantity per level.
struct LodLevelTarget {
  float level1{0.0f};
  float level2{0.0f};
};

// Constraint: the rule scales its cell by the mesh mean edge, so the harness needs the same measurement.
float mean_edge_length(std::span<const float> positions, std::span<const std::uint16_t> indices);

// Constraint: shrinking edges is what a weld removes, so its cell can be read off the edge-length distribution.
float edge_percentile(std::span<const float> positions, std::span<const std::uint16_t> indices, float percentile);

// Constraint: level 0 is the source mesh, so only levels above it are welded on a grid.
LodLevels reduce_mesh_levels(std::span<const float> positions, std::span<const std::uint16_t> indices);

// Constraint: the calibration harness sweeps cells directly, so the rule and the welding stay separable.
LodLevels reduce_mesh_levels_with_cells(std::span<const float> positions, std::span<const std::uint16_t> indices,
                                        const LodWeldCells& cells);

// Constraint: an object's own ratio is exact only through the weld cell, so the target is solved per level.
LodLevels reduce_mesh_levels_to_target(std::span<const float> positions, std::span<const std::uint16_t> indices,
                                       const LodLevelTarget& target);

// Constraint: the ratios a caller asked for are reported back, so a target is checked on the produced levels.
std::array<float, k_previs_lod_levels> lod_triangle_ratios(const LodLevels& levels);

// Constraint: a pinned level is only reproducible with the cell it solved to, so the solve is reported to the caller.
LodWeldCells solve_lod_weld_cells(std::span<const float> positions, std::span<const std::uint16_t> indices,
                                  const LodLevelTarget& target);


// Constraint: u16 indices address at most 65535 vertices, so a large mesh keeps fewer levels rather than failing the cell.
std::optional<PackedLodLevels> pack_lod_levels(const LodLevels& levels, std::span<const std::byte> vertices,
                                               std::size_t stride);

}  // namespace fo4::previs
