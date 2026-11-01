#include "fo4/land/heightmap.hpp"

#include "fo4/error.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace fo4::land {

LandHeightmap decode_land_heightmap(std::span<const std::byte> vhgt) {
  if (vhgt.size() != k_vhgt_size) {
    throw parse_error(0, "vertex height map is not 1096 bytes");
  }
  LandHeightmap heightmap;
  std::memcpy(&heightmap.offset, vhgt.data(), sizeof(heightmap.offset));
  if (!std::isfinite(heightmap.offset)) {
    throw parse_error(0, "vertex height map offset is not finite");
  }
  for (std::size_t index = 0; index < k_heightmap_vertices; ++index) {
    heightmap.deltas[index] =
        static_cast<std::int8_t>(std::to_integer<std::uint8_t>(vhgt[4 + index]));
  }
  for (std::size_t index = 0; index < heightmap.trailing_bytes.size(); ++index) {
    heightmap.trailing_bytes[index] = vhgt[4 + k_heightmap_vertices + index];
  }
  return heightmap;
}

std::array<std::byte, k_vhgt_size> encode_land_heightmap(const LandHeightmap& heightmap) {
  std::array<std::byte, k_vhgt_size> out{};
  std::memcpy(out.data(), &heightmap.offset, sizeof(heightmap.offset));
  for (std::size_t index = 0; index < k_heightmap_vertices; ++index) {
    out[4 + index] = static_cast<std::byte>(static_cast<std::uint8_t>(heightmap.deltas[index]));
  }
  for (std::size_t index = 0; index < heightmap.trailing_bytes.size(); ++index) {
    out[4 + k_heightmap_vertices + index] = heightmap.trailing_bytes[index];
  }
  return out;
}

std::array<float, k_heightmap_vertices> land_heights(const LandHeightmap& heightmap) {
  std::array<float, k_heightmap_vertices> heights{};
  float row_start = k_land_height_scale * heightmap.offset;
  for (std::size_t row = 0; row < k_heightmap_side; ++row) {
    float current = row_start;
    for (std::size_t column = 0; column < k_heightmap_side; ++column) {
      current += static_cast<float>(heightmap.deltas[row * k_heightmap_side + column]) * k_land_height_scale;
      heights[row * k_heightmap_side + column] = current;
      if (column == 0) row_start = current;
    }
  }
  return heights;
}

std::optional<LandHeightmap> make_land_heightmap(std::span<const float> heights, float offset) {
  if (heights.size() != k_heightmap_vertices) return std::nullopt;
  if (!std::isfinite(offset)) return std::nullopt;
  for (const auto height : heights) {
    if (!std::isfinite(height)) return std::nullopt;
  }
  LandHeightmap heightmap;
  heightmap.offset = offset;
  std::int64_t row_start = 0;
  for (std::size_t row = 0; row < k_heightmap_side; ++row) {
    std::int64_t previous = row_start;
    for (std::size_t column = 0; column < k_heightmap_side; ++column) {
      const auto index = row * k_heightmap_side + column;
      const double units = static_cast<double>(heights[index]) / k_land_height_scale - static_cast<double>(offset);
      if (std::fabs(units) > 1.0e9) return std::nullopt;
      const std::int64_t total = std::llround(units);
      const std::int64_t delta = total - previous;
      if (delta < -128 || delta > 127) return std::nullopt;
      heightmap.deltas[index] = static_cast<std::int8_t>(delta);
      previous = total;
      if (column == 0) row_start = total;
    }
  }
  return heightmap;
}

}  // namespace fo4::land
