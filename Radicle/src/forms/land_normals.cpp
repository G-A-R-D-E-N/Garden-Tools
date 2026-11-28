#include "land_normals.hpp"

#include <bit>
#include <cmath>

namespace fo4::land {
namespace {

constexpr std::size_t k_side = k_heightmap_side;
constexpr std::size_t k_padded = k_side + 2;

using Padded = std::array<std::array<float, k_padded>, k_padded>;

struct Corner {
  std::size_t row;
  std::size_t column;
};
using Triangle = std::array<Corner, 3>;

constexpr std::array<std::array<Triangle, 2>, 2> k_triangles = {{
    {{{{{1, 1}, {1, 0}, {0, 0}}}, {{{0, 0}, {0, 1}, {1, 1}}}}},
    {{{{{1, 0}, {0, 0}, {0, 1}}}, {{{0, 1}, {1, 1}, {1, 0}}}}},
}};

Padded pad_heights(std::span<const float> heights, const BorderHeights& borders) {
  Padded grid{};
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      grid[row + 1][column + 1] = heights[row * k_side + column];
    }
  }
  for (std::size_t row = 1; row <= k_side; ++row) {
    grid[row][0] = 2.0f * grid[row][1] - grid[row][2];
    grid[row][k_side + 1] = 2.0f * grid[row][k_side] - grid[row][k_side - 1];
  }
  for (std::size_t column = 0; column < k_padded; ++column) {
    grid[0][column] = 2.0f * grid[1][column] - grid[2][column];
    grid[k_side + 1][column] = 2.0f * grid[k_side][column] - grid[k_side - 1][column];
  }
  for (std::size_t index = 0; index < k_side; ++index) {
    if (borders.east != nullptr) grid[index + 1][k_side + 1] = (*borders.east)[index * k_side + 1];
    if (borders.west != nullptr) grid[index + 1][0] = (*borders.west)[index * k_side + k_side - 2];
    if (borders.north != nullptr) grid[k_side + 1][index + 1] = (*borders.north)[k_side + index];
    if (borders.south != nullptr) grid[0][index + 1] = (*borders.south)[(k_side - 2) * k_side + index];
  }
  if (borders.north_east != nullptr) grid[k_side + 1][k_side + 1] = (*borders.north_east)[k_side + 1];
  if (borders.north_west != nullptr) grid[k_side + 1][0] = (*borders.north_west)[k_side + k_side - 2];
  if (borders.south_east != nullptr) grid[0][k_side + 1] = (*borders.south_east)[(k_side - 2) * k_side + 1];
  if (borders.south_west != nullptr) grid[0][0] = (*borders.south_west)[(k_side - 2) * k_side + k_side - 2];
  return grid;
}

// Constraint: every padded cell, the outer ring included, runs through here; (row ^ column) & 1 picks the checkerboard diagonal.
template <typename Visit>
void for_each_triangle(Visit&& visit) {
  for (std::size_t row = 0; row <= k_side; ++row) {
    for (std::size_t column = 0; column <= k_side; ++column) {
      for (const auto& triangle : k_triangles[(row ^ column) & 1]) visit(row, column, triangle);
    }
  }
}

const std::array<std::uint32_t, 256>& sqrt_table() {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 128; ++i) {
      t[i] = std::bit_cast<std::uint32_t>(std::sqrt(std::bit_cast<float>(0x3f800000u | (i << 16)))) & 0x7fffffu;
      t[128 + i] = std::bit_cast<std::uint32_t>(std::sqrt(std::bit_cast<float>(0x40000000u | (i << 16)))) & 0x7fffffu;
    }
    return t;
  }();
  return table;
}

// Constraint: value must be positive and finite; the result is up to 0.39 % below the true root, see docs/code-notes/Radicle.md.
float sqrt_approx(float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto exponent = bits >> 23;
  const auto top = (bits & 0x7fffffu) >> 16;
  const auto index = ((exponent - 1) & 1) == 0 ? top : 0x80 + top;
  const auto scale = ((exponent << 22) - 0x1FC00000u) & 0xff800000u;
  return std::bit_cast<float>((0x3f800000u + scale) | sqrt_table()[index]);
}

std::int8_t to_byte(float value) {
  return static_cast<std::int8_t>(static_cast<int>(value * 127.0f));
}

}  // namespace

std::array<std::int8_t, k_normals_size> compute_land_normals(std::span<const float> heights,
                                                             const BorderHeights& borders) {
  const auto grid = pad_heights(heights, borders);
  std::array<std::array<std::array<float, 3>, k_padded>, k_padded> sum{};
  constexpr float spacing = static_cast<float>(k_vertex_spacing);
  for_each_triangle([&](std::size_t row, std::size_t column, const Triangle& triangle) {
    float p[3][3];
    for (std::size_t k = 0; k < 3; ++k) {
      p[k][0] = static_cast<float>(triangle[k].column) * spacing;
      p[k][1] = static_cast<float>(triangle[k].row) * spacing;
      p[k][2] = grid[row + triangle[k].row][column + triangle[k].column];
    }
    const float d1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
    const float d2[3] = {p[2][0] - p[1][0], p[2][1] - p[1][1], p[2][2] - p[1][2]};
    const float f[3] = {d2[2] * d1[1] - d2[1] * d1[2], d2[0] * d1[2] - d2[2] * d1[0], d2[1] * d1[0] - d2[0] * d1[1]};
    const float scale = 1.0f / sqrt_approx((f[0] * f[0] + f[1] * f[1]) + f[2] * f[2]);
    for (const auto& corner : triangle) {
      auto& target = sum[row + corner.row][column + corner.column];
      for (std::size_t axis = 0; axis < 3; ++axis) target[axis] += f[axis] * scale;
    }
  });

  std::array<std::int8_t, k_normals_size> out{};
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      const auto& v = sum[row + 1][column + 1];
      const float scale = 1.0f / sqrt_approx((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
      const auto base = (row * k_side + column) * 3;
      for (std::size_t axis = 0; axis < 3; ++axis) out[base + axis] = to_byte(v[axis] * scale);
    }
  }
  return out;
}

ChangedVertices changed_normal_vertices(std::span<const float> before,
                                        const BorderHeights& before_borders,
                                        std::span<const float> after,
                                        const BorderHeights& after_borders) {
  const auto old_grid = pad_heights(before, before_borders);
  const auto new_grid = pad_heights(after, after_borders);
  ChangedVertices changed{};
  for_each_triangle([&](std::size_t row, std::size_t column, const Triangle& triangle) {
    bool differs = false;
    for (const auto& corner : triangle) {
      differs = differs || old_grid[row + corner.row][column + corner.column] != new_grid[row + corner.row][column + corner.column];
    }
    if (!differs) return;
    for (const auto& corner : triangle) {
      const auto padded_row = row + corner.row;
      const auto padded_column = column + corner.column;
      if (padded_row >= 1 && padded_row <= k_side && padded_column >= 1 && padded_column <= k_side) {
        changed[(padded_row - 1) * k_side + padded_column - 1] = true;
      }
    }
  });
  return changed;
}

}  // namespace fo4::land
