#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace fo4::swf::detail {

void read_glyph_shapes(std::span<const std::byte> data, std::size_t table_start, SwfFont& font) {
  font.glyphs.clear();
  font.glyphs.reserve(font.glyph_offsets.size());
  ShapeRecordCounts counts;
  for (const std::uint32_t offset : font.glyph_offsets) {
    const std::size_t start = table_start + offset;
    if (start >= data.size()) {
      font.glyphs.clear();
      return;
    }
    const std::uint8_t header = byte_at(data, start);
    SwfShapeStyles styles;
    styles.fills.resize(1);
    styles.fill_bits = static_cast<unsigned>(header >> 4);
    styles.line_bits = static_cast<unsigned>(header & 0x0Fu);
    StyleBounds bounds{styles.fills.size(), styles.lines.size()};
    SwfGlyph glyph;
    glyph.fill_bits = static_cast<std::uint8_t>(styles.fill_bits);
    glyph.line_bits = static_cast<std::uint8_t>(styles.line_bits);
    std::size_t next = 0;
    if (!read_shape_records(data, start + 1, 1, styles, bounds, counts, glyph.records, next) ||
        styles.fills.size() != 1 || !styles.lines.empty()) {
      font.glyphs.clear();
      return;
    }
    font.glyphs.push_back(std::move(glyph));
  }
}

}  // namespace fo4::swf::detail
