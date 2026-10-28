#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_bitmap_png.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"
#include "fo4/swf/swf_svg.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfShapeRecord;
using fo4::swf::SwfShapeRecordKind;

SwfShapeRecord move_to(std::int32_t x, std::int32_t y) {
  SwfShapeRecord record{SwfShapeRecordKind::move};
  record.x = x;
  record.y = y;
  return record;
}

SwfShapeRecord line_by(std::int32_t dx, std::int32_t dy) {
  SwfShapeRecord record{SwfShapeRecordKind::straight_edge};
  record.x = dx;
  record.y = dy;
  return record;
}

SwfShapeRecord curve_by(std::int32_t control_dx, std::int32_t control_dy, std::int32_t anchor_dx,
                        std::int32_t anchor_dy) {
  SwfShapeRecord record{SwfShapeRecordKind::curved_edge};
  record.control_x = control_dx;
  record.control_y = control_dy;
  record.x = anchor_dx;
  record.y = anchor_dy;
  return record;
}

SwfShapeRecord style_change(std::uint16_t fill0, std::uint16_t fill1, std::uint16_t line) {
  SwfShapeRecord record{SwfShapeRecordKind::style_change};
  record.fill0 = fill0;
  record.fill1 = fill1;
  record.line = line;
  record.has_fill0 = true;
  record.has_fill1 = true;
  record.has_line = true;
  return record;
}

fo4::swf::SwfShape sized_shape(std::int32_t x_max, std::int32_t y_max) {
  fo4::swf::SwfShape shape;
  shape.bounds.x_max = x_max;
  shape.bounds.y_max = y_max;
  return shape;
}

fo4::swf::SwfFillStyle solid_fill(std::uint32_t argb) {
  fo4::swf::SwfFillStyle fill;
  fill.type = 0x00;
  fill.color = argb;
  return fill;
}

fo4::swf::SwfFillStyle bitmap_fill(std::uint8_t type, std::uint16_t bitmap_id, std::int32_t scale,
                                   std::int32_t translate_x) {
  fo4::swf::SwfFillStyle fill;
  fill.type = type;
  fill.bitmap_id = bitmap_id;
  fo4::swf::SwfMatrix matrix;
  matrix.scale_x = scale;
  matrix.scale_y = scale;
  matrix.translate_x = translate_x;
  fill.matrix = matrix;
  return fill;
}

fo4::swf::SwfReadResult two_by_two_bitmap() {
  const std::vector<std::uint8_t> raw = {255, 255, 0,   0,   255, 0,   255, 0,
                                         255, 0,   0,   255, 255, 255, 255, 255};
  return swf_art_test::read_one(36, swf_art_test::lossless_raw_payload(5, 2, 2, 0, raw));
}

std::string square_path_fill(const char* fill) {
  return std::string("<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" ") + fill + " fill-rule=\"nonzero\"/>\n";
}

const std::string kStage =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n";

const std::string kRedSquare =
    "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

const std::string kBlueSquare =
    "<path d=\"M0 0L30 0L30 30L0 30L0 0Z\" fill=\"#0000ff\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

fo4::swf::SwfShape solid_square(std::uint16_t id, std::int32_t size, std::uint32_t argb) {
  auto shape = sized_shape(size, size);
  shape.shape_id = id;
  shape.fill_styles.push_back(solid_fill(argb));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(size, 0), line_by(0, size),
                   line_by(-size, 0), line_by(0, -size)};
  return shape;
}

fo4::swf::SwfFrameInstance frame_row(std::uint16_t depth, std::uint16_t character,
                                     std::uint32_t level, bool visible = true) {
  fo4::swf::SwfFrameInstance row;
  row.depth = depth;
  row.character_id = character;
  row.nesting_level = level;
  row.world_visible = visible;
  return row;
}

fo4::swf::SwfMorphFillStyle solid_morph_fill(std::uint32_t start_argb, std::uint32_t end_argb) {
  fo4::swf::SwfMorphFillStyle fill;
  fill.type = 0x00;
  fill.start_color = start_argb;
  fill.end_color = end_argb;
  return fill;
}

fo4::swf::SwfMorphShape red_to_blue_square() {
  fo4::swf::SwfMorphShape morph;
  morph.character_id = 3;
  morph.fill_styles.push_back(solid_morph_fill(0xFFFF0000, 0xFF0000FF));
  morph.start_records = {move_to(0, 0),    style_change(1, 0, 0), line_by(100, 0),
                         line_by(0, 100), line_by(-100, 0),      line_by(0, -100)};
  morph.end_records = {move_to(0, 0), line_by(200, 0), line_by(0, 200), line_by(-200, 0),
                       line_by(0, -200)};
  return morph;
}

fo4::swf::SwfMorphShape line_morph_square() {
  fo4::swf::SwfMorphShape morph = red_to_blue_square();
  morph.fill_styles.clear();
  morph.start_records[1] = style_change(0, 0, 1);
  fo4::swf::SwfMorphLineStyle line;
  line.start_width = 20;
  line.end_width = 40;
  line.start_color = 0xFF0000FFu;
  line.end_color = 0xFF0000FFu;
  morph.line_styles.push_back(line);
  return morph;
}

std::vector<std::byte> place_object_2(std::uint8_t flags, std::uint16_t depth,
                                      const std::vector<std::uint16_t>& fields) {
  swf_test::SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  for (const auto field : fields) {
    body.u16(field);
  }
  return body.body;
}

std::vector<std::byte> ratio_movie() {
  swf_test::SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(3);
  file.tag(26, place_object_2(0x12, 1, {3, 16384}), false);
  file.tag(26, place_object_2(0x02, 2, {3}), false);
  file.tag(1);
  file.tag(26, place_object_2(0x01, 1, {}), false);
  file.tag(1);
  file.tag(26, place_object_2(0x11, 1, {65535}), false);
  file.tag(1);
  file.tag(0);
  return swf_test::wrap_body(file.body, 10, false, false);
}

const fo4::swf::SwfFrameInstance* row_at(const fo4::swf::SwfDisplayList& display,
                                         std::size_t frame, std::uint16_t depth) {
  for (const auto& row : display.timelines.front().frames[frame].instances) {
    if (row.depth == depth) {
      return &row;
    }
  }
  return nullptr;
}

std::string frame_svg(std::vector<fo4::swf::SwfShape> shapes,
                      std::vector<fo4::swf::SwfFrameInstance> rows,
                      std::optional<fo4::swf::SwfMorphShape> morph = {}) {
  fo4::swf::SwfFile file;
  file.header.frame_size.x_max = 11000;
  file.header.frame_size.y_max = 8000;
  for (auto& shape : shapes) {
    fo4::swf::SwfTag tag;
    tag.shape = std::move(shape);
    file.tags.push_back(std::move(tag));
  }
  if (morph) {
    fo4::swf::SwfTag tag;
    tag.morph_shape = std::move(morph);
    file.tags.push_back(std::move(tag));
  }
  fo4::swf::SwfDisplayList display;
  display.timelines.emplace_back();
  display.timelines.front().frames.push_back(fo4::swf::SwfDisplayFrame{0, std::move(rows)});
  return fo4::swf::frame_to_svg(file, display, 0);
}

}  // namespace

TEST(SwfSvg, FilledSquareKeepsSolidColourAndAlpha) {
  // Detects: a solid fill must export its RGB as the hex fill and its alpha byte as fill-opacity, and the closed square must end in Z.
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(solid_fill(0x80112233u));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#112233\" fill-opacity=\"0.501961\" "
      "fill-rule=\"nonzero\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, ExplicitZeroFillTurnsTheSideOffForLaterEdges) {
  // Detects: a style change that writes fill0 = 0 explicitly must turn fill0 off, so edges after it are not filled; treating the zero as unchanged would emit a second filled path.
  auto shape = sized_shape(3000, 1000);
  shape.fill_styles.push_back(solid_fill(0xFF112233u));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000), move_to(2000, 0), style_change(0, 0, 0),
                   line_by(1000, 0), line_by(0, 1000), line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 150 50\" width=\"150\" height=\"50\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#112233\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, QuadraticCurveUsesQCommandWithAnchorRelativeToControl) {
  // Detects: a curved edge must map its control point and its anchor (relative to the control point) onto SVG Q, not an L approximation.
  auto shape = sized_shape(1000, 0);
  shape.bounds.y_min = -500;
  shape.fill_styles.push_back(solid_fill(0xFF000000u));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), curve_by(500, -1000, 500, 1000),
                   line_by(-1000, 0)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 -25 50 25\" width=\"50\" height=\"25\">\n"
      "<path d=\"M0 0Q25 -50 50 0L0 0Z\" fill=\"#000000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, TwoFillStylesReverseTheRightSideEdges) {
  // Detects: an edge with a right-side fill must be reversed into that fill's path, so the left-side fill keeps the original edge direction.
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(solid_fill(0xFF112233u));
  shape.fill_styles.push_back(solid_fill(0xFF445566u));
  shape.records = {move_to(0, 0), style_change(1, 2, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#112233\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "<path d=\"M50 0L0 0L0 50L50 50L50 0Z\" fill=\"#445566\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, LinearGradientEmitsStopsAndTwipScaledTransform) {
  // Detects: a linear gradient must emit its stops, spread and gradientTransform, with the matrix translation divided by 20 twips per pixel.
  auto shape = sized_shape(1000, 1000);
  fo4::swf::SwfFillStyle gradient;
  gradient.type = 0x10;
  fo4::swf::SwfMatrix matrix;
  matrix.scale_x = 0x8000;
  matrix.translate_x = 200;
  gradient.matrix = matrix;
  gradient.stops = {{0, 0xFF000000u}, {255, 0xFFFFFFFFu}};
  shape.fill_styles.push_back(gradient);
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<defs>\n"
      "<linearGradient id=\"fill1\" x1=\"-819.2\" y1=\"0\" x2=\"819.2\" y2=\"0\" "
      "gradientUnits=\"userSpaceOnUse\" gradientTransform=\"matrix(0.5 0 0 1 10 0)\" "
      "spreadMethod=\"pad\">\n"
      "<stop offset=\"0\" stop-color=\"#000000\" stop-opacity=\"1\"/>\n"
      "<stop offset=\"1\" stop-color=\"#ffffff\" stop-opacity=\"1\"/>\n"
      "</linearGradient>\n"
      "</defs>\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"url(#fill1)\" fill-rule=\"nonzero\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, StrokeOnlyShapeMapsWidthCapAndJoinFlags) {
  // Detects: a stroke must take its width in pixels and its cap and join from the LineStyle2 flag bits (start cap in bits 15-14, join in bits 13-12), with no fill on the path.
  auto shape = sized_shape(1000, 0);
  fo4::swf::SwfLineStyle line;
  line.width = 40;
  line.color = 0xFF102030u;
  line.flags = 0x6000;
  line.miter_limit = 0x0300;
  shape.line_styles.push_back(line);
  shape.records = {move_to(0, 0), style_change(0, 0, 1), line_by(1000, 0)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 0\" width=\"50\" height=\"0\">\n"
      "<path d=\"M0 0L50 0\" stroke=\"#102030\" stroke-opacity=\"1\" fill=\"none\" "
      "stroke-width=\"2\" stroke-linecap=\"butt\" stroke-linejoin=\"miter\" "
      "stroke-miterlimit=\"3\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, ThousandsOfCollinearEdgesChainIntoOneClosedLoop) {
  // Detects: loop chaining must find the next unused edge from a shared start point without losing or splitting edges; 3000 collinear pieces of one square outline must still form one M...Z subpath.
  auto shape = sized_shape(3000, 3000);
  shape.fill_styles.push_back(solid_fill(0xFF000000u));
  shape.records = {move_to(0, 0), style_change(1, 0, 0)};
  for (int i = 0; i < 750; ++i) {
    shape.records.push_back(line_by(4, 0));
  }
  for (int i = 0; i < 750; ++i) {
    shape.records.push_back(line_by(0, 4));
  }
  for (int i = 0; i < 750; ++i) {
    shape.records.push_back(line_by(-4, 0));
  }
  for (int i = 0; i < 750; ++i) {
    shape.records.push_back(line_by(0, -4));
  }
  const std::string svg = fo4::swf::shape_to_svg(shape);
  EXPECT_EQ(std::count(svg.begin(), svg.end(), 'M'), 1);
  EXPECT_EQ(std::count(svg.begin(), svg.end(), 'L'), 3000);
  EXPECT_EQ(std::count(svg.begin(), svg.end(), 'Z'), 1);
  EXPECT_NE(svg.find("<path d=\"M0 0L0.2 0L0.4 0"), std::string::npos);
  EXPECT_NE(svg.find("L0 0Z\" fill=\"#000000\""), std::string::npos);
}

TEST(SwfSvg, ClosedStrokeLoopEndsInZ) {
  // Detects: a stroke whose edges return to their start point must close with Z so the join at the start corner is drawn; an open M...L run leaves a missing corner.
  auto shape = sized_shape(1000, 1000);
  fo4::swf::SwfLineStyle line;
  line.width = 20;
  line.color = 0xFF102030u;
  shape.line_styles.push_back(line);
  shape.records = {move_to(0, 0), style_change(0, 0, 1), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" stroke=\"#102030\" stroke-opacity=\"1\" fill=\"none\" "
      "stroke-width=\"1\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
}

TEST(SwfSvg, EmptyShapeExportsZeroViewBoxAndNoPaths) {
  // Detects: a shape with no records must still export a well-formed svg with a zero viewBox and no path elements, not an error or a stray path.
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 0 0\" width=\"0\" height=\"0\">\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(fo4::swf::SwfShape{}), expected);
}

TEST(SwfSvg, BitmapFillEmitsARepeatingPatternWithItsPngAndPixelTransform) {
  // Detects: a repeating bitmap fill must become a pattern whose image is the bitmap's PNG in base64 and whose patternTransform divides both the linear part and the translation of the fill matrix by 20 twips per pixel; a missing pattern, a wrong base64 tail, a payload that is not a PNG, or a matrix that skips the scale division changes the expected text.
  const auto bitmap = two_by_two_bitmap();
  ASSERT_TRUE(bitmap.ok()) << fo4::swf::status_name(bitmap.status);
  const auto png = fo4::swf::bitmap_to_png(*bitmap.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x40, 3, 131072, 400));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<defs>\n"
      "<pattern id=\"fillbmp1\" patternUnits=\"userSpaceOnUse\" width=\"2\" height=\"2\" "
      "patternTransform=\"matrix(0.1 0 0 0.1 20 0)\">\n"
      "<image width=\"2\" height=\"2\" href=\"data:image/png;base64," +
      swf_art_test::base64_text(*png) +
      "\"/>\n"
      "</pattern>\n"
      "</defs>\n" +
      square_path_fill("fill=\"url(#fillbmp1)\"") + "</svg>\n";
  const std::string svg = fo4::swf::shape_to_svg(shape, bitmap.file);
  EXPECT_EQ(svg, expected);
  EXPECT_NE(svg.find("href=\"data:image/png;base64,iVBORw0KGgo"), std::string::npos);
}

TEST(SwfSvg, NonSmoothedBitmapFillRendersPixelated) {
  // Detects: a non-smoothed bitmap fill (type 0x42) must carry image-rendering="pixelated" so hard-edged pixels are not blurred on scaling.
  const auto bitmap = two_by_two_bitmap();
  ASSERT_TRUE(bitmap.ok()) << fo4::swf::status_name(bitmap.status);
  const auto png = fo4::swf::bitmap_to_png(*bitmap.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x42, 3, 131072, 0));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<defs>\n"
      "<pattern id=\"fillbmp1\" patternUnits=\"userSpaceOnUse\" width=\"2\" height=\"2\" "
      "patternTransform=\"matrix(0.1 0 0 0.1 0 0)\">\n"
      "<image width=\"2\" height=\"2\" href=\"data:image/png;base64," +
      swf_art_test::base64_text(*png) +
      "\" image-rendering=\"pixelated\"/>\n"
      "</pattern>\n"
      "</defs>\n" +
      square_path_fill("fill=\"url(#fillbmp1)\"") + "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape, bitmap.file), expected);
}

TEST(SwfSvg, ClippedBitmapFillDrawsItsImageOnceInsideItsPath) {
  // Detects: a clipped bitmap fill (0x41) must draw its image once under a clipPath made from the fill's own path; a <pattern> would tile the bitmap across the plane, and an image without the clip would paint outside the path.
  const auto bitmap = two_by_two_bitmap();
  ASSERT_TRUE(bitmap.ok()) << fo4::swf::status_name(bitmap.status);
  const auto png = fo4::swf::bitmap_to_png(*bitmap.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x41, 3, 131072, 400));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<clipPath id=\"fillbmp1clip\"><path d=\"M0 0L50 0L50 50L0 50L0 0Z\"/></clipPath>\n"
      "<g clip-path=\"url(#fillbmp1clip)\">\n"
      "<image transform=\"matrix(0.1 0 0 0.1 20 0)\" width=\"2\" height=\"2\" "
      "href=\"data:image/png;base64," +
      swf_art_test::base64_text(*png) +
      "\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape, bitmap.file), expected);
}

TEST(SwfSvg, ClippedNonSmoothedBitmapFillRendersPixelatedImageInItsPath) {
  // Detects: a clipped non-smoothed bitmap fill (0x43) must keep image-rendering="pixelated" on its single image, since 0x43 is the only clipped type with hard-edged pixels.
  const auto bitmap = two_by_two_bitmap();
  ASSERT_TRUE(bitmap.ok()) << fo4::swf::status_name(bitmap.status);
  const auto png = fo4::swf::bitmap_to_png(*bitmap.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x43, 3, 131072, 0));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n"
      "<clipPath id=\"fillbmp1clip\"><path d=\"M0 0L50 0L50 50L0 50L0 0Z\"/></clipPath>\n"
      "<g clip-path=\"url(#fillbmp1clip)\">\n"
      "<image transform=\"matrix(0.1 0 0 0.1 0 0)\" width=\"2\" height=\"2\" "
      "href=\"data:image/png;base64," +
      swf_art_test::base64_text(*png) +
      "\" image-rendering=\"pixelated\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape, bitmap.file), expected);
}

TEST(SwfSvg, ClippedBitmapFillStaysPlainGeometryInsideAMaskClipPath) {
  // Detects: a mask's clipPath is built from shape_fill_paths_to_svg, which must keep a clipped bitmap fill as a plain path; a clip group with an <image> is not a valid clipPath child, so the mask would clip everything out.
  const auto bitmap = two_by_two_bitmap();
  ASSERT_TRUE(bitmap.ok()) << fo4::swf::status_name(bitmap.status);
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x41, 3, 131072, 400));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const auto bitmaps = fo4::swf::index_bitmaps(bitmap.file);
  const auto images = fo4::swf::shape_images(shape, bitmaps);
  EXPECT_EQ(fo4::swf::shape_fill_paths_to_svg(shape, "fill", images),
            square_path_fill("fill=\"#808080\""));
}

TEST(SwfSvg, MissingOrUnencodableBitmapFallsBackToThePlaceholder) {
  // Detects: a bitmap fill whose id is absent from the file, or whose bitmap bitmap_to_png refuses, must keep the grey placeholder with no pattern; a url to a pattern that was never emitted would leave the path unpainted, and a crash would fail the whole export.
  auto shape = sized_shape(1000, 1000);
  shape.fill_styles.push_back(bitmap_fill(0x40, 9, 131072, 400));
  shape.records = {move_to(0, 0), style_change(1, 0, 0), line_by(1000, 0), line_by(0, 1000),
                   line_by(-1000, 0), line_by(0, -1000)};
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 50 50\" width=\"50\" height=\"50\">\n" +
      square_path_fill("fill=\"#808080\"") + "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(shape, fo4::swf::SwfFile{}), expected);
  EXPECT_EQ(fo4::swf::shape_to_svg(shape), expected);
  const auto format4 =
      swf_art_test::read_one(20, swf_art_test::lossless_payload(4, 2, 2, 4, 0, false));
  ASSERT_TRUE(format4.ok()) << fo4::swf::status_name(format4.status);
  shape.fill_styles[0] = bitmap_fill(0x40, 3, 131072, 400);
  EXPECT_EQ(fo4::swf::shape_to_svg(shape, format4.file), expected);
}

TEST(SwfFrameSvg, HiddenRowWithAClipDepthStillOpensItsMask) {
  // Detects: a hidden row with a clip depth opening no mask, or painting its own geometry; the visible row at depth 2 must be clipped by the hidden row's square and the hidden row paints nothing. Flash applying a hidden mask is recalled, not verified against the player.
  auto mask = frame_row(1, 1, 0, false);
  mask.clip_depth = 2;
  const std::string expected =
      kStage +
      "<clipPath id=\"r0clip\" transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</clipPath>\n"
      "<g clip-path=\"url(#r0clip)\">\n"
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kBlueSquare + "</g>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({solid_square(1, 1000, 0xFFFF0000), solid_square(2, 600, 0xFF0000FF)},
                      {mask, frame_row(2, 2, 0)}),
            expected);
}

TEST(SwfFrameSvg, ASpriteMaskClipsWithItsShapeChildrenAndPaintsNothingItself) {
  // Detects: a sprite row with a clip depth drawing its children unmasked, or a clipPath built from the sprite instead of its shape children; the red child is offset by 20 px, so a clipPath that drops the child transform puts it at the origin.
  auto mask = frame_row(1, 9, 0);
  mask.is_sprite = true;
  mask.clip_depth = 2;
  auto red_child = frame_row(1, 1, 1);
  red_child.world_transform.translate_x = 20;
  const std::string expected =
      kStage +
      "<clipPath id=\"r0clip\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" transform=\"matrix(1 0 0 1 20 0)\" fill=\"#ff0000\" "
      "fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "<path d=\"M0 0L30 0L30 30L0 30L0 0Z\" transform=\"matrix(1 0 0 1 0 0)\" fill=\"#0000ff\" "
      "fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</clipPath>\n"
      "<g clip-path=\"url(#r0clip)\">\n"
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kBlueSquare + "</g>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({solid_square(1, 1000, 0xFFFF0000), solid_square(2, 600, 0xFF0000FF)},
                      {mask, red_child, frame_row(2, 2, 1), frame_row(2, 2, 0)}),
            expected);
}

TEST(SwfFrameSvg, MorphRowAtRatioZeroDrawsTheStartShape) {
  auto row = frame_row(1, 3, 0);
  row.ratio = 0;
  const std::string expected =
      kStage +
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L5 0L5 5L0 5L0 0Z\" fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({}, {row}, red_to_blue_square()), expected);
}

TEST(SwfFrameSvg, MorphRowAtRatioHalfWayBlendsEdgesAndColour) {
  auto row = frame_row(1, 3, 0);
  row.ratio = 32768;
  const std::string expected =
      kStage +
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L7.5 0L7.5 7.5L0 7.5L0 0Z\" fill=\"#7f0080\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({}, {row}, red_to_blue_square()), expected);
}

TEST(SwfFrameSvg, MorphLineWidthBlendsIntoAStrokeAtHalfWay) {
  auto row = frame_row(1, 3, 0);
  row.ratio = 32768;
  const std::string expected =
      kStage +
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L7.5 0L7.5 7.5L0 7.5L0 0Z\" stroke=\"#0000ff\" stroke-opacity=\"1\" fill=\"none\" "
      "stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({}, {row}, line_morph_square()), expected);
}

TEST(SwfFrameSvg, MorphWithAGradientFillDrawsNothing) {
  auto morph = red_to_blue_square();
  morph.fill_styles[0].type = 0x10;
  auto row = frame_row(1, 3, 0);
  row.ratio = 32768;
  EXPECT_EQ(frame_svg({}, {row}, morph), kStage + "</svg>\n");
}

TEST(SwfFrameSvg, PlaceObjectRatioIsKeptAcrossAMoveAndOverridden) {
  const auto result = fo4::swf::read_swf(ratio_movie());
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  ASSERT_EQ(display.status, fo4::swf::SwfTimelineStatus::ok);
  ASSERT_EQ(display.timelines.front().frames.size(), 3u);
  for (std::size_t frame = 0; frame < 3; ++frame) {
    ASSERT_NE(row_at(display, frame, 1), nullptr);
    ASSERT_NE(row_at(display, frame, 2), nullptr);
    EXPECT_FALSE(row_at(display, frame, 2)->ratio.has_value());
  }
  EXPECT_EQ(row_at(display, 0, 1)->ratio, 16384);
  EXPECT_EQ(row_at(display, 1, 1)->ratio, 16384);
  EXPECT_EQ(row_at(display, 2, 1)->ratio, 65535);
}

TEST(SwfFrameSvg, MorphRowAtFullRatioDrawsTheEndShape) {
  auto row = frame_row(1, 3, 0);
  row.ratio = 65535;
  const std::string expected =
      kStage +
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L10 0L10 10L0 10L0 0Z\" fill=\"#0000ff\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(frame_svg({}, {row}, red_to_blue_square()), expected);
}
