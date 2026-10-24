#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using fo4::swf::SwfReadResult;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> movie_with(std::uint16_t code, std::span<const std::byte> payload) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(code, payload, false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

SwfReadResult read_one(std::uint16_t code, std::span<const std::byte> payload) {
  const auto bytes = movie_with(code, payload);
  return fo4::swf::read_swf(bytes);
}

void rgb(SwfBuilder& builder, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
  builder.u8(red);
  builder.u8(green);
  builder.u8(blue);
}

void rgba(SwfBuilder& builder, std::uint32_t red, std::uint32_t green, std::uint32_t blue,
          std::uint32_t alpha) {
  rgb(builder, red, green, blue);
  builder.u8(alpha);
}

void signed_bits(SwfBuilder& builder, std::int32_t value, unsigned count) {
  builder.push_bits(static_cast<std::uint32_t>(value), count);
}

void identity_matrix(SwfBuilder& builder) {
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
}

std::vector<std::byte> zlib_bytes(std::span<const std::uint8_t> raw) {
  uLongf packed = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(packed));
  EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(buffer.data()), &packed,
                      reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  buffer.resize(static_cast<std::size_t>(packed));
  return buffer;
}

void shape_head(SwfBuilder& body, std::uint16_t id) {
  body.u16(id);
  body.rect(0, 2000, 0, 1000, 12);
}

void solid_fill(SwfBuilder& body, bool alpha, std::uint32_t value) {
  body.u8(0x00);
  rgb(body, (value >> 16) & 0xFFu, (value >> 8) & 0xFFu, value & 0xFFu);
  if (alpha) {
    body.u8(0xFF);
  }
}

std::vector<std::byte> define_shape_3_payload() {
  SwfBuilder body;
  shape_head(body, 5);
  body.u8(3);
  solid_fill(body, true, 0x112233);
  body.u8(0x10);
  identity_matrix(body);
  body.u8(0x40 | 0x02);
  body.u8(0);
  rgba(body, 255, 0, 0, 255);
  body.u8(255);
  rgba(body, 0, 0, 255, 128);
  body.u8(0x41);
  body.u16(7);
  identity_matrix(body);
  body.u8(1);
  body.u16(20);
  rgba(body, 0x09, 0x08, 0x07, 0xFF);
  body.u8(0x22);
  body.push_bits(0, 1);
  body.push_bits(0x0F, 5);
  body.push_bits(10, 5);
  signed_bits(body, 100, 10);
  signed_bits(body, -50, 10);
  body.push_bits(1, 2);
  body.push_bits(2, 2);
  body.push_bits(1, 2);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(1, 1);
  signed_bits(body, 10, 6);
  signed_bits(body, -20, 6);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(0, 1);
  body.push_bits(1, 1);
  signed_bits(body, 30, 6);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  signed_bits(body, -15, 6);
  body.push_bits(1, 1);
  body.push_bits(0, 1);
  body.push_bits(4, 4);
  signed_bits(body, 5, 6);
  signed_bits(body, 5, 6);
  signed_bits(body, 10, 6);
  signed_bits(body, 0, 6);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> define_shape_4_payload() {
  SwfBuilder body;
  body.u16(9);
  body.rect(0, 1000, 0, 500, 11);
  body.rect(0, 900, 0, 400, 11);
  body.u8(0x04);
  body.u8(1);
  solid_fill(body, true, 0x445566);
  body.u8(2);
  body.u16(40);
  // Requires spec bit order: StartCapStyle occupies the top two bits of the field's first byte.
  body.u8(0x60);
  body.u8(0x00);
  body.u16(0x0300);
  rgba(body, 0x0A, 0x0B, 0x0C, 0xFF);
  body.u16(5);
  body.u8(0x08);
  body.u8(0x00);
  solid_fill(body, true, 0x778899);
  body.u8(0x12);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, 16, 8);
  signed_bits(body, 8, 8);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> define_shape_2_new_styles_lines_payload() {
  SwfBuilder body;
  shape_head(body, 23);
  body.u8(1);
  solid_fill(body, false, 0x010203);
  body.u8(1);
  body.u16(5);
  rgb(body, 0x14, 0x15, 0x16);
  body.u8(0x11);
  body.push_bits(0, 1);
  body.push_bits(0x10, 5);
  body.align();
  body.u8(2);
  solid_fill(body, false, 0x040506);
  solid_fill(body, false, 0x0B0C0D);
  body.u8(1);
  body.u16(7);
  rgb(body, 0x0E, 0x0F, 0x10);
  body.u8(0x21);
  body.push_bits(0, 1);
  body.push_bits(0x0A, 5);
  body.push_bits(1, 2);
  body.push_bits(1, 1);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> define_shape_2_new_styles_payload() {
  SwfBuilder body;
  shape_head(body, 21);
  body.u8(1);
  solid_fill(body, false, 0x010203);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x10, 5);
  body.align();
  body.u8(2);
  solid_fill(body, false, 0x040506);
  solid_fill(body, false, 0x070809);
  body.u8(0);
  body.u8(0x20);
  body.push_bits(0, 1);
  body.push_bits(0x02, 5);
  body.push_bits(2, 2);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> define_shape_2_extended_counts_payload() {
  SwfBuilder body;
  shape_head(body, 33);
  body.u8(0xFF);
  body.u16(3);
  solid_fill(body, false, 0x101010);
  solid_fill(body, false, 0x202020);
  solid_fill(body, false, 0x303030);
  body.u8(0xFF);
  body.u16(0);
  body.u8(0x30);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> start_edges(const std::vector<std::byte>& styles_and_bits) {
  SwfBuilder body;
  body.bytes(styles_and_bits);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, 1, 8);
  signed_bits(body, 2, 8);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(2, 4);
  body.push_bits(1, 1);
  signed_bits(body, 4, 4);
  signed_bits(body, 6, 4);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> end_edges() {
  SwfBuilder body;
  body.u8(0);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, -1, 8);
  signed_bits(body, -2, 8);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(2, 4);
  body.push_bits(1, 1);
  signed_bits(body, -4, 4);
  signed_bits(body, -6, 4);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> morph_styles(bool version_two) {
  SwfBuilder body;
  body.u8(2);
  body.u8(0x00);
  rgba(body, 1, 2, 3, 4);
  rgba(body, 5, 6, 7, 8);
  body.u8(0x10);
  identity_matrix(body);
  identity_matrix(body);
  body.u8(0x11);
  body.u8(0);
  rgba(body, 9, 10, 11, 12);
  body.u8(200);
  rgba(body, 13, 14, 15, 16);
  body.u8(1);
  body.u16(30);
  body.u16(60);
  if (version_two) {
    body.u8(0x08);
    body.u8(0x00);
    body.u8(0x00);
    rgba(body, 17, 18, 19, 20);
    rgba(body, 21, 22, 23, 24);
  } else {
    rgba(body, 17, 18, 19, 20);
    rgba(body, 21, 22, 23, 24);
  }
  body.u8(0x10);
  return body.body;
}

std::vector<std::byte> morph_payload(bool version_two) {
  SwfBuilder body;
  body.u16(12);
  body.rect(0, 800, 0, 600, 12);
  body.rect(0, 900, 0, 700, 12);
  if (version_two) {
    body.rect(0, 700, 0, 500, 12);
    body.rect(0, 750, 0, 550, 12);
    body.u8(0x02);
  }
  const auto middle = start_edges(morph_styles(version_two));
  body.u32(static_cast<std::uint32_t>(middle.size()));
  body.bytes(middle);
  const auto tail = end_edges();
  body.bytes(tail);
  return body.body;
}

std::vector<std::byte> lossless_payload(std::uint16_t format, std::uint16_t width,
                                        std::uint16_t height, std::size_t row_bytes,
                                        std::uint8_t table_size, bool alpha) {
  SwfBuilder body;
  body.u16(3);
  body.u8(format);
  body.u16(width);
  body.u16(height);
  if (format == 3) {
    body.u8(table_size);
    for (std::uint32_t index = 0; index <= table_size; ++index) {
      if (alpha) {
        rgba(body, index, index, index, 0xFF);
      } else {
        rgb(body, index, index, index);
      }
    }
  }
  std::vector<std::uint8_t> raw(row_bytes * height, 0x7F);
  body.bytes(zlib_bytes(raw));
  return body.body;
}

// A JPEG marker length must be big-endian; every SWF field around it is little-endian.
void be16(SwfBuilder& builder, std::uint32_t value) {
  builder.u8((value >> 8) & 0xFFu);
  builder.u8(value & 0xFFu);
}

std::vector<std::byte> minimal_jpeg(std::uint16_t width, std::uint16_t height) {
  SwfBuilder body;
  body.u8(0xFF);
  body.u8(0xD8);
  body.u8(0xFF);
  body.u8(0xC0);
  be16(body, 11);
  body.u8(8);
  be16(body, height);
  be16(body, width);
  body.u8(1);
  body.u8(1);
  body.u8(0x11);
  body.u8(0);
  body.u8(0xFF);
  body.u8(0xD9);
  return body.body;
}

std::vector<std::byte> jpeg3_payload(std::span<const std::byte> image, std::size_t alpha_pixels) {
  SwfBuilder body;
  body.u16(4);
  body.u32(static_cast<std::uint32_t>(image.size()));
  body.bytes(image);
  std::vector<std::uint8_t> plane(alpha_pixels, 0x5A);
  body.bytes(zlib_bytes(plane));
  return body.body;
}

}  // namespace

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
  // Regression: format 3 must use one byte per pixel after its palette.
  const auto result = read_one(20, lossless_payload(3, 5, 3, 8, 3, false));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& bitmap = *result.file.tags[0].bitmap;
  ASSERT_TRUE(bitmap.color_table_size.has_value());
  EXPECT_EQ(*bitmap.color_table_size, 3u);
  EXPECT_EQ(bitmap.width, 5u);
  EXPECT_GT(bitmap.data_size, 0u);
}

TEST(SwfBitmap, LosslessColourMapSkipsTheRgbaPaletteBeforeTheStream) {
  // Regression: the compressed stream must start after the palette.
  const auto result = read_one(36, lossless_payload(3, 2, 2, 4, 0, true));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& bitmap = *result.file.tags[0].bitmap;
  ASSERT_TRUE(bitmap.color_table_size.has_value());
  EXPECT_EQ(*bitmap.color_table_size, 0u);
  EXPECT_EQ(bitmap.data_size, zlib_bytes(std::vector<std::uint8_t>(8, 0x7F)).size());
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
