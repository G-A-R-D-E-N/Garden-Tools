#include "fo4/texture/palette.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using fo4::texture::PaletteColor;
using fo4::texture::palette_kmeans;

namespace {

std::vector<PaletteColor> separated_clusters() {
  std::vector<PaletteColor> pixels;
  for (int i = 0; i < 10; ++i) {
    pixels.push_back(PaletteColor{10, 10, 10, 255});
    pixels.push_back(PaletteColor{20, 20, 20, 255});
    pixels.push_back(PaletteColor{200, 100, 50, 255});
    pixels.push_back(PaletteColor{220, 120, 70, 255});
  }
  return pixels;
}

}  // namespace

// Detects: a k-means that must land on the exact mean of two well-separated clusters.
TEST(Palette, FindsTwoSeparatedClusterMeans) {
  const auto pixels = separated_clusters();
  const auto palette = palette_kmeans(pixels, 2);
  ASSERT_EQ(palette.size(), 2u);
  EXPECT_EQ(palette[0], (PaletteColor{15, 15, 15, 255}));
  EXPECT_EQ(palette[1], (PaletteColor{210, 110, 60, 255}));
}

// Detects: a palette that must be deterministic and independent of pixel order.
TEST(Palette, PaletteIsDeterministicAndOrderIndependent) {
  auto pixels = separated_clusters();
  const auto first = palette_kmeans(pixels, 2);
  const auto second = palette_kmeans(pixels, 2);
  EXPECT_EQ(first, second);
  std::reverse(pixels.begin(), pixels.end());
  EXPECT_EQ(palette_kmeans(pixels, 2), first);
  std::rotate(pixels.begin(), pixels.begin() + 7, pixels.end());
  EXPECT_EQ(palette_kmeans(pixels, 2), first);
}

// Detects: a request for more colors than the image holds that must return the distinct colors only.
TEST(Palette, ReturnsTheDistinctColorsWhenFewerArePresent) {
  const std::vector<PaletteColor> pixels{{10, 20, 30, 255},
                                         {10, 20, 30, 255},
                                         {200, 100, 50, 255},
                                         {0, 0, 0, 255}};
  const auto palette = palette_kmeans(pixels, 5);
  ASSERT_EQ(palette.size(), 3u);
  EXPECT_EQ(palette[0], (PaletteColor{0, 0, 0, 255}));
  EXPECT_EQ(palette[1], (PaletteColor{10, 20, 30, 255}));
  EXPECT_EQ(palette[2], (PaletteColor{200, 100, 50, 255}));
}

// Detects: empty inputs and a zero color request that must return an empty palette.
TEST(Palette, HandlesEmptyRequests) {
  const std::vector<PaletteColor> pixels{{1, 2, 3, 255}};
  EXPECT_TRUE(palette_kmeans(pixels, 0).empty());
  EXPECT_TRUE(palette_kmeans(std::span<const PaletteColor>{}, 4).empty());
}

// Detects: a single requested color that must be the rounded mean of every channel, alpha included.
TEST(Palette, SingleColorIsTheRoundedMean) {
  const std::vector<PaletteColor> pixels{{0, 0, 0, 0}, {10, 20, 30, 40}};
  const auto palette = palette_kmeans(pixels, 1);
  ASSERT_EQ(palette.size(), 1u);
  EXPECT_EQ(palette[0], (PaletteColor{5, 10, 15, 20}));
}

// Detects: colors that differ only in alpha that must not collapse into one palette entry.
TEST(Palette, SeparatesColorsThatDifferOnlyByAlpha) {
  const std::vector<PaletteColor> pixels{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 255}, {0, 0, 0, 255}};
  const auto palette = palette_kmeans(pixels, 2);
  ASSERT_EQ(palette.size(), 2u);
  EXPECT_EQ(palette[0], (PaletteColor{0, 0, 0, 0}));
  EXPECT_EQ(palette[1], (PaletteColor{0, 0, 0, 255}));
}

// Detects: a zero iteration budget that must return the deterministic initial centers unchanged.
TEST(Palette, ZeroIterationsReturnsTheInitialCenters) {
  const auto pixels = separated_clusters();
  const auto palette = palette_kmeans(pixels, 2, 0);
  ASSERT_EQ(palette.size(), 2u);
  EXPECT_EQ(palette[0], (PaletteColor{10, 10, 10, 255}));
  EXPECT_EQ(palette[1], (PaletteColor{220, 120, 70, 255}));
}
