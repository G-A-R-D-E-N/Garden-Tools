#include "fo4/swf/swf_glyph_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::swf {

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
  SwfFont font = *tag.font;
  font.glyphs[glyph_index] = shape;
  return encode_define_font(font, tag.code, out, message);
}

}  // namespace fo4::swf
