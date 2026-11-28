#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::land {

inline constexpr std::size_t k_layer_header_size = 8;
inline constexpr std::size_t k_layer_vertex_size = 8;
inline constexpr std::size_t k_land_color_count = 1089;
inline constexpr std::size_t k_land_color_size = k_land_color_count * 3;

struct LandLayerHeader {
  std::uint32_t texture{0};
  std::uint8_t quadrant{0};
  std::uint8_t unused{0};
  std::int16_t layer{0};

  friend bool operator==(const LandLayerHeader&, const LandLayerHeader&) = default;
};

struct LandLayerVertex {
  std::uint16_t position{0};
  std::uint16_t unused{0};
  float opacity{1.0f};

  friend bool operator==(const LandLayerVertex&, const LandLayerVertex&) = default;
};

struct LandColor {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};

  friend bool operator==(const LandColor&, const LandColor&) = default;
};

LandLayerHeader parse_land_layer_header(std::span<const std::byte> payload);

std::array<std::byte, k_layer_header_size> encode_land_layer_header(const LandLayerHeader& header);

std::vector<LandLayerVertex> parse_land_layer_vertices(std::span<const std::byte> payload);

std::vector<std::byte> encode_land_layer_vertices(std::span<const LandLayerVertex> vertices);

std::array<LandColor, k_land_color_count> parse_land_colors(std::span<const std::byte> payload);

std::array<std::byte, k_land_color_size> encode_land_colors(std::span<const LandColor> colors);

}  // namespace fo4::land
