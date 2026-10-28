#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"

#include <gtest/gtest.h>

#include <cstdint>
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

fo4::swf::SwfTag square_glyph_font_tag(std::uint16_t font_id, std::int32_t side) {
  fo4::swf::SwfFont font;
  font.font_id = font_id;
  fo4::swf::SwfGlyph glyph;
  glyph.records = {move_to(0, 0),    style_change(1, 0, 0), line_by(side, 0),
                   line_by(0, side), line_by(-side, 0),     line_by(0, -side)};
  font.glyphs.push_back(glyph);
  fo4::swf::SwfTag tag;
  tag.font = font;
  return tag;
}

std::string tags_frame_svg(std::vector<fo4::swf::SwfTag> tags,
                           std::vector<fo4::swf::SwfFrameInstance> rows) {
  fo4::swf::SwfFile file;
  file.header.frame_size.x_max = 11000;
  file.header.frame_size.y_max = 8000;
  file.tags = std::move(tags);
  fo4::swf::SwfDisplayList display;
  display.timelines.emplace_back();
  display.timelines.front().frames.push_back(fo4::swf::SwfDisplayFrame{0, std::move(rows)});
  return fo4::swf::frame_to_svg(file, display, 0);
}

const std::string kStage =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n";

}  // namespace

// Invariant: a TEXTRECORD x_offset is in twips and is composed under the DefineText MATRIX, so 200 twips is 10 px and a scale of 2 places it at 20 px.
TEST(SwfFrameSvg, TextRecordOffsetIsScaledByTheTextMatrix) {
  fo4::swf::SwfTextRecord record;
  record.font_id = 7;
  record.color = 0xFFFF0000u;
  record.x_offset = 200;
  record.height = 1024;
  record.glyphs = {{0, 0}};
  fo4::swf::SwfTag text_tag;
  text_tag.text = fo4::swf::SwfText{};
  text_tag.text->character_id = 11;
  text_tag.text->matrix.scale_x = 131072;
  text_tag.text->matrix.scale_y = 131072;
  text_tag.text->records = {record};
  fo4::swf::SwfFrameInstance row;
  row.depth = 1;
  row.character_id = 11;
  row.world_visible = true;
  const std::string expected =
      kStage +
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L51.2 0L51.2 51.2L0 51.2L0 0Z\" transform=\"matrix(2 0 0 2 20 0)\" "
      "fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(tags_frame_svg({square_glyph_font_tag(7, 1024), text_tag}, {row}), expected);
}
