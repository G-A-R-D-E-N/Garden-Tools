#include "fo4/texture/palette.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace fo4::texture {
namespace {

using Sums = std::array<std::uint64_t, 4>;

std::uint32_t channel(PaletteColor color, std::size_t index) {
  switch (index) {
    case 0:
      return color.r;
    case 1:
      return color.g;
    case 2:
      return color.b;
    default:
      return color.a;
  }
}

std::uint64_t distance_squared(PaletteColor left, PaletteColor right) {
  std::uint64_t total = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    const auto difference = static_cast<std::int64_t>(channel(left, index)) -
                            static_cast<std::int64_t>(channel(right, index));
    total += static_cast<std::uint64_t>(difference * difference);
  }
  return total;
}

PaletteColor mean_color(const Sums& sums, std::uint64_t count) {
  PaletteColor color;
  const auto rounded = [&](std::size_t index) {
    return static_cast<std::uint8_t>((sums[index] + count / 2) / count);
  };
  color.r = rounded(0);
  color.g = rounded(1);
  color.b = rounded(2);
  color.a = rounded(3);
  return color;
}

void add_color(Sums& sums, PaletteColor color) {
  for (std::size_t index = 0; index < 4; ++index) sums[index] += channel(color, index);
}

std::vector<PaletteColor> initial_centers(std::span<const PaletteColor> pixels,
                                          std::size_t colors) {
  std::vector<PaletteColor> centers;
  centers.reserve(colors);
  auto first = *std::min_element(pixels.begin(), pixels.end());
  centers.push_back(first);
  while (centers.size() < colors) {
    std::uint64_t best_distance = 0;
    PaletteColor best = centers.front();
    bool found = false;
    for (const auto& pixel : pixels) {
      std::uint64_t nearest = std::numeric_limits<std::uint64_t>::max();
      for (const auto& center : centers) {
        nearest = std::min(nearest, distance_squared(pixel, center));
      }
      if (nearest == 0) continue;
      if (!found || nearest > best_distance ||
          (nearest == best_distance && pixel < best)) {
        best_distance = nearest;
        best = pixel;
        found = true;
      }
    }
    if (!found) break;
    centers.push_back(best);
  }
  return centers;
}

}  // namespace

std::vector<PaletteColor> palette_kmeans(std::span<const PaletteColor> pixels,
                                         std::size_t colors,
                                         unsigned iterations) {
  if (colors == 0 || pixels.empty()) return {};
  auto centers = initial_centers(pixels, std::min(colors, pixels.size()));
  for (unsigned round = 0; round < iterations; ++round) {
    std::vector<Sums> sums(centers.size());
    std::vector<std::uint64_t> counts(centers.size(), 0);
    for (const auto& pixel : pixels) {
      std::size_t nearest = 0;
      std::uint64_t nearest_distance = distance_squared(pixel, centers.front());
      for (std::size_t index = 1; index < centers.size(); ++index) {
        const auto candidate = distance_squared(pixel, centers[index]);
        if (candidate < nearest_distance) {
          nearest_distance = candidate;
          nearest = index;
        }
      }
      add_color(sums[nearest], pixel);
      ++counts[nearest];
    }
    bool changed = false;
    for (std::size_t index = 0; index < centers.size(); ++index) {
      if (counts[index] == 0) continue;
      const auto next = mean_color(sums[index], counts[index]);
      if (!(next == centers[index])) {
        centers[index] = next;
        changed = true;
      }
    }
    if (!changed) break;
  }
  std::sort(centers.begin(), centers.end());
  return centers;
}

}  // namespace fo4::texture
