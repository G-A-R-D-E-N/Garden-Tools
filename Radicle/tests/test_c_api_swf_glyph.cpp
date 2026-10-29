#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::byte> font_movie() {
  const std::vector<std::byte> payload = bytes_of(
      {0x07, 0x00, 0x02, 0x00, 0x10, 0x14, 0x88, 0x9C, 0xC4, 0x66, 0xA3, 0x2C, 0x40, 0x00});
  swf_test::SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(10, payload, false);
  builder.tag(0);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfGlyph, ReadsGlyphCountShapeAndRecords) {
  const SwfHandle swf = open(font_movie());
  ASSERT_NE(swf, nullptr);

  size_t count = 0;
  EXPECT_EQ(radicle_swf_font_glyph_count(swf.get(), 0, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(count, 1u);

  radicle_swf_glyph glyph{};
  EXPECT_EQ(radicle_swf_font_glyph_at(swf.get(), 0, 0, &glyph), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(glyph.fill_bits, 1);
  EXPECT_EQ(glyph.line_bits, 0);
  EXPECT_EQ(glyph.record_count, 5u);

  radicle_swf_shape_record record{};
  EXPECT_EQ(radicle_swf_font_glyph_record_at(swf.get(), 0, 0, 2, &record), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STRAIGHT_EDGE);
  EXPECT_EQ(record.x, 8);
  EXPECT_EQ(record.y, 0);

  EXPECT_EQ(radicle_swf_font_glyph_record_at(swf.get(), 0, 0, 5, &record),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_glyph_at(swf.get(), 0, 1, &glyph), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_glyph_count(swf.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfGlyph, ReplacesGlyphShapeAndRebuildsTheView) {
  const SwfHandle swf = open(font_movie());
  ASSERT_NE(swf, nullptr);

  radicle_swf_shape_record records[2]{};
  records[0].kind = RADICLE_SWF_RECORD_STYLE_CHANGE;
  records[0].fill1 = 1;
  records[1].kind = RADICLE_SWF_RECORD_STRAIGHT_EDGE;
  records[1].x = 8;

  radicle_swf_plan_result result{};
  EXPECT_EQ(radicle_swf_font_replace_glyph(swf.get(), 0, 0, 1, 0, records, 2, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.changed, 1);

  radicle_swf_glyph glyph{};
  EXPECT_EQ(radicle_swf_font_glyph_at(swf.get(), 0, 0, &glyph), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(glyph.record_count, 2u);
}

TEST(CApiSwfGlyph, RefusesBadRecordKindGlyphIndexAndNullResult) {
  const SwfHandle swf = open(font_movie());
  ASSERT_NE(swf, nullptr);

  radicle_swf_shape_record record{};
  record.kind = static_cast<radicle_swf_record_kind>(-1);
  radicle_swf_plan_result result{};
  EXPECT_EQ(radicle_swf_font_replace_glyph(swf.get(), 0, 0, 1, 0, &record, 1, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);

  record.kind = RADICLE_SWF_RECORD_MOVE;
  EXPECT_EQ(radicle_swf_font_replace_glyph(swf.get(), 0, 1, 1, 0, &record, 1, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_replace_glyph(swf.get(), 0, 0, 1, 0, &record, 1, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}
