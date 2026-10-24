#include "swf_art_fixtures.hpp"

#include <gtest/gtest.h>

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

TEST(SwfShape, NewStylesRecordReplacesTheArraysAndTheBitWidths) {
  // Regression: a NewStyles record must replace the style arrays and the fill/line bit widths.
  const auto result = read_one(22, define_shape_2_new_styles_payload());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& shape = *result.file.tags[0].shape;
  ASSERT_EQ(shape.fill_styles.size(), 2u);
  EXPECT_EQ(shape.fill_styles[0].color, 0xFF040506u);
  EXPECT_EQ(shape.fill_styles[1].color, 0xFF070809u);
  EXPECT_TRUE(shape.line_styles.empty());
  EXPECT_EQ(shape.style_change_count, 2u);
  EXPECT_EQ(shape.edge_count, 0u);

  const auto with_lines = read_one(22, define_shape_2_new_styles_lines_payload());
  ASSERT_TRUE(with_lines.ok()) << fo4::swf::status_name(with_lines.status);
  const auto& replaced = *with_lines.file.tags[0].shape;
  ASSERT_EQ(replaced.fill_styles.size(), 2u);
  EXPECT_EQ(replaced.fill_styles[1].color, 0xFF0B0C0Du);
  ASSERT_EQ(replaced.line_styles.size(), 1u);
  EXPECT_EQ(replaced.line_styles[0].width, 7u);
  EXPECT_EQ(replaced.line_styles[0].color, 0xFF0E0F10u);
  EXPECT_EQ(replaced.style_change_count, 2u);
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

TEST(SwfMorph, NewStylesRecordParsesMorphStylePairs) {
  const auto result = read_one(46, morph_new_styles_payload(morph_nested_solid()));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.character_id, 14u);
  EXPECT_EQ(morph.fill_style_count, 2u);
  EXPECT_EQ(morph.line_style_count, 1u);
  EXPECT_EQ(morph.start_edge_count, 0u);
  EXPECT_EQ(morph.end_edge_count, 0u);
}

TEST(SwfMorph, NewStylesRecordParsesMorphGradientAndBitmapFills) {
  // A nested block must walk the gradient and bitmap arms, or the record after it desyncs.
  const auto result = read_one(46, morph_new_styles_payload(morph_nested_fill_kinds()));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& morph = *result.file.tags[0].morph_shape;
  EXPECT_EQ(morph.character_id, 14u);
  EXPECT_EQ(morph.fill_style_count, 2u);
  EXPECT_EQ(morph.line_style_count, 1u);
  EXPECT_EQ(morph.start_edge_count, 0u);
  EXPECT_EQ(morph.end_edge_count, 0u);
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
