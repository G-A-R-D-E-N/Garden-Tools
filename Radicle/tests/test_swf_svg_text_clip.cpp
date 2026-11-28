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

const std::string kStage =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n";

const std::string kBlueSquare =
    "<path d=\"M0 0L30 0L30 30L0 30L0 0Z\" fill=\"#0000ff\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

fo4::swf::SwfTag shape_tag(std::uint16_t id, std::int32_t size, std::uint32_t argb) {
  fo4::swf::SwfShape shape;
  shape.shape_id = id;
  fo4::swf::SwfFillStyle fill;
  fill.type = 0x00;
  fill.color = argb;
  shape.fill_styles.push_back(fill);
  shape.records = {move_to(0, 0),    style_change(1, 0, 0), line_by(size, 0),
                   line_by(0, size), line_by(-size, 0),     line_by(0, -size)};
  fo4::swf::SwfTag tag;
  tag.shape = std::move(shape);
  return tag;
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

fo4::swf::SwfTag text_tag(std::uint16_t character_id, std::uint32_t argb) {
  fo4::swf::SwfTextRecord record;
  record.font_id = 7;
  record.color = argb;
  record.height = 1024;
  record.glyphs = {{0, 0}};
  fo4::swf::SwfTag tag;
  tag.text = fo4::swf::SwfText{};
  tag.text->character_id = character_id;
  tag.text->records = {record};
  return tag;
}

fo4::swf::SwfFrameInstance frame_row(std::uint16_t depth, std::uint16_t character,
                                     std::uint32_t level) {
  fo4::swf::SwfFrameInstance row;
  row.depth = depth;
  row.character_id = character;
  row.nesting_level = level;
  return row;
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

}  // namespace

TEST(SwfFrameSvgTextClip, StaticTextMaskClipsTheRowsBelowItAtItsWorldTransform) {
  auto mask = frame_row(1, 10, 0);
  mask.clip_depth = 2;
  mask.world_transform.translate_x = 20;
  const std::string expected =
      kStage +
      "<clipPath id=\"r0clip\" transform=\"matrix(1 0 0 1 20 0)\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" transform=\"matrix(1 0 0 1 0 0)\" fill=\"#ff0000\" "
      "fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</clipPath>\n"
      "<g clip-path=\"url(#r0clip)\">\n"
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n" +
      kBlueSquare +
      "</g>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(tags_frame_svg({square_glyph_font_tag(7, 1000), text_tag(10, 0xFFFF0000u),
                            shape_tag(2, 600, 0xFF0000FFu)},
                           {mask, frame_row(2, 2, 0)}),
            expected);
}

TEST(SwfFrameSvgTextClip, StaticTextInsideAClipSpriteJoinsTheSpriteMask) {
  auto mask = frame_row(1, 9, 0);
  mask.is_sprite = true;
  mask.clip_depth = 2;
  auto text_child = frame_row(1, 10, 1);
  text_child.world_transform.translate_x = 20;
  const std::string expected =
      kStage +
      "<clipPath id=\"r0clip\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" transform=\"matrix(1 0 0 1 20 0)\" fill=\"#ff0000\" "
      "fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</clipPath>\n"
      "<g clip-path=\"url(#r0clip)\">\n"
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n" +
      kBlueSquare +
      "</g>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(tags_frame_svg({square_glyph_font_tag(7, 1000), text_tag(10, 0xFFFF0000u),
                            shape_tag(2, 600, 0xFF0000FFu)},
                           {mask, text_child, frame_row(2, 2, 0)}),
            expected);
}
