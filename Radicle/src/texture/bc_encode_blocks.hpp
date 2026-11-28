#pragma once

#include "fo4/texture/dds.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::texture::bc_encode {

constexpr std::int32_t k_block = 4;

struct Block {
  std::array<std::uint8_t, 16> red{};
  std::array<std::uint8_t, 16> green{};
  std::array<std::uint8_t, 16> blue{};
  std::array<std::uint8_t, 16> alpha{};
};

inline void check_image(std::span<const std::uint8_t> rgba, int width, int height, const char* format) {
  if (width <= 0 || height <= 0 ||
      rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
    throw dds_error{"InvalidImage", std::string(format) + " input size does not match its dimensions"};
  }
}

inline Block read_block(std::span<const std::uint8_t> rgba, int width, int height, int block_x, int block_y) {
  Block block;
  for (std::int32_t row = 0; row < k_block; ++row) {
    for (std::int32_t column = 0; column < k_block; ++column) {
      const auto x = std::min(width - 1, block_x * k_block + column);
      const auto y = std::min(height - 1, block_y * k_block + row);
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      const auto slot = static_cast<std::size_t>(row * k_block + column);
      block.red[slot] = rgba[index * 4];
      block.green[slot] = rgba[index * 4 + 1];
      block.blue[slot] = rgba[index * 4 + 2];
      block.alpha[slot] = rgba[index * 4 + 3];
    }
  }
  return block;
}

inline std::uint16_t pack565(int red, int green, int blue) {
  const auto r = static_cast<std::uint16_t>((red * 31 + 127) / 255);
  const auto g = static_cast<std::uint16_t>((green * 63 + 127) / 255);
  const auto b = static_cast<std::uint16_t>((blue * 31 + 127) / 255);
  return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}

inline std::array<int, 3> expand565(std::uint16_t colour) {
  const auto r = (colour >> 11) & 0x1F;
  const auto g = (colour >> 5) & 0x3F;
  const auto b = colour & 0x1F;
  return {(r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6};
}

inline std::uint8_t nearest_alpha(std::span<const std::uint8_t, 8> palette, int value) {
  std::uint8_t best = 0;
  int best_distance = 1 << 20;
  for (std::size_t index = 0; index < palette.size(); ++index) {
    const int distance = std::abs(static_cast<int>(palette[index]) - value);
    if (distance < best_distance) {
      best_distance = distance;
      best = static_cast<std::uint8_t>(index);
    }
  }
  return best;
}

// Invariant: endpoint 0 > endpoint 1 selects the 8-value palette, equal endpoints select the flat 6-value form.
inline std::array<std::uint8_t, 8> alpha_block(const std::array<std::uint8_t, 16>& values) {
  int low = 255;
  int high = 0;
  for (const auto value : values) {
    low = std::min(low, static_cast<int>(value));
    high = std::max(high, static_cast<int>(value));
  }
  std::array<std::uint8_t, 8> palette{};
  if (high == low) {
    palette[0] = static_cast<std::uint8_t>(high);
    palette[1] = static_cast<std::uint8_t>(high);
    for (std::size_t index = 2; index < palette.size(); ++index) palette[index] = palette[1];
  } else {
    palette[0] = static_cast<std::uint8_t>(high);
    palette[1] = static_cast<std::uint8_t>(low);
    for (int index = 1; index < 7; ++index) {
      palette[static_cast<std::size_t>(index + 1)] =
          static_cast<std::uint8_t>(((7 - index) * high + index * low + 3) / 7);
    }
  }
  std::array<std::uint8_t, 6> bits{};
  for (std::size_t slot = 0; slot < values.size(); ++slot) {
    const auto chosen = nearest_alpha(std::span<const std::uint8_t, 8>(palette.data(), palette.size()),
                                      static_cast<int>(values[slot]));
    const auto bit = slot * 3;
    const auto byte = bit / 8;
    const auto shift = bit % 8;
    bits[byte] = static_cast<std::uint8_t>(bits[byte] | static_cast<std::uint8_t>(chosen << shift));
    if (shift > 5) {
      bits[byte + 1] = static_cast<std::uint8_t>(bits[byte + 1] | static_cast<std::uint8_t>(chosen >> (8 - shift)));
    }
  }
  std::array<std::uint8_t, 8> out{};
  out[0] = palette[0];
  out[1] = palette[1];
  for (std::size_t index = 0; index < bits.size(); ++index) out[index + 2] = bits[index];
  return out;
}

inline std::uint8_t nearest_colour(const std::array<std::array<int, 3>, 4>& palette, int red, int green, int blue) {
  std::uint8_t best = 0;
  int best_distance = 1 << 30;
  for (std::size_t index = 0; index < palette.size(); ++index) {
    const int dr = palette[index][0] - red;
    const int dg = palette[index][1] - green;
    const int db = palette[index][2] - blue;
    const int distance = dr * dr + dg * dg + db * db;
    if (distance < best_distance) {
      best_distance = distance;
      best = static_cast<std::uint8_t>(index);
    }
  }
  return best;
}

// Invariant: colour0 > colour1 after packing, so the block decodes in four-colour (opaque) mode.
inline std::array<std::uint8_t, 8> colour_block(const Block& block) {
  int red_low = 255;
  int green_low = 255;
  int blue_low = 255;
  int red_high = 0;
  int green_high = 0;
  int blue_high = 0;
  for (std::size_t slot = 0; slot < block.red.size(); ++slot) {
    red_low = std::min(red_low, static_cast<int>(block.red[slot]));
    green_low = std::min(green_low, static_cast<int>(block.green[slot]));
    blue_low = std::min(blue_low, static_cast<int>(block.blue[slot]));
    red_high = std::max(red_high, static_cast<int>(block.red[slot]));
    green_high = std::max(green_high, static_cast<int>(block.green[slot]));
    blue_high = std::max(blue_high, static_cast<int>(block.blue[slot]));
  }
  auto high = pack565(red_high, green_high, blue_high);
  auto low = pack565(red_low, green_low, blue_low);
  if (high <= low) {
    if (high == low) {
      if (high > 0) {
        --low;
      } else {
        ++high;
      }
    } else {
      std::swap(high, low);
    }
  }
  const auto expanded_high = expand565(high);
  const auto expanded_low = expand565(low);
  const std::array<std::array<int, 3>, 4> palette{
      {expanded_high, expanded_low,
       {(2 * expanded_high[0] + expanded_low[0]) / 3, (2 * expanded_high[1] + expanded_low[1]) / 3,
        (2 * expanded_high[2] + expanded_low[2]) / 3},
       {(expanded_high[0] + 2 * expanded_low[0]) / 3, (expanded_high[1] + 2 * expanded_low[1]) / 3,
        (expanded_high[2] + 2 * expanded_low[2]) / 3}}};
  std::array<std::uint8_t, 4> indices{};
  for (std::size_t slot = 0; slot < block.red.size(); ++slot) {
    const auto chosen = nearest_colour(palette, block.red[slot], block.green[slot], block.blue[slot]);
    const auto shift = (slot % 4) * 2;
    indices[slot / 4] = static_cast<std::uint8_t>(indices[slot / 4] | static_cast<std::uint8_t>(chosen << shift));
  }
  std::array<std::uint8_t, 8> out{};
  out[0] = static_cast<std::uint8_t>(high & 0xFF);
  out[1] = static_cast<std::uint8_t>(high >> 8);
  out[2] = static_cast<std::uint8_t>(low & 0xFF);
  out[3] = static_cast<std::uint8_t>(low >> 8);
  for (std::size_t index = 0; index < indices.size(); ++index) out[index + 4] = indices[index];
  return out;
}

template <std::size_t N, typename EncodeBlock>
std::vector<std::uint8_t> encode_blocks(std::span<const std::uint8_t> rgba, int width, int height,
                                        const char* format, EncodeBlock&& encode_block) {
  check_image(rgba, width, height, format);
  const auto blocks_x = (width + k_block - 1) / k_block;
  const auto blocks_y = (height + k_block - 1) / k_block;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(blocks_x) * static_cast<std::size_t>(blocks_y) * N);
  std::size_t offset = 0;
  for (int block_y = 0; block_y < blocks_y; ++block_y) {
    for (int block_x = 0; block_x < blocks_x; ++block_x) {
      const auto bytes = encode_block(read_block(rgba, width, height, block_x, block_y));
      for (const auto value : bytes) out[offset++] = value;
    }
  }
  return out;
}

}  // namespace fo4::texture::bc_encode
