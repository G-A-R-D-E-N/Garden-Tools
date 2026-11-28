#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::swf {

/* Requires a DefineFont2 or DefineFont3 with outlines, a code table and a layout; out is then a complete TrueType sfnt in which SWF glyph i is glyph i + 1 (glyph 0 is an empty .notdef). */
bool build_font_ttf(const SwfFont& font, std::vector<std::uint8_t>& out, std::string& message);

}  // namespace fo4::swf
