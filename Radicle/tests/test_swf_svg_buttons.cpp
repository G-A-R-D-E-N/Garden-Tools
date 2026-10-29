#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"
#include "fo4/swf/swf_svg_filters.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using swf_test::matrix_full;
using swf_test::place_full;
using swf_test::sprite_tag;
using swf_test::SwfBuilder;
using swf_test::TagPayload;
using swf_test::wrap_body;

const std::string kStage =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n";

const std::string kRedSquare =
    "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

const std::string kBlueSquare =
    "<path d=\"M0 0L30 0L30 30L0 30L0 0Z\" fill=\"#0000ff\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

constexpr std::uint16_t kButtonId = 3;

struct Record {
  std::uint8_t flags;
  std::uint16_t character;
  std::uint16_t depth;
  std::int32_t x;
  std::int32_t y;
};

std::vector<std::byte> square_shape(std::uint16_t id, std::int32_t size, std::uint32_t rgb) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, size, 0, size, 12);
  body.u8(1);
  swf_art_test::solid_fill(body, false, rgb);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x03, 5);
  body.push_bits(12, 5);
  body.push_bits(0, 12);
  body.push_bits(0, 12);
  body.push_bits(1, 1);
  const std::int32_t edges[8] = {size, 0, 0, size, -size, 0, 0, -size};
  for (std::size_t index = 0; index < 8; index += 2) {
    body.push_bits(1, 1);
    body.push_bits(1, 1);
    body.push_bits(10, 4);
    body.push_bits(1, 1);
    body.push_bits(static_cast<std::uint32_t>(edges[index]) & 0xFFFu, 12);
    body.push_bits(static_cast<std::uint32_t>(edges[index + 1]) & 0xFFFu, 12);
  }
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

void translate_matrix(SwfBuilder& body, std::int32_t x, std::int32_t y) {
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  body.push_bits(10, 5);
  swf_art_test::signed_bits(body, x, 10);
  swf_art_test::signed_bits(body, y, 10);
  body.align();
}

void half_colour_transform(SwfBuilder& body) {
  body.push_bits(0, 1);
  body.push_bits(1, 1);
  body.push_bits(9, 4);
  for (int channel = 0; channel < 4; ++channel) {
    body.push_bits(128, 9);
  }
  body.align();
}

std::vector<std::byte> button_payload(std::uint16_t code, const std::vector<Record>& records) {
  SwfBuilder body;
  body.u16(kButtonId);
  if (code == fo4::swf::tag::kDefineButton2) {
    body.u8(0);
    body.u16(0);
  }
  for (const Record& record : records) {
    body.u8(record.flags);
    body.u16(record.character);
    body.u16(record.depth);
    translate_matrix(body, record.x, record.y);
    if (code == fo4::swf::tag::kDefineButton2) {
      half_colour_transform(body);
    }
  }
  body.u8(0);
  return body.body;
}

std::vector<std::byte> button_movie(std::uint16_t code, const std::vector<Record>& records) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(2, square_shape(1, 1000, 0xFF0000u), false);
  file.tag(2, square_shape(2, 600, 0x0000FFu), false);
  file.tag(code, button_payload(code, records), false);
  SwfBuilder place;
  place.u8(0x02);
  place.u16(1);
  place.u16(kButtonId);
  file.tag(26, place.body, false);
  file.tag(1);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

void frame_svg_of(std::uint16_t code, const std::vector<Record>& records, std::string& svg) {
  const auto result = fo4::swf::read_swf(button_movie(code, records));
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  ASSERT_EQ(display.status, fo4::swf::SwfTimelineStatus::ok);
  svg = fo4::swf::frame_to_svg(result.file, display, 0);
}

fo4::swf::SwfRect stage_rect() {
  fo4::swf::SwfRect stage;
  stage.x_max = 11000;
  stage.y_max = 8000;
  return stage;
}

fo4::swf::SwfFilter filter_of(std::uint8_t id, std::uint32_t argb, double blur_x, double blur_y,
                              double strength = 1.0) {
  fo4::swf::SwfFilter filter;
  filter.id = id;
  filter.color = argb;
  filter.blur_x = blur_x;
  filter.blur_y = blur_y;
  filter.strength = strength;
  filter.composite_source = true;
  return filter;
}

std::string filter_open(const std::string& id) {
  return "<filter id=\"" + id +
         "\" filterUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\" width=\"550\" height=\"400\" "
         "color-interpolation-filters=\"sRGB\">";
}

constexpr double kQuarterPi = 0.7853981633974483;

SwfBuilder drop_shadow_placement() {
  SwfBuilder place;
  place.u8(0x02);
  place.u8(0x01);
  place.u16(1);
  place.u16(1);
  place.u8(1);
  place.u8(0);
  place.u8(0xFF);
  place.u8(0x00);
  place.u8(0x00);
  place.u8(0x80);
  place.u32(0x00080000);
  place.u32(0x00040000);
  place.u32(0);
  place.u32(0x000A0000);
  place.u16(0x0100);
  place.u8(0x20);
  return place;
}

SwfBuilder blend_placement(std::uint8_t mode, std::uint16_t character = 1,
                           std::uint16_t depth = 1) {
  SwfBuilder place;
  place.u8(0x02);
  place.u8(0x02);
  place.u16(depth);
  place.u16(character);
  place.u8(mode);
  return place;
}

SwfBuilder blur_placement(std::uint16_t character) {
  SwfBuilder place;
  place.u8(0x02);
  place.u8(0x01);
  place.u16(1);
  place.u16(character);
  place.u8(1);
  place.u8(1);
  place.u32(0x00080000);
  place.u32(0x00040000);
  place.u8(0x08);
  return place;
}

SwfBuilder plain_placement() {
  SwfBuilder place;
  place.u8(0x02);
  place.u16(1);
  place.u16(1);
  return place;
}

void placed_square_svg(std::uint16_t code, const SwfBuilder& place, std::string& svg) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(2, square_shape(1, 1000, 0xFF0000u), false);
  file.tag(code, place.body, false);
  file.tag(1);
  file.tag(0);
  const auto result = fo4::swf::read_swf(wrap_body(file.body, 10, false, false));
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  ASSERT_EQ(display.status, fo4::swf::SwfTimelineStatus::ok);
  svg = fo4::swf::frame_to_svg(result.file, display, 0);
}

void sprite_movie_svg(const std::vector<TagPayload>& root_tags, std::string& svg) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(2, square_shape(1, 1000, 0xFF0000u), false);
  file.tag(2, square_shape(2, 600, 0x0000FFu), false);
  for (const auto& [code, payload] : root_tags) {
    file.tag(code, payload, false);
  }
  file.tag(1);
  file.tag(0);
  const auto result = fo4::swf::read_swf(wrap_body(file.body, 10, false, false));
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  ASSERT_EQ(display.status, fo4::swf::SwfTimelineStatus::ok);
  svg = fo4::swf::frame_to_svg(result.file, display, 0);
}

std::vector<TagPayload> red_and_blue_children() {
  const auto blue_at_20 = matrix_full(65536, 65536, 0, 0, 400, 0);
  return {{fo4::swf::tag::kPlaceObject2, place_full(1, 1, {}, {})},
          {fo4::swf::tag::kPlaceObject2, place_full(2, 2, blue_at_20, {})},
          {fo4::swf::tag::kShowFrame, {}}};
}

}  // namespace

TEST(SwfFrameSvgButtons, DefineButton2UpRecordDrawsItsShapeAtItsMatrix) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x01, 1, 1, 200, 100}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 10 5)\">\n" + kRedSquare + "</g>\n</svg>\n");
}

TEST(SwfFrameSvgButtons, HitOnlyRecordDrawsNothing) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x08, 1, 1, 0, 0}}, svg);
  EXPECT_EQ(svg, kStage + "</svg>\n");
}

TEST(SwfFrameSvgButtons, UpRecordsDrawInDepthOrderAndIgnoreTheirColourTransforms) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x01, 1, 5, 0, 0}, {0x01, 2, 3, 400, 0}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" +
                "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n" + "</svg>\n");
}

TEST(SwfFrameSvgButtons, DefineButtonRecordHasNoColourTransformBytes) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton, {{0x01, 1, 1, 200, 100}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 10 5)\">\n" + kRedSquare + "</g>\n</svg>\n");
}

TEST(SwfSvgFilters, DropShadowShiftsByAngleAndHalvesBlurIntoStdDeviation) {
  auto shadow = filter_of(0, 0x80FF0000u, 8, 4);
  shadow.distance = 10;
  shadow.angle = kQuarterPi;
  EXPECT_EQ(fo4::swf::filter_defs({shadow}, "r0", stage_rect()).defs,
            "<defs>" + filter_open("r0fl0") +
                "<feOffset in=\"SourceAlpha\" dx=\"7.071068\" dy=\"7.071068\" result=\"o\"/>"
                "<feGaussianBlur in=\"o\" stdDeviation=\"4 2\" result=\"b\"/>"
                "<feFlood flood-color=\"#ff0000\" flood-opacity=\"0.501961\" result=\"f\"/>"
                "<feComposite in=\"f\" in2=\"b\" operator=\"in\" result=\"s\"/>"
                "<feMerge><feMergeNode in=\"s\"/><feMergeNode in=\"SourceGraphic\"/></feMerge>"
                "</filter></defs>\n");
}

TEST(SwfSvgFilters, KnockoutDropShadowMergesOnlyTheShadow) {
  auto shadow = filter_of(0, 0x80FF0000u, 8, 4);
  shadow.knockout = true;
  const std::string defs = fo4::swf::filter_defs({shadow}, "r0", stage_rect()).defs;
  EXPECT_TRUE(defs.ends_with(
      "<feComposite in=\"s\" in2=\"SourceAlpha\" operator=\"out\" result=\"k\"/>"
      "<feMerge><feMergeNode in=\"k\"/></feMerge></filter></defs>\n"));
  EXPECT_EQ(defs.find("SourceGraphic"), std::string::npos);
}

TEST(SwfSvgFilters, ClearCompositeSourceDropsTheSourceFromTheMerge) {
  auto shadow = filter_of(0, 0x80FF0000u, 8, 4);
  shadow.composite_source = false;
  auto glow = filter_of(2, 0xFF00FF00u, 2, 2);
  glow.inner = true;
  glow.composite_source = false;
  const std::string shadow_defs = fo4::swf::filter_defs({shadow}, "r0", stage_rect()).defs;
  const std::string glow_defs = fo4::swf::filter_defs({glow}, "r0", stage_rect()).defs;
  EXPECT_TRUE(shadow_defs.ends_with(
      "<feMerge><feMergeNode in=\"s\"/></feMerge></filter></defs>\n"));
  EXPECT_TRUE(glow_defs.ends_with(
      "<feMerge><feMergeNode in=\"i\"/></feMerge></filter></defs>\n"));
}

TEST(SwfSvgFilters, InnerGlowClipsItsShadowToTheSourceAndKeepsTheSource) {
  auto glow = filter_of(2, 0xFF00FF00u, 2, 2, 2.0);
  glow.inner = true;
  EXPECT_EQ(fo4::swf::filter_defs({glow}, "r0", stage_rect()).defs,
            "<defs>" + filter_open("r0fl0") +
                "<feComponentTransfer in=\"SourceAlpha\" result=\"h\"><feFuncA type=\"table\" "
                "tableValues=\"1 0\"/></feComponentTransfer>"
                "<feOffset in=\"h\" dx=\"0\" dy=\"0\" result=\"o\"/>"
                "<feGaussianBlur in=\"o\" stdDeviation=\"1 1\" result=\"b\"/>"
                "<feFlood flood-color=\"#00ff00\" flood-opacity=\"1\" result=\"f\"/>"
                "<feComposite in=\"f\" in2=\"b\" operator=\"in\" result=\"s\"/>"
                "<feComposite in=\"s\" in2=\"SourceAlpha\" operator=\"in\" result=\"i\"/>"
                "<feMerge><feMergeNode in=\"SourceGraphic\"/><feMergeNode in=\"i\"/></feMerge>"
                "</filter></defs>\n");
}

TEST(SwfSvgFilters, BlurHalvesEachAxisInOrder) {
  EXPECT_EQ(fo4::swf::filter_defs({filter_of(1, 0, 6, 2)}, "r0", stage_rect()).defs,
            "<defs>" + filter_open("r0fl0") +
                "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"3 1\"/></filter></defs>\n");
}

TEST(SwfSvgFilters, ColorMatrixKeepsTwentyValuesAndScalesOffsetsToUnit) {
  auto matrix = filter_of(6, 0, 0, 0);
  matrix.matrix = {0.3f,  0.59f, 0.11f, 0.0f, 51.0f, 0.3f,  0.59f, 0.11f, 0.0f, 0.0f,
                   0.3f,  0.59f, 0.11f, 0.0f, 0.0f,  0.0f,  0.0f,  0.0f,  1.0f, 0.0f};
  EXPECT_EQ(fo4::swf::filter_defs({matrix}, "r0", stage_rect()).defs,
            "<defs>" + filter_open("r0fl0") +
                "<feColorMatrix in=\"SourceGraphic\" type=\"matrix\" values=\"0.3 0.59 0.11 0 0.2 "
                "0.3 0.59 0.11 0 0 0.3 0.59 0.11 0 0 0 0 0 1 0\"/></filter></defs>\n");
}

TEST(SwfSvgFilters, UnsupportedFiltersAreSkippedAndKeepTheirIndexedIds) {
  const auto result = fo4::swf::filter_defs(
      {filter_of(3, 0, 0, 0), filter_of(4, 0, 0, 0), filter_of(5, 0, 0, 0), filter_of(1, 0, 2, 2),
       filter_of(7, 0, 0, 0)},
      "r0", stage_rect());
  EXPECT_EQ(result.skipped, (std::vector<std::uint8_t>{3, 4, 5, 7}));
  EXPECT_EQ(result.ids, (std::vector<std::string>{"r0fl3"}));
  EXPECT_EQ(result.defs, "<defs>" + filter_open("r0fl3") +
                             "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"1 1\"/>"
                             "</filter></defs>\n");
}

TEST(SwfSvgFilters, BlendModeTableMapsCssValuesAndRefusesTheRest) {
  const std::vector<std::string> expected = {
      "",       "",       "",       "mix-blend-mode:multiply",   "mix-blend-mode:screen",
      "mix-blend-mode:lighten", "mix-blend-mode:darken", "mix-blend-mode:difference",
      "mix-blend-mode:plus-lighter", "", "", "", "", "mix-blend-mode:overlay",
      "mix-blend-mode:hard-light", "", ""};
  for (std::size_t mode = 0; mode < expected.size(); ++mode) {
    EXPECT_EQ(fo4::swf::blend_mode_style(static_cast<std::uint8_t>(mode)), expected[mode])
        << "blend mode " << mode;
  }
}

TEST(SwfSvgFilters, FilteredGroupNestsFiltersInListOrderUnderTheBlendGroup) {
  const std::string defs =
      "<defs>" + filter_open("r2fl0") +
      "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"1 1\"/></filter>" + filter_open("r2fl1") +
      "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"2 3\"/></filter></defs>\n";
  EXPECT_EQ(fo4::swf::filtered_group({filter_of(1, 0, 2, 2), filter_of(1, 0, 4, 6)},
                                     std::uint8_t{3}, "r2", stage_rect(), "X\n"),
            "<g style=\"mix-blend-mode:multiply\">\n" + defs +
                "<g filter=\"url(#r2fl1)\">\n<g filter=\"url(#r2fl0)\">\nX\n</g>\n</g>\n</g>\n");
}

TEST(SwfFrameSvgFilters, PlacedShapeWithDropShadowWrapsItsGroupInTheShadowFilter) {
  std::string svg;
  placed_square_svg(fo4::swf::tag::kPlaceObject3, drop_shadow_placement(), svg);
  EXPECT_EQ(svg,
            kStage + "<defs>" + filter_open("r0fl0") +
                "<feOffset in=\"SourceAlpha\" dx=\"10\" dy=\"0\" result=\"o\"/>"
                "<feGaussianBlur in=\"o\" stdDeviation=\"4 2\" result=\"b\"/>"
                "<feFlood flood-color=\"#ff0000\" flood-opacity=\"0.501961\" result=\"f\"/>"
                "<feComposite in=\"f\" in2=\"b\" operator=\"in\" result=\"s\"/>"
                "<feMerge><feMergeNode in=\"s\"/><feMergeNode in=\"SourceGraphic\"/></feMerge>"
                "</filter></defs>\n"
                "<g filter=\"url(#r0fl0)\">\n"
                "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n" + "</g>\n" +
                "</svg>\n");
}

TEST(SwfFrameSvgFilters, PlainPlacementIsByteIdenticalToTheUnfilteredGroup) {
  std::string svg;
  placed_square_svg(fo4::swf::tag::kPlaceObject2, plain_placement(), svg);
  EXPECT_EQ(svg, kStage + "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n</svg>\n");
}

TEST(SwfFrameSvgFilters, PlacedBlendModeWrapsTheGroupInItsStyle) {
  std::string svg;
  placed_square_svg(fo4::swf::tag::kPlaceObject3, blend_placement(3), svg);
  EXPECT_EQ(svg,
            kStage + "<g style=\"mix-blend-mode:multiply\">\n"
                     "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n" +
                "</g>\n</svg>\n");
}

TEST(SwfFrameSvgFilters, SpriteBlurWrapsBothFlattenedChildrenInOneFilteredGroup) {
  // Invariant: a blur placed on a sprite wraps every flattened child in one filter group, where a dropped sprite row leaves the children unfiltered.
  std::string svg;
  sprite_movie_svg({{fo4::swf::tag::kDefineSprite, sprite_tag(3, 1, red_and_blue_children())},
                    {fo4::swf::tag::kPlaceObject3, blur_placement(3).body}},
                   svg);
  EXPECT_EQ(svg,
            kStage + "<defs>" + filter_open("r0fl0") +
                "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"4 2\"/></filter></defs>\n"
                "<g filter=\"url(#r0fl0)\">\n"
                "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n"
                "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" +
                "</g>\n</svg>\n");
}

TEST(SwfFrameSvgFilters, NestedSpriteInsideFilteredSpriteClosesEachGroupAtItsOwnEnd) {
  // Invariant: an inner sprite's blend group closes at its own last child, before the outer sibling that follows it, and the outer filter closes after the last row, where a wrap closed at the wrong index moves the boundary.
  std::string svg;
  const auto blue_at_20 = matrix_full(65536, 65536, 0, 0, 400, 0);
  const auto red_at_down_20 = matrix_full(65536, 65536, 0, 0, 0, 400);
  const std::vector<TagPayload> inner = {
      {fo4::swf::tag::kPlaceObject2, place_full(2, 2, blue_at_20, {})},
      {fo4::swf::tag::kShowFrame, {}}};
  const std::vector<TagPayload> outer = {
      {fo4::swf::tag::kPlaceObject2, place_full(1, 1, {}, {})},
      {fo4::swf::tag::kPlaceObject3, blend_placement(3, 4, 2).body},
      {fo4::swf::tag::kPlaceObject2, place_full(3, 1, red_at_down_20, {})},
      {fo4::swf::tag::kShowFrame, {}}};
  sprite_movie_svg({{fo4::swf::tag::kDefineSprite, sprite_tag(3, 1, outer)},
                    {fo4::swf::tag::kDefineSprite, sprite_tag(4, 1, inner)},
                    {fo4::swf::tag::kPlaceObject3, blur_placement(3).body}},
                   svg);
  EXPECT_EQ(svg,
            kStage + "<defs>" + filter_open("r0fl0") +
                "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"4 2\"/></filter></defs>\n"
                "<g filter=\"url(#r0fl0)\">\n"
                "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n"
                "<g style=\"mix-blend-mode:multiply\">\n"
                "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" + "</g>\n" +
                "<g transform=\"matrix(1 0 0 1 0 20)\">\n" + kRedSquare + "</g>\n" +
                "</g>\n</svg>\n");
}

TEST(SwfFrameSvgFilters, UnfilteredSpriteAddsNoWrapperAroundItsChildren) {
  // Invariant: a sprite with no filter and no blend emits only its flattened children, where an extra wrapper would change every movie that places a plain sprite.
  std::string svg;
  sprite_movie_svg({{fo4::swf::tag::kDefineSprite, sprite_tag(3, 1, red_and_blue_children())},
                    {fo4::swf::tag::kPlaceObject2, place_full(1, 3, {}, {})}},
                   svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n" +
                "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" +
                "</svg>\n");
}

TEST(SwfFrameSvgFilters, BlendModeOnSpriteWrapsItsFlattenedChildrenInOneStyleGroup) {
  // Invariant: a blend mode on a sprite reaches its flattened children through one style group, where the sprite row itself emits nothing and so would drop the blend.
  std::string svg;
  sprite_movie_svg({{fo4::swf::tag::kDefineSprite, sprite_tag(3, 1, red_and_blue_children())},
                    {fo4::swf::tag::kPlaceObject3, blend_placement(3, 3).body}},
                   svg);
  EXPECT_EQ(svg,
            kStage + "<g style=\"mix-blend-mode:multiply\">\n"
                     "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n"
                     "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" +
                "</g>\n</svg>\n");
}
