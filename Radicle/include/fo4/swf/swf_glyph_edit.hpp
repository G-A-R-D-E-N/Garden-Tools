#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::swf {

/* Requires a decoded DefineFont, DefineFont2 or DefineFont3 tag and a glyph index it holds; the payload keeps every other table. */
bool replace_glyph_payload(const SwfTag& tag, std::size_t glyph_index, const SwfGlyph& shape,
                           std::vector<std::byte>& out, std::string& message);

}  // namespace fo4::swf
