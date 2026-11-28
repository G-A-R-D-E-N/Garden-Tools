#include "fo4/swf/swf_glyph_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::swf {

namespace {

bool styles_are_in_range(const SwfGlyph& shape, std::string& message) {
  for (const SwfShapeRecord& record : shape.records) {
    if (record.kind != SwfShapeRecordKind::style_change) {
      continue;
    }
    if (record.fill0 > 1 || record.fill1 > 1) {
      message = "a glyph style change names a fill style other than the implicit fill style 1";
      return false;
    }
    if (record.line != 0) {
      message = "a glyph style change names a line style, but a glyph SHAPE has no line styles";
      return false;
    }
  }
  return true;
}

}  // namespace

bool replace_glyph_payload(const SwfTag& tag, std::size_t glyph_index, const SwfGlyph& shape,
                           std::vector<std::byte>& out, std::string& message) {
  out.clear();
  message.clear();
  if (!tag.font || (tag.code != tag::kDefineFont && tag.code != tag::kDefineFont2 &&
                    tag.code != tag::kDefineFont3)) {
    message = "the tag is not a decoded DefineFont, DefineFont2 or DefineFont3";
    return false;
  }
  if (tag.font->glyphs.size() != tag.font->glyph_count) {
    message = "the decoded glyph list does not match the glyph count";
    return false;
  }
  if (glyph_index >= tag.font->glyphs.size()) {
    message = "the glyph index is out of range";
    return false;
  }
  if (!styles_are_in_range(shape, message)) {
    return false;
  }
  SwfFont font = *tag.font;
  font.glyphs[glyph_index] = shape;
  return encode_define_font(font, tag.code, out, message);
}

}  // namespace fo4::swf
