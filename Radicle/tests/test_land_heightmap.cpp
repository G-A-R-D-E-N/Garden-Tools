#include "fo4/land/heightmap.hpp"

#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

using fo4::land::LandHeightmap;
using fo4::land::decode_land_heightmap;
using fo4::land::encode_land_heightmap;
using fo4::land::land_heights;
using fo4::land::make_land_heightmap;

namespace {

std::array<std::byte, fo4::land::k_vhgt_size> payload(float offset,
                                                      const std::vector<int>& leading_deltas,
                                                      std::array<std::byte, 3> tail = {}) {
  std::array<std::byte, fo4::land::k_vhgt_size> bytes{};
  std::memcpy(bytes.data(), &offset, sizeof(offset));
  for (std::size_t index = 0; index < leading_deltas.size(); ++index) {
    bytes[4 + index] = static_cast<std::byte>(static_cast<std::uint8_t>(leading_deltas[index]));
  }
  for (std::size_t index = 0; index < tail.size(); ++index) {
    bytes[4 + fo4::land::k_heightmap_vertices + index] = tail[index];
  }
  return bytes;
}

float float_at(std::span<const std::byte> bytes, std::size_t offset) {
  float value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

}  // namespace

// Detects: a decoder that must read the measured 1096-byte layout, offset first and 1089 signed deltas after.
TEST(LandHeightmap, DecodesTheMeasuredLayout) {
  const auto bytes = payload(85.0f, {0, 2, 3, 3, 2, 2, 1, 0, -1, -1});
  const auto heightmap = decode_land_heightmap(bytes);
  EXPECT_EQ(heightmap.offset, 85.0f);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[0]), 0);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[2]), 3);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[8]), -1);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[10]), 0);
  EXPECT_EQ(encode_land_heightmap(heightmap), bytes);
}

// Detects: a decoder that must accumulate deltas across row boundaries instead of resetting each row.
TEST(LandHeightmap, HeightsAccumulateAcrossRowBoundaries) {
  std::vector<int> deltas(fo4::land::k_heightmap_vertices, 1);
  const auto heightmap = decode_land_heightmap(payload(10.0f, deltas));
  const auto heights = land_heights(heightmap);
  EXPECT_EQ(heights[0], 11.0f);
  EXPECT_EQ(heights[32], 43.0f);
  EXPECT_EQ(heights[33], 44.0f);
  EXPECT_EQ(heights[fo4::land::k_heightmap_vertices - 1], 10.0f + 1089.0f);
}

// Detects: a writer that must reproduce every shipped payload, including the unused trailing bytes.
TEST(LandHeightmap, RoundTripsEveryPayloadByte) {
  const std::array<std::byte, 3> measured_tail{std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFC}};
  const auto bytes = payload(-1003.0f, {5, -6, 7, 8}, measured_tail);
  const auto heightmap = decode_land_heightmap(bytes);
  EXPECT_EQ(heightmap.trailing_bytes, measured_tail);
  EXPECT_EQ(encode_land_heightmap(heightmap), bytes);
}

// Detects: widths other than the measured 1096 bytes, which must be refused.
TEST(LandHeightmap, RejectsPayloadsOfAnotherWidth) {
  for (const std::size_t size : {std::size_t{0}, std::size_t{1095}, std::size_t{1097}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(decode_land_heightmap(bytes), fo4::parse_error) << size;
  }
}

// Detects: a non-finite offset, which must be refused instead of poisoning the height grid.
TEST(LandHeightmap, RejectsNonFiniteOffset) {
  auto bytes = payload(0.0f, {});
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(bytes.data(), &nan, sizeof(nan));
  EXPECT_THROW(decode_land_heightmap(bytes), fo4::parse_error);
}

// Detects: an encoder that must recover the exact deltas from a decoded grid.
TEST(LandHeightmap, BuildsDeltasFromHeights) {
  const auto bytes = payload(5597.0f, {0, 4, -3, 1});
  const auto original = decode_land_heightmap(bytes);
  const auto heights = land_heights(original);
  const auto rebuilt = make_land_heightmap(heights, original.offset);
  ASSERT_TRUE(rebuilt.has_value());
  EXPECT_EQ(rebuilt->deltas, original.deltas);
  EXPECT_EQ(*rebuilt, original);
}

// Detects: a step that must round to the nearest signed byte, because heights are integers plus an offset.
TEST(LandHeightmap, RoundsFractionalStepsToTheNearestByte) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[0] = 0.4f;
  heights[1] = 1.6f;
  const auto heightmap = make_land_heightmap(heights, 0.0f);
  ASSERT_TRUE(heightmap.has_value());
  EXPECT_EQ(static_cast<int>(heightmap->deltas[0]), 0);
  EXPECT_EQ(static_cast<int>(heightmap->deltas[1]), 1);
  EXPECT_EQ(land_heights(*heightmap)[1], 1.0f);
}

// Detects: a height grid whose step cannot fit a signed byte, which must be refused instead of wrapping.
TEST(LandHeightmap, RejectsHeightsWithTooLargeStep) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[5] = 200.0f;
  EXPECT_FALSE(make_land_heightmap(heights, 0.0f).has_value());
  std::vector<float> short_grid(10, 0.0f);
  EXPECT_FALSE(make_land_heightmap(short_grid, 0.0f).has_value());
}

// Detects: a non-finite height, which must be refused.
TEST(LandHeightmap, RejectsNonFiniteHeights) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[7] = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(make_land_heightmap(heights, 0.0f).has_value());
  heights[7] = 1.0f;
  EXPECT_FALSE(make_land_heightmap(heights, std::numeric_limits<float>::quiet_NaN()).has_value());
}

// Detects: a flat grid that must be representable with zero deltas and keep its offset.
TEST(LandHeightmap, EncodesAFlatGridWithoutDeltas) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 42.0f);
  const auto heightmap = make_land_heightmap(heights, 42.0f);
  ASSERT_TRUE(heightmap.has_value());
  for (const auto delta : heightmap->deltas) EXPECT_EQ(delta, 0);
  const auto bytes = encode_land_heightmap(*heightmap);
  EXPECT_EQ(float_at(bytes, 0), 42.0f);
  EXPECT_EQ(land_heights(*heightmap)[0], 42.0f);
}
