#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fo4::land {

inline constexpr std::size_t k_heightmap_side = 33;
inline constexpr std::size_t k_heightmap_vertices = k_heightmap_side * k_heightmap_side;
inline constexpr std::size_t k_vhgt_size = 4 + k_heightmap_vertices + 3;

struct LandHeightmap {
  float offset{0.0f};
  std::array<std::int8_t, k_heightmap_vertices> deltas{};
  std::array<std::byte, 3> trailing_bytes{};

  friend bool operator==(const LandHeightmap&, const LandHeightmap&) = default;
};

LandHeightmap decode_land_heightmap(std::span<const std::byte> vhgt);

std::array<std::byte, k_vhgt_size> encode_land_heightmap(const LandHeightmap& heightmap);

std::array<float, k_heightmap_vertices> land_heights(const LandHeightmap& heightmap);

std::optional<LandHeightmap> make_land_heightmap(std::span<const float> heights, float offset);

}  // namespace fo4::land
