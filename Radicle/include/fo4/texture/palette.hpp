#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::texture {

struct PaletteColor {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
  std::uint8_t a{255};

  friend bool operator==(const PaletteColor&, const PaletteColor&) = default;
  friend auto operator<=>(const PaletteColor&, const PaletteColor&) = default;
};

std::vector<PaletteColor> palette_kmeans(std::span<const PaletteColor> pixels,
                                         std::size_t colors,
                                         unsigned iterations = 16);

}  // namespace fo4::texture
