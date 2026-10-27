#include <fo4/swf/swf.hpp>
#include <fo4/swf/swf_glyph_edit.hpp>

#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using fo4::swf::SwfFont;
using fo4::swf::SwfGlyph;
using fo4::swf::SwfKerning;
using fo4::swf::SwfRect;
using fo4::swf::SwfShapeRecord;
using fo4::swf::SwfShapeRecordKind;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

const std::vector<std::byte> kShapeBytes =
    bytes_of({0x10, 0x14, 0x88, 0x9C, 0xC4, 0x66, 0xA3, 0x2C, 0x40, 0x00});

const std::vector<std::byte> kFontPayload =
    bytes_of({0x07, 0x00, 0x02, 0x00, 0x10, 0x14, 0x88, 0x9C, 0xC4, 0x66, 0xA3, 0x2C, 0x40, 0x00});

const std::vector<std::byte> kFont2Payload = bytes_of({
    0x05, 0x00, 0x84, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x08, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x41, 0x00, 0x42, 0x00, 0x20, 0x03, 0xC8, 0x00, 0xCE, 0xFF, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00,
});

const std::vector<std::byte> kFont2ReplacedPayload = bytes_of({
    0x05, 0x00, 0x84, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x0B, 0x00, 0x0D, 0x00, 0x10, 0x13,
    0x98, 0x80, 0x00, 0x00, 0x00, 0x41, 0x00, 0x42, 0x00, 0x20, 0x03, 0xC8, 0x00, 0xCE, 0xFF,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
});

const std::vector<std::byte> kFont3Payload = bytes_of({
    0x09, 0x00, 0x80, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x41, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x01, 0x00, 0x41, 0x00, 0x42, 0x00,
    0xFE, 0xFF,
});

std::vector<std::byte> define_font_movie(const std::vector<std::byte>& payload, std::uint16_t code) {
  swf_test::SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(code, payload, false);
  builder.tag(0);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

}  // namespace

TEST(SwfGlyphTest, DecodesGlyphShapeAndRebuildsDefineFontByteExactly) {
  const auto result = fo4::swf::read_swf(
      define_font_movie(kFontPayload, fo4::swf::tag::kDefineFont));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].font.has_value());
  const auto& font = *result.file.tags[0].font;
  ASSERT_EQ(font.glyphs.size(), 1u);
  const SwfGlyph& glyph = font.glyphs[0];
  EXPECT_EQ(glyph.fill_bits, 1u);
  EXPECT_EQ(glyph.line_bits, 0u);
  ASSERT_EQ(glyph.records.size(), 5u);
  EXPECT_EQ(glyph.records[0].kind, SwfShapeRecordKind::move);
  EXPECT_EQ(glyph.records[0].x, 4);
  EXPECT_EQ(glyph.records[0].y, 4);
  EXPECT_EQ(glyph.records[1].kind, SwfShapeRecordKind::style_change);
  EXPECT_EQ(glyph.records[1].fill1, 1u);
  EXPECT_EQ(glyph.records[2].kind, SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(glyph.records[2].x, 8);
  EXPECT_EQ(glyph.records[2].y, 0);
  EXPECT_EQ(glyph.records[3].kind, SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(glyph.records[3].x, 0);
  EXPECT_EQ(glyph.records[3].y, 8);
  EXPECT_EQ(glyph.records[4].kind, SwfShapeRecordKind::straight_edge);
  EXPECT_EQ(glyph.records[4].x, -8);
  EXPECT_EQ(glyph.records[4].y, -8);

  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::encode_define_font(font, fo4::swf::tag::kDefineFont, out, message))
      << message;
  EXPECT_EQ(out, kFontPayload);
}

TEST(SwfGlyphTest, EncodesGlyphShapeWithMinimalFieldWidths) {
  const auto result = fo4::swf::read_swf(
      define_font_movie(kFontPayload, fo4::swf::tag::kDefineFont));
  ASSERT_TRUE(result.ok());
  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::encode_glyph_shape(result.file.tags[0].font->glyphs[0], out, message))
      << message;
  EXPECT_EQ(out, kShapeBytes);
}

TEST(SwfGlyphTest, ReplacesOneGlyphShapeAndRefusesAnUnknownGlyph) {
  const auto result = fo4::swf::read_swf(
      define_font_movie(kFontPayload, fo4::swf::tag::kDefineFont));
  ASSERT_TRUE(result.ok());

  SwfShapeRecord style{SwfShapeRecordKind::style_change};
  style.fill1 = 1;
  SwfShapeRecord edge{SwfShapeRecordKind::straight_edge};
  edge.x = 8;
  SwfGlyph shape;
  shape.fill_bits = 1;
  shape.line_bits = 0;
  shape.records = {style, edge};

  std::vector<std::byte> payload;
  std::string message;
  ASSERT_TRUE(fo4::swf::replace_glyph_payload(result.file.tags[0], 0, shape, payload, message))
      << message;
  EXPECT_EQ(payload, bytes_of({0x07, 0x00, 0x02, 0x00, 0x10, 0x13, 0x98, 0x80, 0x00}));

  EXPECT_FALSE(fo4::swf::replace_glyph_payload(result.file.tags[0], 1, shape, payload, message));
  EXPECT_FALSE(message.empty());
}

TEST(SwfGlyphTest, EncodesDefineFont2WithLayoutAndWideCodes) {
  SwfFont font;
  font.font_id = 5;
  font.flags = 0x84;
  font.glyph_count = 2;
  font.glyphs = {SwfGlyph{}, SwfGlyph{}};
  font.wide_codes = true;
  font.has_layout = true;
  font.glyph_codes = {65, 66};
  font.ascent = 800;
  font.descent = 200;
  font.leading = -50;
  font.units_per_em = 1024;
  font.advances = {0.5, 0.25};
  font.glyph_bounds = {SwfRect{}, SwfRect{}};

  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::encode_define_font(font, fo4::swf::tag::kDefineFont2, out, message))
      << message;
  EXPECT_EQ(out, kFont2Payload);

  const auto result =
      fo4::swf::read_swf(define_font_movie(kFont2Payload, fo4::swf::tag::kDefineFont2));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].font.has_value());
  EXPECT_EQ(result.file.tags[0].font->glyph_codes, (std::vector<std::uint16_t>{65, 66}));
  EXPECT_EQ(result.file.tags[0].font->glyphs.size(), 2u);
}

TEST(SwfGlyphTest, EncodesDefineFont2WithoutLayoutAndZeroGlyphs) {
  SwfFont font;
  font.font_id = 3;
  font.flags = 0x08;
  font.wide_offsets = true;

  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::encode_define_font(font, fo4::swf::tag::kDefineFont2, out, message))
      << message;
  const auto expected = bytes_of({0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00});
  EXPECT_EQ(out, expected);

  const auto result = fo4::swf::read_swf(define_font_movie(out, fo4::swf::tag::kDefineFont2));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].font.has_value());
  EXPECT_EQ(result.file.tags[0].font->glyph_count, 0u);
  EXPECT_TRUE(result.file.tags[0].font->glyphs.empty());
}

TEST(SwfGlyphTest, EncodesDefineFont3WithKerningAndLayout) {
  SwfFont font;
  font.font_id = 9;
  font.flags = 0x80;
  font.define_font3 = true;
  font.glyph_count = 1;
  font.glyphs = {SwfGlyph{}};
  font.wide_codes = true;
  font.has_layout = true;
  font.glyph_codes = {65};
  font.units_per_em = 20480;
  font.advances = {0.5};
  font.glyph_bounds = {SwfRect{}};
  font.kerning = {SwfKerning{65, 66, -2}};

  std::vector<std::byte> out;
  std::string message;
  ASSERT_TRUE(fo4::swf::encode_define_font(font, fo4::swf::tag::kDefineFont3, out, message))
      << message;
  EXPECT_EQ(out, kFont3Payload);

  const auto result =
      fo4::swf::read_swf(define_font_movie(kFont3Payload, fo4::swf::tag::kDefineFont3));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].font.has_value());
  const auto& decoded = *result.file.tags[0].font;
  EXPECT_EQ(decoded.glyph_codes, (std::vector<std::uint16_t>{65}));
  ASSERT_EQ(decoded.kerning.size(), 1u);
  EXPECT_EQ(decoded.kerning[0].code1, 65u);
  EXPECT_EQ(decoded.kerning[0].code2, 66u);
  EXPECT_EQ(decoded.kerning[0].adjustment, -2);
}

TEST(SwfGlyphTest, UnreadableGlyphKeepsTheFontDecodedWithoutGlyphs) {
  std::vector<std::byte> payload = kFont3Payload;
  payload[7] = std::byte{0x20};
  const auto result = fo4::swf::read_swf(define_font_movie(payload, fo4::swf::tag::kDefineFont3));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].font.has_value());
  const auto& font = *result.file.tags[0].font;
  EXPECT_EQ(font.glyph_count, 1u);
  EXPECT_TRUE(font.glyphs.empty());
  EXPECT_EQ(font.glyph_codes, (std::vector<std::uint16_t>{65}));

  std::vector<std::byte> out;
  std::string message;
  EXPECT_FALSE(fo4::swf::replace_glyph_payload(result.file.tags[0], 0, SwfGlyph{}, out, message));
  EXPECT_FALSE(message.empty());
}

TEST(SwfGlyphTest, ReplacesOneGlyphInsideDefineFont2WithLayout) {
  const auto result =
      fo4::swf::read_swf(define_font_movie(kFont2Payload, fo4::swf::tag::kDefineFont2));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  SwfShapeRecord style{SwfShapeRecordKind::style_change};
  style.fill1 = 1;
  SwfShapeRecord edge{SwfShapeRecordKind::straight_edge};
  edge.x = 8;
  SwfGlyph shape;
  shape.fill_bits = 1;
  shape.line_bits = 0;
  shape.records = {style, edge};

  std::vector<std::byte> payload;
  std::string message;
  ASSERT_TRUE(fo4::swf::replace_glyph_payload(result.file.tags[0], 0, shape, payload, message))
      << message;
  EXPECT_EQ(payload, kFont2ReplacedPayload);
}
