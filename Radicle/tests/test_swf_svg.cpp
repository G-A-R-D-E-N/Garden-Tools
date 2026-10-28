#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_svg.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

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

TEST(SwfSvg, EmptyShapeExportsZeroViewBoxAndNoPaths) {
  // Detects: a shape with no records must still export a well-formed svg with a zero viewBox and no path elements, not an error or a stray path.
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 0 0\" width=\"0\" height=\"0\">\n"
      "</svg>\n";
  EXPECT_EQ(fo4::swf::shape_to_svg(fo4::swf::SwfShape{}), expected);
}
