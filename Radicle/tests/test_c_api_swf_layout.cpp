#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

struct LayoutDeleter {
  void operator()(radicle_swf_layout* layout) const { radicle_swf_layout_destroy(layout); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;
using LayoutHandle = std::unique_ptr<radicle_swf_layout, LayoutDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

LayoutHandle layout_of(const SwfHandle& swf) {
  radicle_swf_layout* raw = nullptr;
  EXPECT_EQ(radicle_swf_layout_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return LayoutHandle(raw);
}

std::vector<std::byte> identity() { return swf_test::matrix_full(0x10000, 0x10000, 0, 0, 0, 0); }

std::vector<std::byte> place(std::uint16_t depth, std::uint16_t character, const std::string& name) {
  swf_test::SwfBuilder body;
  body.u8(0x26);
  body.u16(depth);
  body.u16(character);
  body.bytes(identity());
  body.text(name);
  return body.body;
}

std::vector<std::byte> class_edit_text() {
  swf_test::SwfBuilder body;
  body.u16(12);
  body.rect(0, 400, 0, 200, 12);
  body.u8(0x80);
  body.u8(0x80);
  body.text("TextFont");
  body.u16(240);
  body.text("v");
  body.text("Hi");
  return body.body;
}

std::vector<std::byte> font_movie() {
  const auto sprite = swf_test::sprite_tag(14, 3, {{1, {}}, {1, {}}, {1, {}}});
  return swf_test::movie_of(
      1, {{91, swf_test::font4_payload(9, 0x05, "Cff")},
          {10, swf_test::one_glyph_font()},
          {48, swf_test::font2_payload(4, "Plain")},
          {75, swf_test::font3_payload(3, "Face")},
          {13, swf_test::font_info_payload(7, "Old", {0x61}, false, 0x04)},
          {88, swf_test::font_name_payload(3, "Display", "(c) Maker")},
          {76, swf_test::symbol_class_payload({{3, "FaceClass"}})},
          {37, class_edit_text()},
          {39, sprite},
          {26, place(2, 14, "Art")},
          {26, place(3, 12, "Field")},
          {1, {}}});
}

}  // namespace

// Constraint: the font table must reach the ABI in id order with each has_ flag matching its tag.
TEST(CApiSwfLayoutFonts, ExposesTheFontTableByIndexInIdOrder) {
  const auto swf = open(font_movie());
  ASSERT_NE(swf, nullptr);
  const auto layout = layout_of(swf);
  ASSERT_NE(layout, nullptr);
  radicle_swf_layout_info info{};
  ASSERT_EQ(radicle_swf_layout_get_info(layout.get(), &info), RADICLE_STATUS_OK);
  ASSERT_EQ(info.font_count, 4u);

  radicle_swf_layout_font face{};
  ASSERT_EQ(radicle_swf_layout_font_at(layout.get(), 0, &face), RADICLE_STATUS_OK);
  EXPECT_EQ(face.font_id, 3);
  EXPECT_EQ(face.version, 3);
  EXPECT_EQ(std::string(face.name), "Face");
  EXPECT_EQ(face.has_style, 1);
  EXPECT_EQ(face.bold, 1);
  EXPECT_EQ(face.italic, 0);
  EXPECT_EQ(face.has_language, 1);
  EXPECT_EQ(face.language, 1);
  EXPECT_EQ(face.has_glyph_count, 1);
  EXPECT_EQ(face.glyph_count, 1u);
  EXPECT_EQ(face.has_outlines, 1);
  EXPECT_EQ(face.has_metrics, 1);
  EXPECT_EQ(face.ascent, 800);
  EXPECT_EQ(face.descent, 200);
  EXPECT_EQ(face.leading, 40);
  EXPECT_EQ(face.units_per_em, 20480u);
  EXPECT_EQ(face.has_codes, 1);
  EXPECT_EQ(face.code_count, 1u);
  EXPECT_EQ(face.has_font_name, 1);
  EXPECT_EQ(std::string(face.font_name), "Display");
  EXPECT_EQ(std::string(face.font_copyright), "(c) Maker");
  EXPECT_EQ(face.has_font_class, 1);
  EXPECT_EQ(std::string(face.font_class), "FaceClass");

  radicle_swf_layout_font plain{};
  ASSERT_EQ(radicle_swf_layout_font_at(layout.get(), 1, &plain), RADICLE_STATUS_OK);
  EXPECT_EQ(plain.font_id, 4);
  EXPECT_EQ(plain.italic, 1);
  EXPECT_EQ(plain.has_outlines, 0);
  EXPECT_EQ(plain.has_metrics, 0);
  EXPECT_EQ(plain.has_codes, 1);
  EXPECT_EQ(plain.code_count, 0u);
  EXPECT_EQ(plain.has_font_name, 0);
  EXPECT_EQ(std::string(plain.font_name), "");
  EXPECT_EQ(std::string(plain.font_copyright), "");
  EXPECT_EQ(plain.has_font_class, 0);
  EXPECT_EQ(std::string(plain.font_class), "");

  radicle_swf_layout_font legacy{};
  ASSERT_EQ(radicle_swf_layout_font_at(layout.get(), 2, &legacy), RADICLE_STATUS_OK);
  EXPECT_EQ(legacy.font_id, 7);
  EXPECT_EQ(legacy.version, 1);
  EXPECT_EQ(legacy.has_style, 1);
  EXPECT_EQ(legacy.bold, 0);
  EXPECT_EQ(legacy.italic, 1);
  EXPECT_EQ(legacy.has_language, 0);
  EXPECT_EQ(std::string(legacy.name), "Old");
  EXPECT_EQ(legacy.has_codes, 1);
  EXPECT_EQ(legacy.code_count, 1u);

  radicle_swf_layout_font cff{};
  ASSERT_EQ(radicle_swf_layout_font_at(layout.get(), 3, &cff), RADICLE_STATUS_OK);
  EXPECT_EQ(cff.font_id, 9);
  EXPECT_EQ(cff.version, 4);
  EXPECT_EQ(std::string(cff.name), "Cff");
  EXPECT_EQ(cff.has_glyph_count, 0);
  EXPECT_EQ(cff.has_outlines, 1);
  EXPECT_EQ(cff.has_codes, 0);
  EXPECT_EQ(cff.code_count, 0u);
}

// Constraint: each code must be readable by font and code index, in glyph order, and every bad index or null argument must refuse with the output cleared.
TEST(CApiSwfLayoutFonts, ReadsTheGlyphCodeTableOneCodeAtATime) {
  const auto swf = open(font_movie());
  const auto layout = layout_of(swf);
  ASSERT_NE(layout, nullptr);
  std::uint16_t code = 0xFFFF;
  ASSERT_EQ(radicle_swf_layout_font_code_at(layout.get(), 0, 0, &code), RADICLE_STATUS_OK);
  EXPECT_EQ(code, 0x41);
  ASSERT_EQ(radicle_swf_layout_font_code_at(layout.get(), 2, 0, &code), RADICLE_STATUS_OK);
  EXPECT_EQ(code, 0x61);
  code = 0xFFFF;
  EXPECT_EQ(radicle_swf_layout_font_code_at(layout.get(), 0, 1, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(code, 0);
  EXPECT_EQ(radicle_swf_layout_font_code_at(layout.get(), 1, 0, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_font_code_at(layout.get(), 3, 0, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_font_code_at(layout.get(), 4, 0, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_font_code_at(nullptr, 0, 0, &code), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_font_code_at(layout.get(), 0, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: an out-of-range or null font argument must refuse and leave the output cleared.
TEST(CApiSwfLayoutFonts, RefusesBadFontArgumentsWithTheOutputCleared) {
  const auto swf = open(font_movie());
  const auto layout = layout_of(swf);
  ASSERT_NE(layout, nullptr);
  radicle_swf_layout_font font{};
  font.font_id = 77;
  EXPECT_EQ(radicle_swf_layout_font_at(layout.get(), 4, &font), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(font.font_id, 0);
  EXPECT_EQ(radicle_swf_layout_font_at(nullptr, 0, &font), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_font_at(layout.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: a sprite row must carry its frame span through the ABI.
TEST(CApiSwfLayoutFonts, ASpriteRowCarriesItsFrameSpan) {
  const auto swf = open(font_movie());
  const auto layout = layout_of(swf);
  ASSERT_NE(layout, nullptr);
  radicle_swf_layout_row art{};
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 0, &art), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(art.path), "Art");
  EXPECT_EQ(art.clip_frame_count, 3u);
  EXPECT_EQ(art.clip_first_frame, 0u);
  EXPECT_EQ(art.clip_last_frame, 2u);
}

// Constraint: a text row that names its font by class must carry the class and no font id, and a row with no text must carry none.
TEST(CApiSwfLayoutFonts, ATextRowThatNamesItsFontByClassCarriesTheClassThroughTheAbi) {
  const auto swf = open(font_movie());
  const auto layout = layout_of(swf);
  ASSERT_NE(layout, nullptr);
  radicle_swf_layout_row art{};
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 0, &art), RADICLE_STATUS_OK);
  EXPECT_EQ(art.has_text, 0);
  radicle_swf_layout_row field{};
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 1, &field), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(field.path), "Field");
  ASSERT_EQ(field.has_text, 1);
  EXPECT_EQ(field.text.has_font_id, 0);
  EXPECT_EQ(field.text.has_font_class, 1);
  EXPECT_EQ(std::string(field.text.font_class), "TextFont");
  EXPECT_EQ(field.text.has_font_height, 1);
  EXPECT_DOUBLE_EQ(field.text.font_height, 12.0);
  EXPECT_EQ(std::string(field.text.text), "Hi");
}

// Constraint: a CWS movie must open a layout of the same size as its FWS twin.
TEST(CApiSwfLayoutFonts, OpensALayoutFromACwsMovie) {
  const auto fws = font_movie();
  const auto parsed = fo4::swf::read_swf(fws);
  ASSERT_TRUE(parsed.ok()) << parsed.message;
  const auto cws = fo4::swf::write_swf(parsed.file, fo4::swf::SwfCompression::zlib);
  ASSERT_GE(cws.size(), 3u);
  ASSERT_EQ(static_cast<char>(cws[0]), 'C');

  const auto plain_swf = open(fws);
  const auto packed_swf = open(cws);
  ASSERT_NE(packed_swf, nullptr);
  const auto plain = layout_of(plain_swf);
  const auto packed = layout_of(packed_swf);
  ASSERT_NE(packed, nullptr);
  radicle_swf_layout_info a{};
  radicle_swf_layout_info b{};
  ASSERT_EQ(radicle_swf_layout_get_info(plain.get(), &a), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_swf_layout_get_info(packed.get(), &b), RADICLE_STATUS_OK);
  EXPECT_EQ(b.font_count, a.font_count);
  EXPECT_EQ(b.row_count, a.row_count);
  EXPECT_EQ(b.state_count, a.state_count);
  EXPECT_EQ(b.font_count, 4u);
}
