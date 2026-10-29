#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

std::vector<std::byte> movie_of(const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

SwfBuilder place3_header(std::uint32_t flags3) {
  SwfBuilder builder;
  builder.u8(0x02);
  builder.u8(flags3);
  builder.u16(5);
  builder.u16(3);
  return builder;
}

}  // namespace

TEST(SwfPlaceFilters, DropShadowReadsRgbaColourFixedPointFieldsAndFlags) {
  auto builder = place3_header(0x01);
  builder.u8(1);
  builder.u8(0);
  builder.u8(0x11);
  builder.u8(0x22);
  builder.u8(0x33);
  builder.u8(0x44);
  builder.u32(0x00018000);
  builder.u32(0x00020000);
  builder.u32(0x00008000);
  builder.u32(0x00100000);
  builder.u16(0x0180);
  builder.u8(0xE3);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 1u);
  const auto& filter = place.filters[0];
  EXPECT_EQ(filter.id, 0);
  EXPECT_EQ(filter.color, 0x44112233u);
  EXPECT_DOUBLE_EQ(filter.blur_x, 1.5);
  EXPECT_DOUBLE_EQ(filter.blur_y, 2.0);
  EXPECT_DOUBLE_EQ(filter.angle, 0.5);
  EXPECT_DOUBLE_EQ(filter.distance, 16.0);
  EXPECT_DOUBLE_EQ(filter.strength, 1.5);
  EXPECT_TRUE(filter.inner);
  EXPECT_TRUE(filter.knockout);
  EXPECT_TRUE(filter.composite_source);
  EXPECT_FALSE(filter.on_top);
  EXPECT_EQ(filter.passes, 3);
}

TEST(SwfPlaceFilters, BlurAndGlowReadPassesFromTheirOwnBitsAndStrengthAtEightEight) {
  auto builder = place3_header(0x01);
  builder.u8(2);
  builder.u8(1);
  builder.u32(0x00010000);
  builder.u32(0x00030000);
  builder.u8(0x2F);
  builder.u8(2);
  builder.u8(0xFF);
  builder.u8(0x00);
  builder.u8(0x00);
  builder.u8(0x80);
  builder.u32(0x00020000);
  builder.u32(0x00020000);
  builder.u16(0x0120);
  builder.u8(0x47);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 2u);
  const auto& blur = place.filters[0];
  EXPECT_EQ(blur.id, 1);
  EXPECT_DOUBLE_EQ(blur.blur_x, 1.0);
  EXPECT_DOUBLE_EQ(blur.blur_y, 3.0);
  EXPECT_EQ(blur.passes, 5);
  EXPECT_FALSE(blur.inner);
  const auto& glow = place.filters[1];
  EXPECT_EQ(glow.id, 2);
  EXPECT_EQ(glow.color, 0x80FF0000u);
  EXPECT_DOUBLE_EQ(glow.blur_x, 2.0);
  EXPECT_DOUBLE_EQ(glow.blur_y, 2.0);
  EXPECT_DOUBLE_EQ(glow.strength, 1.125);
  EXPECT_TRUE(glow.knockout);
  EXPECT_FALSE(glow.inner);
  EXPECT_FALSE(glow.composite_source);
  EXPECT_EQ(glow.passes, 7);
}

TEST(SwfPlaceFilters, BevelReadsShadowThenHighlightSignedAngleAndOnTopBeforeBlend) {
  auto builder = place3_header(0x03);
  builder.u8(1);
  builder.u8(3);
  builder.u8(0x10);
  builder.u8(0x20);
  builder.u8(0x30);
  builder.u8(0xFF);
  builder.u8(0xA0);
  builder.u8(0xB0);
  builder.u8(0xC0);
  builder.u8(0xD0);
  builder.u32(0x00010000);
  builder.u32(0x00010000);
  builder.u32(0xFFFF8000);
  builder.u32(0x00020000);
  builder.u16(0x0100);
  builder.u8(0x91);
  builder.u8(0x0E);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 1u);
  const auto& bevel = place.filters[0];
  EXPECT_EQ(bevel.id, 3);
  EXPECT_EQ(bevel.color, 0xFF102030u);
  EXPECT_EQ(bevel.highlight_color, 0xD0A0B0C0u);
  EXPECT_DOUBLE_EQ(bevel.angle, -0.5);
  EXPECT_DOUBLE_EQ(bevel.distance, 2.0);
  EXPECT_DOUBLE_EQ(bevel.strength, 1.0);
  EXPECT_TRUE(bevel.inner);
  EXPECT_TRUE(bevel.on_top);
  EXPECT_FALSE(bevel.knockout);
  EXPECT_EQ(bevel.passes, 1);
  EXPECT_EQ(place.blend_mode, std::optional<std::uint8_t>(14));
}

TEST(SwfPlaceFilters, GradientGlowAndBevelReadEveryStopColourThenEveryRatio) {
  auto builder = place3_header(0x01);
  builder.u8(2);
  builder.u8(4);
  builder.u8(2);
  builder.u8(0x01);
  builder.u8(0x02);
  builder.u8(0x03);
  builder.u8(0xFF);
  builder.u8(0x04);
  builder.u8(0x05);
  builder.u8(0x06);
  builder.u8(0x80);
  builder.u8(0x00);
  builder.u8(0xFF);
  builder.u32(0x00010000);
  builder.u32(0x00010000);
  builder.u32(0);
  builder.u32(0);
  builder.u16(0x0100);
  builder.u8(0x12);
  builder.u8(7);
  builder.u8(1);
  builder.u8(0x0A);
  builder.u8(0x0B);
  builder.u8(0x0C);
  builder.u8(0xFF);
  builder.u8(0x80);
  builder.u32(0);
  builder.u32(0);
  builder.u32(0);
  builder.u32(0);
  builder.u16(0);
  builder.u8(0x00);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 2u);
  const auto& glow = place.filters[0];
  EXPECT_EQ(glow.id, 4);
  EXPECT_EQ(glow.gradient_colors, (std::vector<std::uint32_t>{0xFF010203u, 0x80040506u}));
  EXPECT_EQ(glow.gradient_ratios, (std::vector<std::uint8_t>{0x00, 0xFF}));
  EXPECT_TRUE(glow.on_top);
  EXPECT_FALSE(glow.inner);
  EXPECT_EQ(glow.passes, 2);
  const auto& bevel = place.filters[1];
  EXPECT_EQ(bevel.id, 7);
  EXPECT_EQ(bevel.gradient_colors, (std::vector<std::uint32_t>{0xFF0A0B0Cu}));
  EXPECT_EQ(bevel.gradient_ratios, (std::vector<std::uint8_t>{0x80}));
  EXPECT_EQ(bevel.passes, 0);
}

TEST(SwfPlaceFilters, ConvolutionReadsDivisorBiasMatrixAndClampBeforeBlend) {
  auto builder = place3_header(0x03);
  builder.u8(1);
  builder.u8(5);
  builder.u8(3);
  builder.u8(2);
  builder.u32(0x40000000);
  builder.u32(0x3F000000);
  for (const std::uint32_t bits :
       {0x3F800000u, 0x00000000u, 0xBF800000u, 0x40000000u, 0x3E800000u, 0x40800000u}) {
    builder.u32(bits);
  }
  builder.u32(0);
  builder.u8(0x02);
  builder.u8(0x0B);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 1u);
  const auto& convolution = place.filters[0];
  EXPECT_EQ(convolution.id, 5);
  EXPECT_EQ(convolution.matrix_columns, 3);
  EXPECT_EQ(convolution.matrix_rows, 2);
  EXPECT_FLOAT_EQ(convolution.divisor, 2.0f);
  EXPECT_FLOAT_EQ(convolution.bias, 0.5f);
  EXPECT_EQ(convolution.matrix, (std::vector<float>{1.0f, 0.0f, -1.0f, 2.0f, 0.25f, 4.0f}));
  EXPECT_EQ(convolution.default_color, 0u);
  EXPECT_TRUE(convolution.clamp);
  EXPECT_FALSE(convolution.preserve_alpha);
  EXPECT_EQ(place.blend_mode, std::optional<std::uint8_t>(11));
}

TEST(SwfPlaceFilters, ColorMatrixKeepsAllTwentyEntriesBeforeTheBlendMode) {
  auto builder = place3_header(0x03);
  builder.u8(1);
  builder.u8(6);
  for (int index = 0; index < 20; ++index) {
    builder.u32(std::bit_cast<std::uint32_t>(static_cast<float>(index)));
  }
  builder.u8(0x0C);
  const auto result = fo4::swf::read_swf(movie_of({{70, builder.body}}));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_EQ(place.filters.size(), 1u);
  const auto& matrix = place.filters[0];
  EXPECT_EQ(matrix.id, 6);
  ASSERT_EQ(matrix.matrix.size(), 20u);
  EXPECT_FLOAT_EQ(matrix.matrix.front(), 0.0f);
  EXPECT_FLOAT_EQ(matrix.matrix.back(), 19.0f);
  EXPECT_EQ(place.blend_mode, std::optional<std::uint8_t>(12));
}

TEST(SwfPlaceFilters, AnUnknownFilterIdOrAShortGradientBodyMakesThePlacementMalformed) {
  auto unknown = place3_header(0x01);
  unknown.u8(1);
  unknown.u8(9);
  unknown.u8(0);
  const auto unknown_result = fo4::swf::read_swf(movie_of({{70, unknown.body}}));
  EXPECT_EQ(unknown_result.status, fo4::swf::SwfStatus::malformed_tag);
  ASSERT_FALSE(unknown_result.file.tags.empty());
  EXPECT_FALSE(unknown_result.file.tags[0].placement.has_value());

  auto truncated = place3_header(0x01);
  truncated.u8(1);
  truncated.u8(4);
  truncated.u8(2);
  truncated.u8(0x01);
  truncated.u8(0x02);
  truncated.u8(0x03);
  truncated.u8(0xFF);
  const auto truncated_result = fo4::swf::read_swf(movie_of({{70, truncated.body}}));
  EXPECT_EQ(truncated_result.status, fo4::swf::SwfStatus::malformed_tag);
  ASSERT_FALSE(truncated_result.file.tags.empty());
  EXPECT_FALSE(truncated_result.file.tags[0].placement.has_value());
}
