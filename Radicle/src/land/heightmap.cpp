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
  std::int32_t accumulated = 0;
  for (std::size_t index = 0; index < k_heightmap_vertices; ++index) {
    accumulated += static_cast<std::int32_t>(heightmap.deltas[index]);
    heights[index] = heightmap.offset + static_cast<float>(accumulated);
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
  double previous = static_cast<double>(offset);
  for (std::size_t index = 0; index < k_heightmap_vertices; ++index) {
    const auto difference = static_cast<double>(heights[index]) - previous;
    const auto rounded = std::llround(difference);
    if (rounded < -128 || rounded > 127) return std::nullopt;
    heightmap.deltas[index] = static_cast<std::int8_t>(rounded);
    previous = static_cast<double>(heights[index]);
  }
  return heightmap;
}

}  // namespace fo4::land
