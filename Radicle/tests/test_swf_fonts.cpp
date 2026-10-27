#include <fo4/swf/swf.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<std::byte> font_payload(std::uint16_t font_id, std::uint8_t flags,
                                    std::uint16_t glyph_count,
                                    const std::vector<std::uint32_t>& offsets,
                                    const std::vector<std::uint8_t>& codes,
                                    const std::vector<std::int16_t>& advances, bool wide_codes) {
  std::vector<std::byte> payload;
  const auto push_u8 = [&payload](std::uint8_t value) {
    payload.push_back(static_cast<std::byte>(value));
  };
  const auto push_u16 = [&payload](std::uint16_t value) {
    payload.push_back(static_cast<std::byte>(value & 0xFFu));
    payload.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
  };
  const auto push_u32 = [&payload](std::uint32_t value) {
    payload.push_back(static_cast<std::byte>(value & 0xFFu));
    payload.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    payload.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    payload.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  const auto push_bits_rect = [&payload]() {
    payload.push_back(std::byte{1});
    payload.push_back(std::byte{0});
  };

  push_u16(font_id);
  push_u8(flags);
  push_u8(0);
  push_u8(1);
  payload.push_back(std::byte{'F'});
  push_u16(glyph_count);
  const std::size_t offset_table_start = payload.size();
  for (const std::uint32_t offset : offsets) {
    push_u32(offset);
  }
  const std::size_t code_table_relative = 40;
  push_u32(static_cast<std::uint32_t>(code_table_relative));
  while (payload.size() < offset_table_start + code_table_relative) {
    payload.push_back(std::byte{0});
  }
  for (const std::uint8_t code : codes) {
    if (wide_codes) {
      push_u16(code);
    } else {
      push_u8(code);
    }
  }
  push_u16(800);
  push_u16(200);
  push_u16(static_cast<std::uint16_t>(-50));
  for (const std::int16_t advance : advances) {
    push_u16(static_cast<std::uint16_t>(advance));
  }
  push_bits_rect();
  push_bits_rect();
  push_u16(0);
  return payload;
}

void append_tag(std::vector<std::byte>& bytes, std::uint16_t code,
                const std::vector<std::byte>& payload) {
  const std::uint16_t header = static_cast<std::uint16_t>((code << 6u) | 0x3Fu);
  bytes.push_back(static_cast<std::byte>(header & 0xFFu));
  bytes.push_back(static_cast<std::byte>((header >> 8u) & 0xFFu));
  const auto length = static_cast<std::uint32_t>(payload.size());
  bytes.push_back(static_cast<std::byte>(length & 0xFFu));
  bytes.push_back(static_cast<std::byte>((length >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::byte>((length >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::byte>((length >> 24u) & 0xFFu));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<std::byte> build_swf(std::uint16_t tag_code,
                                 const std::vector<std::byte>& tag_payload) {
  std::vector<std::byte> body;
  append_tag(body, tag_code, tag_payload);
  body.push_back(std::byte{0});
  body.push_back(std::byte{0});

  std::vector<std::byte> bytes{'F', 'W', 'S', std::byte{10}};
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{1});
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{24});
  bytes.push_back(std::byte{0});
  bytes.push_back(std::byte{1});
  bytes.push_back(std::byte{0});
  bytes.insert(bytes.end(), body.begin(), body.end());
  const auto file_length = static_cast<std::uint32_t>(bytes.size());
  bytes[4] = static_cast<std::byte>(file_length & 0xFFu);
  bytes[5] = static_cast<std::byte>((file_length >> 8u) & 0xFFu);
  bytes[6] = static_cast<std::byte>((file_length >> 16u) & 0xFFu);
  bytes[7] = static_cast<std::byte>((file_length >> 24u) & 0xFFu);
  return bytes;
}

}  // namespace

TEST(SwfFonts, DecodesDefineFont3LayoutAndCodes) {
  const std::vector<std::byte> payload =
      font_payload(7, 0x88u, 2, {0, 30}, {65, 66}, {10240, 5120}, false);
  const std::vector<std::byte> bytes = build_swf(fo4::swf::tag::kDefineFont3, payload);
  const fo4::swf::SwfReadResult result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_FALSE(result.file.tags.empty());
  const fo4::swf::SwfFont* font = nullptr;
  for (const fo4::swf::SwfTag& tag : result.file.tags) {
    if (tag.font.has_value()) {
      font = &*tag.font;
    }
  }
  ASSERT_NE(font, nullptr);
  EXPECT_EQ(font->font_id, 7u);
  EXPECT_EQ(font->glyph_count, 2u);
  EXPECT_EQ(font->name, "F");
  EXPECT_TRUE(font->define_font3);
  EXPECT_TRUE(font->has_layout);
  ASSERT_EQ(font->glyph_codes.size(), 2u);
  EXPECT_EQ(font->glyph_codes[0], 65u);
  EXPECT_EQ(font->glyph_codes[1], 66u);
  ASSERT_EQ(font->advances.size(), 2u);
  EXPECT_DOUBLE_EQ(font->advances[0], 0.5);
  EXPECT_DOUBLE_EQ(font->advances[1], 0.25);
  EXPECT_EQ(font->units_per_em, 20480u);
  EXPECT_EQ(font->ascent, 800u);
  EXPECT_EQ(font->leading, -50);
  ASSERT_EQ(font->glyph_offsets.size(), 2u);
  EXPECT_EQ(font->glyph_offsets[1], 30u);
}

TEST(SwfFonts, DecodesDefineFont2AdvanceUnits) {
  const std::vector<std::byte> payload =
      font_payload(9, 0x88u, 2, {0, 20}, {65, 66}, {512, 256}, false);
  const std::vector<std::byte> bytes = build_swf(fo4::swf::tag::kDefineFont2, payload);
  const fo4::swf::SwfReadResult result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const fo4::swf::SwfFont* font = nullptr;
  for (const fo4::swf::SwfTag& tag : result.file.tags) {
    if (tag.font.has_value()) {
      font = &*tag.font;
    }
  }
  ASSERT_NE(font, nullptr);
  EXPECT_FALSE(font->define_font3);
  EXPECT_TRUE(font->has_layout);
  ASSERT_EQ(font->advances.size(), 2u);
  EXPECT_DOUBLE_EQ(font->advances[0], 0.5);
  EXPECT_DOUBLE_EQ(font->advances[1], 0.25);
  EXPECT_EQ(font->units_per_em, 1024u);
}
