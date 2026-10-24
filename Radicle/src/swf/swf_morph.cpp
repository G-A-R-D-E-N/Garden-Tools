#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::swf::detail {

namespace {

bool skip_rgba(Cursor& cursor) {
  std::uint8_t value = 0;
  for (unsigned channel = 0; channel < 4; ++channel) {
    if (!cursor.u8(value)) {
      return false;
    }
  }
  return true;
}

bool skip_gradient_pair(Cursor& cursor) {
  SwfMatrix matrix;
  if (!cursor.matrix(matrix) || !cursor.matrix(matrix)) {
    return false;
  }
  std::uint8_t flags = 0;
  if (!cursor.u8(flags)) {
    return false;
  }
  const auto count = static_cast<unsigned>(flags & 0x0Fu);
  std::uint8_t ratio = 0;
  for (unsigned index = 0; index < count; ++index) {
    if (!cursor.u8(ratio) || !skip_rgba(cursor) || !cursor.u8(ratio) || !skip_rgba(cursor)) {
      return false;
    }
  }
  return true;
}

bool skip_fixed8_pair(Cursor& cursor) {
  std::uint16_t value = 0;
  return cursor.u16(value) && cursor.u16(value);
}

bool read_morph_fill_style(Cursor& cursor) {
  std::uint8_t kind = 0;
  if (!cursor.u8(kind)) {
    return false;
  }
  switch (kind) {
    case 0x00:
      return skip_rgba(cursor) && skip_rgba(cursor);
    case 0x10:
    case 0x12:
      return skip_gradient_pair(cursor);
    case 0x13:
      return skip_gradient_pair(cursor) && skip_fixed8_pair(cursor);
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43: {
      std::uint16_t bitmap = 0;
      SwfMatrix matrix;
      return cursor.u16(bitmap) && cursor.matrix(matrix) && cursor.matrix(matrix);
    }
    default:
      return false;
  }
}

bool read_morph_line_style(Cursor& cursor, unsigned version) {
  std::uint16_t width = 0;
  if (!cursor.u16(width) || !cursor.u16(width)) {
    return false;
  }
  if (version < 2) {
    return skip_rgba(cursor) && skip_rgba(cursor);
  }
  std::uint8_t high = 0;
  std::uint8_t low = 0;
  if (!cursor.u8(high) || !cursor.u8(low)) {
    return false;
  }
  const auto flags = static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8 | low);
  if (((flags >> 12) & 0x03u) == 2 && !cursor.u16(width)) {
    return false;
  }
  if ((flags & 0x0800u) != 0) {
    return read_morph_fill_style(cursor);
  }
  return skip_rgba(cursor) && skip_rgba(cursor);
}

bool read_style_count(Cursor& cursor, std::size_t& count) {
  std::uint8_t first = 0;
  if (!cursor.u8(first)) {
    return false;
  }
  if (first != 0xFF) {
    count = first;
    return true;
  }
  std::uint16_t extended = 0;
  if (!cursor.u16(extended)) {
    return false;
  }
  count = extended;
  return true;
}

}  // namespace

SwfStatus decode_morph_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  const unsigned version = out_tag.code == tag::kDefineMorphShape2 ? 2u : 1u;
  Cursor cursor{data, 0};
  SwfMorphShape morph;
  morph.version = static_cast<std::uint8_t>(version);
  std::size_t next = 0;
  if (!cursor.u16(morph.character_id) ||
      !read_rect(data, cursor.position, morph.start_bounds, next)) {
    return SwfStatus::malformed_tag;
  }
  cursor.position = next;
  if (!read_rect(data, cursor.position, morph.end_bounds, next)) {
    return SwfStatus::malformed_tag;
  }
  cursor.position = next;
  if (version >= 2) {
    if (!read_rect(data, cursor.position, morph.start_edge_bounds, next)) {
      return SwfStatus::malformed_tag;
    }
    cursor.position = next;
    if (!read_rect(data, cursor.position, morph.end_edge_bounds, next) || !cursor.u8(morph.flags)) {
      return SwfStatus::malformed_tag;
    }
    cursor.position = next;
    morph.has_edge_bounds = true;
  } else {
    morph.start_edge_bounds = morph.start_bounds;
    morph.end_edge_bounds = morph.end_bounds;
  }
  std::uint32_t offset = 0;
  if (!cursor.u32(offset)) {
    return SwfStatus::malformed_tag;
  }
  const auto end_edges = cursor.position + static_cast<std::size_t>(offset);
  if (end_edges >= data.size()) {
    return SwfStatus::malformed_tag;
  }
  std::size_t fill_count = 0;
  if (!read_style_count(cursor, fill_count)) {
    return SwfStatus::malformed_tag;
  }
  for (std::size_t index = 0; index < fill_count; ++index) {
    if (!read_morph_fill_style(cursor)) {
      return SwfStatus::malformed_tag;
    }
  }
  std::size_t line_count = 0;
  if (!read_style_count(cursor, line_count)) {
    return SwfStatus::malformed_tag;
  }
  for (std::size_t index = 0; index < line_count; ++index) {
    if (!read_morph_line_style(cursor, version)) {
      return SwfStatus::malformed_tag;
    }
  }
  std::uint8_t bits = 0;
  if (!cursor.u8(bits)) {
    return SwfStatus::malformed_tag;
  }
  ShapeRecordCounts start_counts;
  std::size_t start_end = 0;
  if (!read_shape_records(data, cursor.position, version, static_cast<unsigned>(bits >> 4),
                          static_cast<unsigned>(bits & 0x0Fu), start_counts, start_end)) {
    return SwfStatus::malformed_tag;
  }
  Cursor edges{data, start_end};
  if (start_end != end_edges) {
    edges.position = end_edges;
  }
  std::uint8_t end_bits = 0;
  if (!edges.u8(end_bits)) {
    return SwfStatus::malformed_tag;
  }
  ShapeRecordCounts end_counts;
  std::size_t end_end = 0;
  if (!read_shape_records(data, edges.position, version, static_cast<unsigned>(end_bits >> 4),
                          static_cast<unsigned>(end_bits & 0x0Fu), end_counts, end_end)) {
    return SwfStatus::malformed_tag;
  }
  if (end_end != data.size()) {
    return SwfStatus::malformed_tag;
  }
  morph.fill_style_count = static_cast<std::uint32_t>(fill_count);
  morph.line_style_count = static_cast<std::uint32_t>(line_count);
  morph.start_edge_count = start_counts.edges;
  morph.end_edge_count = end_counts.edges;
  out_tag.morph_shape = morph;
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail
