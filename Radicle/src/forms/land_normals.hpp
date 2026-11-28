#pragma once

#include "fo4/land/heightmap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::land {

inline constexpr std::size_t k_normals_size = 3 * k_heightmap_vertices;
inline constexpr double k_vertex_spacing = 128.0;

using HeightGrid = std::array<float, k_heightmap_vertices>;
using ChangedVertices = std::array<bool, k_heightmap_vertices>;

struct BorderHeights {
  const HeightGrid* east{nullptr};
  const HeightGrid* west{nullptr};
  const HeightGrid* north{nullptr};
  const HeightGrid* south{nullptr};
  const HeightGrid* north_east{nullptr};
  const HeightGrid* north_west{nullptr};
  const HeightGrid* south_east{nullptr};
  const HeightGrid* south_west{nullptr};
};

// Precondition: heights holds 1089 finite world heights in the row-major order of land_heights (row = y, column = x).
// A missing border or corner is padded by linear extrapolation of the cell's own heights; the result follows the CK's triangle formula but is not bit-identical.
std::array<std::int8_t, k_normals_size> compute_land_normals(std::span<const float> heights,
                                                             const BorderHeights& borders = {});

// Constraint: a vertex is flagged when a padded height of any triangle it belongs to differs between the two states (a vertex off the cell's diagonal does not read the opposite corner).
ChangedVertices changed_normal_vertices(std::span<const float> before,
                                        const BorderHeights& before_borders,
                                        std::span<const float> after,
                                        const BorderHeights& after_borders);

}  // namespace fo4::land
