#include "swf_art_fixtures.hpp"

#include "fo4/swf/swf_bitmap_png.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace swf_art_test;

TEST(SwfShape, DefineShape3DecodesStylesAndEdgeRecords) {
  // Regression: styles must use their version's colour depth and every edge form must be consumed.
  const auto result = read_one(32, define_shape_3_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  EXPECT_EQ(shape.shape_id, 5u);
  EXPECT_EQ(shape.bounds.x_max, 2000);
  EXPECT_EQ(shape.bounds.y_max, 1000);
  ASSERT_EQ(shape.fill_styles.size(), 3u);
  EXPECT_EQ(shape.fill_styles[0].color, 0xFF112233u);
  EXPECT_EQ(shape.fill_styles[1].type, 0x10u);
  EXPECT_EQ(shape.fill_styles[1].spread, 1u);
  ASSERT_EQ(shape.fill_styles[1].stops.size(), 2u);
  EXPECT_EQ(shape.fill_styles[1].stops[0].ratio, 0u);
  EXPECT_EQ(shape.fill_styles[1].stops[1].ratio, 255u);
  EXPECT_EQ(shape.fill_styles[1].stops[1].color, 0x800000FFu);
  EXPECT_EQ(shape.fill_styles[2].bitmap_id, 7u);
  EXPECT_TRUE(shape.fill_styles[2].matrix.has_value());
  ASSERT_EQ(shape.line_styles.size(), 1u);
  EXPECT_EQ(shape.line_styles[0].width, 20u);
  EXPECT_EQ(shape.line_styles[0].color, 0xFF090807u);
  EXPECT_EQ(shape.edge_count, 4u);
  EXPECT_EQ(shape.curved_edge_count, 1u);
  EXPECT_EQ(shape.style_change_count, 1u);
}

TEST(SwfShape, KeepsEveryRecordAsADrawCommandInFileOrder) {
  const auto result = read_one(32, define_shape_3_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  ASSERT_EQ(shape.records.size(), 6u);
  EXPECT_EQ(shape.records[0].kind, fo4::swf::SwfShapeRecordKind::move);
  EXPECT_EQ(shape.records[0].x, 100);
  EXPECT_EQ(shape.records[0].y, -50);
  EXPECT_EQ(shape.records[1].kind, fo4::swf::SwfShapeRecordKind::style_change);
  EXPECT_EQ(shape.records[1].fill0, 1);
  EXPECT_EQ(shape.records[1].fill1, 2);
  EXPECT_EQ(shape.records[1].line, 1);
  EXPECT_EQ(shape.records[2].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(shape.records[2].x, 10);
  EXPECT_EQ(shape.records[2].y, -20);
  EXPECT_EQ(shape.records[3].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(shape.records[3].x, 0);
  EXPECT_EQ(shape.records[3].y, 30);
  EXPECT_EQ(shape.records[4].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(shape.records[4].x, -15);
  EXPECT_EQ(shape.records[4].y, 0);
  EXPECT_EQ(shape.records[5].kind, fo4::swf::SwfShapeRecordKind::curved_edge);
  EXPECT_EQ(shape.records[5].control_x, 5);
  EXPECT_EQ(shape.records[5].control_y, 5);
  EXPECT_EQ(shape.records[5].x, 10);
  EXPECT_EQ(shape.records[5].y, 0);
}

TEST(SwfShape, DefineShape4ReadsEdgeBoundsAndLineStyle2) {
  // Regression: a LineStyle2 must consume its miter factor and the fill style behind HasFill.
  const auto result = read_one(83, define_shape_4_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  EXPECT_TRUE(shape.has_edge_bounds);
  EXPECT_EQ(shape.flags, 0x04u);
  EXPECT_EQ(shape.bounds.x_max, 1000);
  EXPECT_EQ(shape.edge_bounds.x_max, 900);
  ASSERT_EQ(shape.line_styles.size(), 2u);
  EXPECT_EQ(shape.line_styles[0].flags, 0x6000u);
  ASSERT_TRUE(shape.line_styles[0].miter_limit.has_value());
  EXPECT_EQ(*shape.line_styles[0].miter_limit, 0x0300u);
  EXPECT_FALSE(shape.line_styles[0].fill.has_value());
  EXPECT_EQ(shape.line_styles[0].color, 0xFF0A0B0Cu);
  EXPECT_EQ(shape.line_styles[1].flags, 0x0800u);
  EXPECT_FALSE(shape.line_styles[1].miter_limit.has_value());
  ASSERT_TRUE(shape.line_styles[1].fill.has_value());
  EXPECT_EQ(shape.line_styles[1].fill->color, 0xFF778899u);
  EXPECT_EQ(shape.edge_count, 0u);
  EXPECT_EQ(shape.style_change_count, 1u);
}

TEST(SwfShape, NewStylesRecordKeepsEveryGenerationAndRebasesTheIndices) {
  const auto result = read_one(22, define_shape_2_new_styles_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  ASSERT_EQ(shape.fill_styles.size(), 3u);
  EXPECT_EQ(shape.fill_styles[0].color, 0xFF010203u);
  EXPECT_EQ(shape.fill_styles[1].color, 0xFF040506u);
  EXPECT_EQ(shape.fill_styles[2].color, 0xFF070809u);
  EXPECT_TRUE(shape.line_styles.empty());
  EXPECT_EQ(shape.style_change_count, 2u);
  EXPECT_EQ(shape.edge_count, 0u);
  ASSERT_EQ(shape.records.size(), 1u);
  EXPECT_EQ(shape.records[0].kind, fo4::swf::SwfShapeRecordKind::style_change);
  EXPECT_EQ(shape.records[0].fill0, 3);

  const auto with_lines = read_one(22, define_shape_2_new_styles_lines_payload());
  ASSERT_TRUE(with_lines.ok()) << fo4::swf::status_name(with_lines.status);
  const auto& extended = *with_lines.file.tags[0].shape;
  ASSERT_EQ(extended.fill_styles.size(), 3u);
  EXPECT_EQ(extended.fill_styles[0].color, 0xFF010203u);
  EXPECT_EQ(extended.fill_styles[2].color, 0xFF0B0C0Du);
  ASSERT_EQ(extended.line_styles.size(), 2u);
  EXPECT_EQ(extended.line_styles[0].width, 5u);
  EXPECT_EQ(extended.line_styles[1].width, 7u);
  EXPECT_EQ(extended.line_styles[1].color, 0xFF0E0F10u);
  EXPECT_EQ(extended.style_change_count, 2u);
  ASSERT_EQ(extended.records.size(), 1u);
  EXPECT_EQ(extended.records[0].fill0, 2);
  EXPECT_EQ(extended.records[0].line, 2);
}

TEST(SwfShape, ExtendedStyleCountsUseTheTwoByteForm) {
  // Regression: a 0xFF style count in a version 2 shape must be read as the extended UI16 count.
  const auto result = read_one(22, define_shape_2_extended_counts_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  ASSERT_EQ(shape.fill_styles.size(), 3u);
  EXPECT_EQ(shape.fill_styles[2].color, 0xFF303030u);
  EXPECT_TRUE(shape.line_styles.empty());
}

TEST(SwfShape, StyleCountIsBoundedByTheRemainingBytes) {
  // Regression: an extended count must not exceed the bytes that could encode it.
  SwfBuilder body;
  shape_head(body, 8);
  body.u8(0xFF);
  body.u16(0xFFFF);
  body.u8(0);
  body.u8(0x10);
  body.u8(0);
  const auto result = read_one(22, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(result.file.tags[0].shape.has_value());
}

TEST(SwfShape, BytesAfterTheEndRecordAreMalformed) {
  // Regression: a payload must be refused when bytes follow its end record.
  auto payload = define_shape_3_payload();
  payload.push_back(std::byte{0});
  const auto result = read_one(32, payload);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  ASSERT_EQ(result.file.tags.size(), 2u);
  EXPECT_EQ(result.file.tags[0].payload.size(), payload.size());
  EXPECT_FALSE(result.file.tags[0].shape.has_value());
}

TEST(SwfShape, StyleIndexPastTheFillArrayIsMalformed) {
  SwfBuilder body;
  shape_head(body, 9);
  body.u8(1);
  solid_fill(body, false, 0x112233);
  body.u8(0);
  body.u8(0x20);
  body.push_bits(0, 1);
  body.push_bits(0x06, 5);
  body.push_bits(2, 2);
  body.push_bits(1, 2);
  body.align();
  const auto result = read_one(22, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(result.file.tags[0].shape.has_value());
}

TEST(SwfShape, StyleIndexPastTheLineArrayIsMalformed) {
  SwfBuilder body;
  shape_head(body, 10);
  body.u8(0);
  body.u8(1);
  body.u16(10);
  rgb(body, 0x01, 0x02, 0x03);
  body.u8(0x02);
  body.push_bits(0, 1);
  body.push_bits(0x08, 5);
  body.push_bits(2, 2);
  body.align();
  const auto result = read_one(22, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
}

TEST(SwfShape, NewStylesRecordMayIndexTheArraysItInstalls) {
  SwfBuilder body;
  shape_head(body, 11);
  body.u8(0);
  body.u8(0);
  body.u8(0x20);
  body.push_bits(0, 1);
  body.push_bits(0x12, 5);
  body.push_bits(1, 2);
  body.align();
  body.u8(1);
  solid_fill(body, false, 0x445566);
  body.u8(0);
  body.u8(0x20);
  body.push_bits(0, 1);
  body.align();
  const auto result = read_one(22, body.body);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_EQ(result.file.tags[0].shape->fill_styles.size(), 1u);
  EXPECT_EQ(result.file.tags[0].shape->fill_styles[0].color, 0xFF445566u);
}

TEST(SwfMorph, DefineMorphShape2DecodesBothEdgeStreams) {
  // Regression: the morph end edge stream must be found from the offset and its own bit widths.
  const auto result = read_one(84, morph_payload(true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.character_id, 12u);
  EXPECT_EQ(morph.version, 2u);
  EXPECT_TRUE(morph.has_edge_bounds);
  EXPECT_EQ(morph.flags, 0x02u);
  EXPECT_EQ(morph.start_bounds.x_max, 800);
  EXPECT_EQ(morph.end_bounds.x_max, 900);
  EXPECT_EQ(morph.start_edge_bounds.x_max, 700);
  EXPECT_EQ(morph.end_edge_bounds.x_max, 750);
  EXPECT_EQ(morph.fill_style_count, 2u);
  EXPECT_EQ(morph.line_style_count, 1u);
  EXPECT_EQ(morph.start_edge_count, 1u);
  EXPECT_EQ(morph.end_edge_count, 1u);
}

TEST(SwfMorph, DefineMorphShapeOneHasNoEdgeBoundsOrLineFlags) {
  // Regression: DefineMorphShape must be read without the version 2 edge bounds and line flags.
  const auto result = read_one(46, morph_payload(false));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.version, 1u);
  EXPECT_FALSE(morph.has_edge_bounds);
  EXPECT_EQ(morph.start_edge_bounds.x_max, morph.start_bounds.x_max);
  EXPECT_EQ(morph.end_edge_bounds.x_max, morph.end_bounds.x_max);
  EXPECT_EQ(morph.fill_style_count, 2u);
  EXPECT_EQ(morph.line_style_count, 1u);
  EXPECT_EQ(morph.start_edge_count, 1u);
  EXPECT_EQ(morph.end_edge_count, 1u);
}

TEST(SwfMorph, KeepsTheStartAndEndStylePairs) {
  const auto result = read_one(46, morph_payload(false));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  ASSERT_EQ(morph.fill_styles.size(), 2u);
  EXPECT_EQ(morph.fill_styles[0].type, 0x00u);
  EXPECT_EQ(morph.fill_styles[0].start_color, 0x04010203u);
  EXPECT_EQ(morph.fill_styles[0].end_color, 0x08050607u);
  EXPECT_EQ(morph.fill_styles[1].type, 0x10u);
  ASSERT_TRUE(morph.fill_styles[1].start_matrix.has_value());
  ASSERT_TRUE(morph.fill_styles[1].end_matrix.has_value());
  EXPECT_EQ(morph.fill_styles[1].start_matrix->translate_x, 0);
  ASSERT_EQ(morph.fill_styles[1].stops.size(), 1u);
  EXPECT_EQ(morph.fill_styles[1].stops[0].start_ratio, 0u);
  EXPECT_EQ(morph.fill_styles[1].stops[0].start_color, 0x0C090A0Bu);
  EXPECT_EQ(morph.fill_styles[1].stops[0].end_ratio, 200u);
  EXPECT_EQ(morph.fill_styles[1].stops[0].end_color, 0x100D0E0Fu);
  ASSERT_EQ(morph.line_styles.size(), 1u);
  EXPECT_EQ(morph.line_styles[0].start_width, 30u);
  EXPECT_EQ(morph.line_styles[0].end_width, 60u);
  EXPECT_EQ(morph.line_styles[0].flags, 0u);
  EXPECT_EQ(morph.line_styles[0].start_color, 0x14111213u);
  EXPECT_EQ(morph.line_styles[0].end_color, 0x18151617u);
  EXPECT_FALSE(morph.line_styles[0].fill.has_value());
}

TEST(SwfMorph, KeepsAMorphLineStyleFill) {
  const auto result = read_one(84, morph_payload(true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  ASSERT_EQ(morph.line_styles.size(), 1u);
  const auto& line = morph.line_styles[0];
  EXPECT_EQ(line.flags, 0x0800u);
  EXPECT_FALSE(line.start_miter_limit.has_value());
  ASSERT_TRUE(line.fill.has_value());
  EXPECT_EQ(line.fill->type, 0x00u);
  EXPECT_EQ(line.fill->start_color, 0x14111213u);
  EXPECT_EQ(line.fill->end_color, 0x18151617u);
}

TEST(SwfMorph, KeepsEveryFillAndLineKindTheTagCarries) {
  const auto result = read_one(84, morph_every_kind_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  ASSERT_EQ(morph.fill_styles.size(), 8u);
  EXPECT_EQ(morph.fill_style_count, 8u);
  ASSERT_EQ(morph.line_styles.size(), 2u);
  EXPECT_EQ(morph.line_style_count, 2u);

  const auto& solid = morph.fill_styles[0];
  EXPECT_EQ(solid.type, 0x00u);
  EXPECT_EQ(solid.start_color, 0x44112233u);
  EXPECT_EQ(solid.end_color, 0x88556677u);
  EXPECT_FALSE(solid.start_matrix.has_value());
  EXPECT_TRUE(solid.stops.empty());

  const auto& linear = morph.fill_styles[1];
  EXPECT_EQ(linear.type, 0x10u);
  ASSERT_TRUE(linear.start_matrix.has_value());
  ASSERT_TRUE(linear.end_matrix.has_value());
  EXPECT_EQ(linear.start_matrix->scale_x, 0x10000);
  EXPECT_EQ(linear.start_matrix->translate_x, 5);
  EXPECT_EQ(linear.start_matrix->translate_y, 6);
  EXPECT_EQ(linear.end_matrix->translate_x, -5);
  EXPECT_EQ(linear.end_matrix->translate_y, -6);
  EXPECT_EQ(linear.spread, 1u);
  EXPECT_EQ(linear.interpolation, 1u);
  ASSERT_EQ(linear.stops.size(), 1u);
  EXPECT_EQ(linear.stops[0].start_ratio, 10u);
  EXPECT_EQ(linear.stops[0].start_color, 0x04010203u);
  EXPECT_EQ(linear.stops[0].end_ratio, 20u);
  EXPECT_EQ(linear.stops[0].end_color, 0x08050607u);
  EXPECT_FALSE(linear.start_focal_point.has_value());

  const auto& radial = morph.fill_styles[2];
  EXPECT_EQ(radial.type, 0x12u);
  EXPECT_EQ(radial.spread, 2u);
  EXPECT_EQ(radial.interpolation, 2u);
  ASSERT_TRUE(radial.start_matrix.has_value());
  EXPECT_EQ(radial.start_matrix->translate_x, 7);
  EXPECT_EQ(radial.end_matrix->translate_y, -8);
  ASSERT_EQ(radial.stops.size(), 2u);
  EXPECT_EQ(radial.stops[1].start_ratio, 50u);
  EXPECT_EQ(radial.stops[1].start_color, 0x14111213u);
  EXPECT_EQ(radial.stops[1].end_ratio, 60u);
  EXPECT_EQ(radial.stops[1].end_color, 0x18151617u);
  EXPECT_FALSE(radial.start_focal_point.has_value());

  const auto& focal = morph.fill_styles[3];
  EXPECT_EQ(focal.type, 0x13u);
  ASSERT_TRUE(focal.start_matrix.has_value());
  EXPECT_EQ(focal.start_matrix->translate_x, 9);
  EXPECT_EQ(focal.end_matrix->translate_x, -9);
  ASSERT_EQ(focal.stops.size(), 1u);
  EXPECT_EQ(focal.stops[0].start_color, 0x1C191A1Bu);
  EXPECT_EQ(focal.stops[0].end_ratio, 80u);
  ASSERT_TRUE(focal.start_focal_point.has_value());
  ASSERT_TRUE(focal.end_focal_point.has_value());
  EXPECT_EQ(*focal.start_focal_point, 0x0140);
  EXPECT_EQ(*focal.end_focal_point, 0x00C0);

  const std::uint8_t bitmap_kinds[] = {0x40, 0x41, 0x42, 0x43};
  for (unsigned index = 0; index < 4; ++index) {
    const auto& bitmap = morph.fill_styles[4 + index];
    EXPECT_EQ(bitmap.type, bitmap_kinds[index]);
    EXPECT_EQ(bitmap.bitmap_id, static_cast<std::uint16_t>(21u + index));
    ASSERT_TRUE(bitmap.start_matrix.has_value());
    ASSERT_TRUE(bitmap.end_matrix.has_value());
    EXPECT_EQ(bitmap.start_matrix->translate_x, static_cast<std::int32_t>(11 + 2 * index));
    EXPECT_EQ(bitmap.end_matrix->translate_y, -static_cast<std::int32_t>(12 + 2 * index));
    EXPECT_TRUE(bitmap.stops.empty());
    EXPECT_FALSE(bitmap.start_focal_point.has_value());
  }

  const auto& miter = morph.line_styles[0];
  EXPECT_EQ(miter.start_width, 40u);
  EXPECT_EQ(miter.end_width, 80u);
  EXPECT_EQ(miter.flags, 0x2600u);
  ASSERT_TRUE(miter.start_miter_limit.has_value());
  ASSERT_TRUE(miter.end_miter_limit.has_value());
  EXPECT_EQ(*miter.start_miter_limit, 0x0300u);
  EXPECT_EQ(*miter.end_miter_limit, 0x0480u);
  EXPECT_EQ(miter.start_color, 0x33303132u);
  EXPECT_EQ(miter.end_color, 0x37343536u);
  EXPECT_FALSE(miter.fill.has_value());

  const auto& filled = morph.line_styles[1];
  EXPECT_EQ(filled.start_width, 5u);
  EXPECT_EQ(filled.end_width, 10u);
  EXPECT_EQ(filled.flags, 0x0800u);
  EXPECT_FALSE(filled.start_miter_limit.has_value());
  EXPECT_EQ(filled.start_color, 0u);
  EXPECT_EQ(filled.end_color, 0u);
  ASSERT_TRUE(filled.fill.has_value());
  EXPECT_EQ(filled.fill->type, 0x10u);
  EXPECT_EQ(filled.fill->interpolation, 2u);
  ASSERT_TRUE(filled.fill->start_matrix.has_value());
  EXPECT_EQ(filled.fill->start_matrix->translate_x, 19);
  EXPECT_EQ(filled.fill->end_matrix->translate_x, -19);
  ASSERT_EQ(filled.fill->stops.size(), 2u);
  EXPECT_EQ(filled.fill->stops[0].end_color, 0x47444546u);
  EXPECT_EQ(filled.fill->stops[1].start_ratio, 110u);
  EXPECT_EQ(filled.fill->stops[1].end_color, 0x4F4C4D4Eu);
}

TEST(SwfMorph, NewStylesBlockKeepsBothGenerationsOnTheTag) {
  const auto result = read_one(46, morph_new_styles_payload(morph_nested_fill_kinds()));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  ASSERT_EQ(morph.fill_styles.size(), 8u);
  EXPECT_EQ(morph.fill_styles[1].type, 0x10u);
  EXPECT_EQ(morph.fill_styles[3].type, 0x13u);
  EXPECT_EQ(morph.fill_styles[4].type, 0x42u);
  EXPECT_EQ(morph.fill_styles[7].type, 0x42u);
  ASSERT_EQ(morph.line_styles.size(), 3u);
  EXPECT_EQ(morph.line_styles[0].start_width, 30u);
  EXPECT_EQ(morph.line_styles[1].start_width, 40u);
  ASSERT_EQ(morph.start_records.size(), 1u);
  EXPECT_EQ(morph.start_records[0].kind, fo4::swf::SwfShapeRecordKind::style_change);
  EXPECT_EQ(morph.start_records[0].fill0, 3);
  ASSERT_EQ(morph.end_records.size(), 1u);
  EXPECT_EQ(morph.end_records[0].fill0, 6);
}

TEST(SwfMorph, NewStylesRecordParsesMorphStylePairs) {
  const auto result = read_one(46, morph_new_styles_payload(morph_nested_solid()));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.character_id, 14u);
  EXPECT_EQ(morph.fill_style_count, 4u);
  EXPECT_EQ(morph.line_style_count, 1u);
  EXPECT_EQ(morph.start_edge_count, 0u);
  EXPECT_EQ(morph.end_edge_count, 0u);
  ASSERT_EQ(morph.start_records.size(), 1u);
  EXPECT_EQ(morph.start_records[0].fill0, 3);
  ASSERT_EQ(morph.end_records.size(), 1u);
  EXPECT_EQ(morph.end_records[0].fill0, 4);
}

TEST(SwfMorph, NewStylesRecordParsesMorphGradientAndBitmapFills) {
  // A nested block must walk the gradient and bitmap arms, or the record after it desyncs.
  const auto result = read_one(46, morph_new_styles_payload(morph_nested_fill_kinds()));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.character_id, 14u);
  EXPECT_EQ(morph.fill_style_count, 8u);
  EXPECT_EQ(morph.line_style_count, 3u);
  EXPECT_EQ(morph.start_edge_count, 0u);
  EXPECT_EQ(morph.end_edge_count, 0u);
}

TEST(SwfMorph, KeepsBothEdgeStreamsAsDrawCommands) {
  const auto result = read_one(84, morph_payload(true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  ASSERT_EQ(morph.start_records.size(), 2u);
  EXPECT_EQ(morph.start_records[0].kind, fo4::swf::SwfShapeRecordKind::move);
  EXPECT_EQ(morph.start_records[0].x, 1);
  EXPECT_EQ(morph.start_records[0].y, 2);
  EXPECT_EQ(morph.start_records[1].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(morph.start_records[1].x, 4);
  EXPECT_EQ(morph.start_records[1].y, 6);
  ASSERT_EQ(morph.end_records.size(), 2u);
  EXPECT_EQ(morph.end_records[0].kind, fo4::swf::SwfShapeRecordKind::move);
  EXPECT_EQ(morph.end_records[0].x, -1);
  EXPECT_EQ(morph.end_records[0].y, -2);
  EXPECT_EQ(morph.end_records[1].kind, fo4::swf::SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(morph.end_records[1].x, -4);
  EXPECT_EQ(morph.end_records[1].y, -6);
  EXPECT_EQ(morph.start_edge_count, 1u);
  EXPECT_EQ(morph.end_edge_count, 1u);
}

TEST(SwfMorph, StyleIndexPastTheStyleArraysIsMalformed) {
  auto styles = morph_styles(false);
  styles.back() = std::byte{0x20};
  SwfBuilder start;
  start.bytes(styles);
  start.push_bits(0, 1);
  start.push_bits(0x02, 5);
  start.push_bits(3, 2);
  start.push_bits(0, 6);
  start.align();
  SwfBuilder body;
  body.u16(16);
  body.rect(0, 100, 0, 100, 10);
  body.rect(0, 120, 0, 90, 10);
  body.u32(static_cast<std::uint32_t>(start.body.size()));
  body.bytes(start.body);
  body.u8(0);
  body.push_bits(0, 1);
  body.push_bits(0, 5);
  body.align();
  const auto result = read_one(46, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
}

TEST(SwfMorph, StyleCountIsBoundedByTheRemainingBytes) {
  // Regression: an extended morph style count must not exceed the bytes that could encode it.
  SwfBuilder body;
  body.u16(15);
  body.rect(0, 100, 0, 100, 10);
  body.rect(0, 120, 0, 90, 10);
  body.u8(0xFF);
  body.u16(0xFFFF);
  body.u8(0);
  const auto result = read_one(46, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(result.file.tags[0].morph_shape.has_value());
}

TEST(SwfBitmap, LosslessRgb15InflatesToThePaddedRowSize) {
  // Regression: a lossless row must be padded to four bytes, or every odd width is refused.
  const auto result = read_one(20, lossless_payload(4, 3, 2, 8, 0, false));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& bitmap = *result.file.tags[0].bitmap;
  EXPECT_EQ(bitmap.character_id, 3u);
  EXPECT_EQ(bitmap.format, 4u);
  EXPECT_EQ(bitmap.width, 3u);
  EXPECT_EQ(bitmap.height, 2u);
  EXPECT_FALSE(bitmap.color_table_size.has_value());
  EXPECT_GT(bitmap.data_size, 0u);
}

TEST(SwfBitmap, LosslessFirstFormatUsesEightBitRows) {
  // Regression: format 3 must use one byte per pixel after the palette in the same stream.
  const auto result = read_one(20, lossless_payload(3, 5, 3, 8, 3, false));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& bitmap = *result.file.tags[0].bitmap;
  ASSERT_TRUE(bitmap.color_table_size.has_value());
  EXPECT_EQ(*bitmap.color_table_size, 3u);
  EXPECT_EQ(bitmap.width, 5u);
  EXPECT_GT(bitmap.data_size, 0u);
}

TEST(SwfBitmap, LosslessColourMapInflatesThePaletteInsideTheStream) {
  // Regression: COLORMAPDATA is one compressed block, so the palette must inflate with the rows.
  const auto result = read_one(36, lossless_payload(3, 2, 2, 4, 0, true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& bitmap = *result.file.tags[0].bitmap;
  ASSERT_TRUE(bitmap.color_table_size.has_value());
  EXPECT_EQ(*bitmap.color_table_size, 0u);
  auto raw = colour_map_palette(0, true);
  raw.insert(raw.end(), 8, 0x7F);
  EXPECT_EQ(bitmap.data_size, zlib_bytes(raw).size());
}

TEST(SwfBitmap, LosslessColourMapRequiresThePaletteInTheStream) {
  // Regression: the inflated size must be the palette plus the rows, not the rows alone.
  SwfBuilder body;
  body.u16(3);
  body.u8(3);
  body.u16(2);
  body.u16(2);
  body.u8(0);
  body.bytes(zlib_bytes(std::vector<std::uint8_t>(8, 0x7F)));
  const auto result = read_one(36, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(result.file.tags[0].bitmap.has_value());
}

TEST(SwfBitmap, LosslessRejectsAStreamThatInflatesToTheWrongSize) {
  // Regression: a stream that does not inflate to the declared geometry must be refused.
  SwfBuilder body;
  body.u16(3);
  body.u8(5);
  body.u16(2);
  body.u16(2);
  body.bytes(zlib_bytes(std::vector<std::uint8_t>(8, 0x7F)));
  const auto result = read_one(20, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(result.file.tags[0].bitmap.has_value());
}

TEST(SwfBitmap, LosslessRejectsGeometryBeyondTheByteCap) {
  // Regression: the row times height product must be checked before it is formed.
  SwfBuilder body;
  body.u16(3);
  body.u8(5);
  body.u16(0xFFFF);
  body.u16(0xFFFF);
  body.bytes(zlib_bytes(std::vector<std::uint8_t>(8, 0x7F)));
  const auto result = read_one(20, body.body);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
}

std::vector<std::uint8_t> inflate_idat(std::span<const std::uint8_t> stream, std::size_t size) {
  std::vector<std::uint8_t> raw(size);
  uLongf length = static_cast<uLongf>(size);
  EXPECT_EQ(uncompress(raw.data(), &length, stream.data(), static_cast<uLong>(stream.size())), Z_OK);
  raw.resize(length);
  return raw;
}

TEST(SwfBitmapPng, Lossless2UnpremultipliesAlphaIntoAnRgbaPng) {
  const std::vector<std::uint8_t> raw = {255, 10, 20, 30, 128, 64, 100, 32,
                                         0,   0,  0,  0,  170, 3,  170, 85};
  const auto result = read_one(36, lossless_raw_payload(5, 2, 2, 0, raw));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto png = fo4::swf::bitmap_to_png(*result.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  const std::vector<std::uint8_t> signature = {137, 80, 78, 71, 13, 10, 26, 10};
  EXPECT_EQ(std::vector<std::uint8_t>(png->begin(), png->begin() + 8), signature);
  const auto chunks = png_chunks(*png);
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(chunks[0].type, "IHDR");
  EXPECT_EQ(chunks[1].type, "IDAT");
  EXPECT_EQ(chunks[2].type, "IEND");
  for (const auto& chunk : chunks) {
    EXPECT_EQ(chunk.stored_crc, chunk.computed_crc) << chunk.type;
  }
  ASSERT_EQ(chunks[0].data.size(), 13u);
  EXPECT_EQ(png_u32(chunks[0].data, 0), 2u);
  EXPECT_EQ(png_u32(chunks[0].data, 4), 2u);
  EXPECT_EQ(chunks[0].data[8], 8u);
  EXPECT_EQ(chunks[0].data[9], 6u);
  EXPECT_EQ(chunks[2].stored_crc, 0xAE426082u);
  const std::vector<std::uint8_t> scanlines = {0, 10, 20, 30, 255, 128, 199, 64, 128,
                                               0, 0,  0,  0,   0,   4,   255, 128, 170};
  EXPECT_EQ(inflate_idat(chunks[1].data, scanlines.size()), scanlines);
}

TEST(SwfBitmapPng, ColourMappedRowsAreStridedAndLookedUpInThePalette) {
  const std::vector<std::uint8_t> raw = {10, 20, 30, 200, 100, 50, 0, 1, 0, 0, 1, 1, 0, 0};
  const auto result = read_one(20, lossless_raw_payload(3, 3, 2, 1, raw));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto png = fo4::swf::bitmap_to_png(*result.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  const auto chunks = png_chunks(*png);
  ASSERT_EQ(chunks.size(), 3u);
  const std::vector<std::uint8_t> scanlines = {0,  10, 20, 30, 255, 200, 100, 50, 255,
                                               10, 20, 30, 255, 0,   200, 100, 50, 255,
                                               200, 100, 50, 255, 10, 20, 30, 255};
  EXPECT_EQ(inflate_idat(chunks[1].data, scanlines.size()), scanlines);
}

TEST(SwfBitmapPng, Lossless2ColourMapUnpremultipliesEachPaletteEntry) {
  const std::vector<std::uint8_t> raw = {64, 100, 32, 128, 3, 170, 85, 170,
                                         0,  1,   0xEE, 0xEE, 1, 0, 0xEE, 0xEE};
  const auto result = read_one(36, lossless_raw_payload(3, 2, 2, 1, raw));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto png = fo4::swf::bitmap_to_png(*result.file.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  const auto chunks = png_chunks(*png);
  ASSERT_EQ(chunks.size(), 3u);
  const std::vector<std::uint8_t> scanlines = {0, 128, 199, 64, 128, 4, 255, 128, 170,
                                               0, 4,   255, 128, 170, 128, 199, 64, 128};
  EXPECT_EQ(inflate_idat(chunks[1].data, scanlines.size()), scanlines);
}

TEST(SwfBitmapPng, RefusesBitmapsThatCannotBeEncodedAsPng) {
  const auto rgb15 = read_one(20, lossless_payload(4, 2, 2, 4, 0, false));
  ASSERT_TRUE(rgb15.ok()) << fo4::swf::status_name(rgb15.status);
  ASSERT_TRUE(rgb15.file.tags[0].bitmap.has_value());
  EXPECT_FALSE(fo4::swf::bitmap_to_png(*rgb15.file.tags[0].bitmap).has_value());
  SwfBuilder jpeg;
  jpeg.u16(4);
  jpeg.bytes(minimal_jpeg(2, 3));
  const auto jpeg_result = read_one(21, jpeg.body);
  ASSERT_TRUE(jpeg_result.ok()) << fo4::swf::status_name(jpeg_result.status);
  EXPECT_FALSE(jpeg_result.file.tags[0].bitmap.has_value());
  EXPECT_TRUE(jpeg_result.file.tags[0].jpeg.has_value());
  const auto argb = read_one(36, lossless_raw_payload(5, 2, 2, 0, std::vector<std::uint8_t>(16, 0x40)));
  ASSERT_TRUE(argb.ok()) << fo4::swf::status_name(argb.status);
  ASSERT_TRUE(argb.file.tags[0].bitmap.has_value());
  auto resized = *argb.file.tags[0].bitmap;
  resized.width = 3;
  EXPECT_FALSE(fo4::swf::bitmap_to_png(resized).has_value());
  const std::vector<std::uint8_t> out_of_range = {10, 20, 30, 5, 0, 0xEE, 0xEE, 0, 0, 0xEE, 0xEE};
  const auto index = read_one(20, lossless_raw_payload(3, 2, 2, 0, out_of_range));
  ASSERT_TRUE(index.ok()) << fo4::swf::status_name(index.status);
  ASSERT_TRUE(index.file.tags[0].bitmap.has_value());
  EXPECT_FALSE(fo4::swf::bitmap_to_png(*index.file.tags[0].bitmap).has_value());
}

TEST(SwfJpeg, DefineBitsJpeg3SplitsTheImageFromTheAlphaPlane) {
  // Regression: DataSize must split the image from the alpha plane.
  const auto image = minimal_jpeg(2, 3);
  const auto result = read_one(35, jpeg3_payload(image, 6));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& jpeg = *result.file.tags[0].jpeg;
  EXPECT_EQ(jpeg.character_id, 4u);
  EXPECT_EQ(jpeg.image_size, image.size());
  EXPECT_TRUE(jpeg.has_alpha);
  EXPECT_EQ(jpeg.alpha_size, zlib_bytes(std::vector<std::uint8_t>(6, 0x5A)).size());
  EXPECT_EQ(jpeg.deblock, 0u);
}

TEST(SwfJpeg, DefineBitsJpeg3RejectsAnAlphaPlaneThatDoesNotMatchTheJpegGeometry) {
  // Regression: the alpha plane must inflate to the JPEG's own width times height.
  const auto image = minimal_jpeg(2, 3);
  const auto result = read_one(35, jpeg3_payload(image, 5));
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
}

TEST(SwfJpeg, DefineBitsJpeg3SeesTheSofBehindFillBytes) {
  auto image = minimal_jpeg(2, 3);
  image.insert(image.begin() + 2, static_cast<std::byte>(0xFF));
  const auto rejected = read_one(35, jpeg3_payload(image, 5));
  EXPECT_EQ(rejected.status, SwfStatus::malformed_tag);
  const auto accepted = read_one(35, jpeg3_payload(image, 6));
  ASSERT_TRUE(accepted.ok()) << fo4::swf::status_name(accepted.status);
  EXPECT_TRUE(accepted.file.tags[0].jpeg.has_value());
  EXPECT_EQ(accepted.file.tags[0].jpeg->image_size, image.size());
}

TEST(SwfJpeg, DefineBitsJpeg4ReadsTheDeblockBeforeTheImage) {
  // Regression: DefineBitsJPEG4 must read its deblocking factor before the image.
  const auto image = minimal_jpeg(2, 3);
  SwfBuilder body;
  body.u16(4);
  body.u32(static_cast<std::uint32_t>(image.size()));
  body.u16(0x0180);
  body.bytes(image);
  std::vector<std::uint8_t> plane(6, 0x5A);
  body.bytes(zlib_bytes(plane));
  const auto result = read_one(90, body.body);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& jpeg = *result.file.tags[0].jpeg;
  EXPECT_EQ(jpeg.deblock, 0x0180u);
  EXPECT_EQ(jpeg.image_size, image.size());
  EXPECT_TRUE(jpeg.has_alpha);
}

TEST(SwfJpeg, DefineBitsAndTablesKeepTheirPayloadSizes) {
  // Regression: DefineBits and JPEGTables must keep their whole payload.
  const auto image = minimal_jpeg(2, 3);
  SwfBuilder bits;
  bits.u16(8);
  bits.bytes(image);
  const auto bits_result = read_one(6, bits.body);
  ASSERT_TRUE(bits_result.ok()) << fo4::swf::status_name(bits_result.status);
  const auto& jpeg = *bits_result.file.tags[0].jpeg;
  EXPECT_EQ(jpeg.character_id, 8u);
  EXPECT_EQ(jpeg.image_size, image.size());
  EXPECT_FALSE(jpeg.has_alpha);

  const auto tables_result = read_one(8, image);
  ASSERT_TRUE(tables_result.ok()) << fo4::swf::status_name(tables_result.status);
  EXPECT_EQ(tables_result.file.tags[0].jpeg_tables->size, image.size());
}
