#include <fo4/swf/swf.hpp>

#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

using fo4::swf::SwfStatus;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

const std::vector<std::byte> kDefineTextPayload = bytes_of({
    0x07, 0x00, 0x00, 0x00, 0x04, 0x08,
    0x8D, 0x03, 0x00, 0x11, 0x22, 0x33, 0xF6, 0xFF, 0xF0, 0x00, 0x02, 0x10, 0xA2, 0xFD,
    0x82, 0xFC, 0xFF, 0x00,
    0x00,
});

const std::vector<std::byte> kDefineText2Payload = bytes_of({
    0x08, 0x00, 0x00, 0x00, 0x06, 0x07,
    0x8C, 0x09, 0x00, 0x44, 0x55, 0x66, 0x80, 0xC0, 0x00, 0x01, 0x17, 0x60,
    0x00,
});

std::vector<std::byte> text_movie(const std::vector<std::byte>& payload, std::uint16_t code) {
  swf_test::SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(code, payload, false);
  builder.tag(1);
  builder.tag(0);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

}  // namespace

TEST(SwfTextGlyphTest, DecodesDefineTextRecordsWithOpaqueRgbColour) {
  const auto result =
      fo4::swf::read_swf(text_movie(kDefineTextPayload, fo4::swf::tag::kDefineText));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].text.has_value());
  const auto& text = *result.file.tags[0].text;
  EXPECT_EQ(text.character_id, 7);
  EXPECT_EQ(text.record_count, 2u);
  EXPECT_EQ(text.glyph_count, 2u);
  ASSERT_EQ(text.records.size(), 2u);

  const auto& first = text.records[0];
  ASSERT_TRUE(first.font_id.has_value());
  EXPECT_EQ(*first.font_id, 3);
  ASSERT_TRUE(first.color.has_value());
  EXPECT_EQ(*first.color, 0xFF112233u);
  ASSERT_TRUE(first.x_offset.has_value());
  EXPECT_EQ(*first.x_offset, -10);
  EXPECT_FALSE(first.y_offset.has_value());
  ASSERT_TRUE(first.height.has_value());
  EXPECT_EQ(*first.height, 240);
  ASSERT_EQ(first.glyphs.size(), 2u);
  EXPECT_EQ(first.glyphs[0].glyph_index, 1u);
  EXPECT_EQ(first.glyphs[0].advance, 10);
  EXPECT_EQ(first.glyphs[1].glyph_index, 2u);
  EXPECT_EQ(first.glyphs[1].advance, -3);

  const auto& second = text.records[1];
  EXPECT_FALSE(second.font_id.has_value());
  EXPECT_FALSE(second.color.has_value());
  EXPECT_FALSE(second.x_offset.has_value());
  ASSERT_TRUE(second.y_offset.has_value());
  EXPECT_EQ(*second.y_offset, -4);
  EXPECT_FALSE(second.height.has_value());
  EXPECT_TRUE(second.glyphs.empty());
}

TEST(SwfTextGlyphTest, DecodesDefineText2RgbaColourAndNegativeAdvance) {
  const auto result =
      fo4::swf::read_swf(text_movie(kDefineText2Payload, fo4::swf::tag::kDefineText2));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].text.has_value());
  const auto& text = *result.file.tags[0].text;
  EXPECT_EQ(text.character_id, 8);
  EXPECT_EQ(text.record_count, 1u);
  EXPECT_EQ(text.glyph_count, 1u);
  ASSERT_EQ(text.records.size(), 1u);

  const auto& record = text.records[0];
  ASSERT_TRUE(record.font_id.has_value());
  EXPECT_EQ(*record.font_id, 9);
  ASSERT_TRUE(record.color.has_value());
  EXPECT_EQ(*record.color, 0x80445566u);
  EXPECT_FALSE(record.x_offset.has_value());
  EXPECT_FALSE(record.y_offset.has_value());
  ASSERT_TRUE(record.height.has_value());
  EXPECT_EQ(*record.height, 192);
  ASSERT_EQ(record.glyphs.size(), 1u);
  EXPECT_EQ(record.glyphs[0].glyph_index, 5u);
  EXPECT_EQ(record.glyphs[0].advance, -20);
}

TEST(SwfTextGlyphTest, RejectsMalformedRecordsAndKeepsThePayload) {
  auto missing_terminator = kDefineTextPayload;
  missing_terminator.pop_back();
  auto type_bit_clear = kDefineTextPayload;
  type_bit_clear[6] = std::byte{0x0D};
  auto text2_missing_terminator = kDefineText2Payload;
  text2_missing_terminator.pop_back();
  const auto cut_glyphs =
      bytes_of({0x07, 0x00, 0x00, 0x00, 0x04, 0x08, 0x80, 0x02, 0x10, 0xA2});

  struct Broken {
    const char* name;
    std::uint16_t code;
    std::vector<std::byte> payload;
  };
  const std::vector<Broken> broken = {
      {"DefineText missing terminator", fo4::swf::tag::kDefineText, missing_terminator},
      {"DefineText record type bit clear", fo4::swf::tag::kDefineText, type_bit_clear},
      {"DefineText glyph entries cut", fo4::swf::tag::kDefineText, cut_glyphs},
      {"DefineText2 missing terminator", fo4::swf::tag::kDefineText2, text2_missing_terminator},
  };

  for (const auto& entry : broken) {
    SCOPED_TRACE(entry.name);
    const auto bytes = text_movie(entry.payload, entry.code);

    const auto result = fo4::swf::read_swf(bytes);
    EXPECT_EQ(result.status, SwfStatus::malformed_tag);
    ASSERT_EQ(result.file.tags.size(), 3u);
    EXPECT_EQ(result.file.tags[0].code, entry.code);
    EXPECT_EQ(result.file.tags[0].payload, entry.payload);
    EXPECT_FALSE(result.file.tags[0].text.has_value());
    EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
  }
}
