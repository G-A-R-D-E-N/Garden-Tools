#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fo4::swf::detail {

namespace {

constexpr std::uint32_t kMaxShapeRecords = 1u << 20;

bool read_color(Cursor& cursor, bool alpha, std::uint32_t& out) {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t opacity = 0xFF;
  if (!cursor.u8(red) || !cursor.u8(green) || !cursor.u8(blue)) {
    return false;
  }
  if (alpha && !cursor.u8(opacity)) {
    return false;
  }
  out = (static_cast<std::uint32_t>(opacity) << 24) |
        (static_cast<std::uint32_t>(red) << 16) |
        (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
  return true;
}

bool read_gradient(Cursor& cursor, unsigned version, SwfFillStyle& fill) {
  SwfMatrix matrix;
  if (!cursor.matrix(matrix)) {
    return false;
  }
  fill.matrix = matrix;
  std::uint8_t flags = 0;
  if (!cursor.u8(flags)) {
    return false;
  }
  fill.spread = static_cast<std::uint8_t>((flags >> 6) & 0x03u);
  fill.interpolation = static_cast<std::uint8_t>((flags >> 4) & 0x03u);
  const auto count = static_cast<unsigned>(flags & 0x0Fu);
  for (unsigned index = 0; index < count; ++index) {
    SwfGradientStop stop;
    if (!cursor.u8(stop.ratio) || !read_color(cursor, version >= 3, stop.color)) {
      return false;
    }
    fill.stops.push_back(stop);
  }
  return true;
}

}  // namespace

bool Cursor::u8(std::uint8_t& out) {
  if (position >= data.size()) {
    return false;
  }
  out = byte_at(data, position);
  ++position;
  return true;
}

bool Cursor::u16(std::uint16_t& out) {
  if (position > data.size() || data.size() - position < 2) {
    return false;
  }
  out = static_cast<std::uint16_t>(static_cast<std::uint16_t>(byte_at(data, position)) |
                                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(
                                                                  byte_at(data, position + 1))
                                                              << 8));
  position += 2;
  return true;
}

bool Cursor::u32(std::uint32_t& out) {
  if (position > data.size() || data.size() - position < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(byte_at(data, position)) |
        (static_cast<std::uint32_t>(byte_at(data, position + 1)) << 8) |
        (static_cast<std::uint32_t>(byte_at(data, position + 2)) << 16) |
        (static_cast<std::uint32_t>(byte_at(data, position + 3)) << 24);
  position += 4;
  return true;
}

bool Cursor::matrix(SwfMatrix& out) {
  BitReader bits(data, position * 8);
  if (!read_matrix(bits, out)) {
    return false;
  }
  position = bits.byte_position();
  return true;
}

bool read_fill_style(Cursor& cursor, unsigned version, SwfFillStyle& out) {
  std::uint8_t kind = 0;
  if (!cursor.u8(kind)) {
    return false;
  }
  SwfFillStyle fill;
  fill.type = kind;
  switch (kind) {
    case 0x00:
      if (!read_color(cursor, version >= 3, fill.color)) {
        return false;
      }
      break;
    case 0x10:
    case 0x12:
    case 0x13:
      if (!read_gradient(cursor, version, fill)) {
        return false;
      }
      if (kind == 0x13) {
        std::uint16_t focal = 0;
        if (!cursor.u16(focal)) {
          return false;
        }
        fill.focal_point = static_cast<std::int16_t>(focal);
      }
      break;
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43: {
      std::uint16_t bitmap = 0;
      SwfMatrix matrix;
      if (!cursor.u16(bitmap) || !cursor.matrix(matrix)) {
        return false;
      }
      fill.bitmap_id = bitmap;
      fill.matrix = matrix;
      break;
    }
    default:
      return false;
  }
  out = std::move(fill);
  return true;
}

bool read_line_style(Cursor& cursor, unsigned version, SwfLineStyle& out) {
  SwfLineStyle line;
  if (!cursor.u16(line.width)) {
    return false;
  }
  if (version < 4) {
    if (!read_color(cursor, version >= 3, line.color)) {
      return false;
    }
    out = std::move(line);
    return true;
  }
  std::uint8_t high = 0;
  std::uint8_t low = 0;
  if (!cursor.u8(high) || !cursor.u8(low)) {
    return false;
  }
  line.flags = static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8 | low);
  const auto join = static_cast<unsigned>((line.flags >> 12) & 0x03u);
  if (join == 2) {
    std::uint16_t miter = 0;
    if (!cursor.u16(miter)) {
      return false;
    }
    line.miter_limit = miter;
  }
  if ((line.flags & 0x0800u) != 0) {
    SwfFillStyle fill;
    if (!read_fill_style(cursor, version, fill)) {
      return false;
    }
    line.fill = std::move(fill);
  } else if (!read_color(cursor, true, line.color)) {
    return false;
  }
  out = std::move(line);
  return true;
}

bool read_shape_styles(Cursor& cursor, unsigned version, SwfShapeStyles& out) {
  std::uint8_t count = 0;
  if (!cursor.u8(count)) {
    return false;
  }
  std::size_t fill_count = count;
  if (count == 0xFF && version >= 2) {
    std::uint16_t extended = 0;
    if (!cursor.u16(extended)) {
      return false;
    }
    fill_count = extended;
  }
  for (std::size_t index = 0; index < fill_count; ++index) {
    SwfFillStyle fill;
    if (!read_fill_style(cursor, version, fill)) {
      return false;
    }
    out.fills.push_back(std::move(fill));
  }
  if (!cursor.u8(count)) {
    return false;
  }
  std::size_t line_count = count;
  if (count == 0xFF && version >= 2) {
    std::uint16_t extended = 0;
    if (!cursor.u16(extended)) {
      return false;
    }
    line_count = extended;
  }
  for (std::size_t index = 0; index < line_count; ++index) {
    SwfLineStyle line;
    if (!read_line_style(cursor, version, line)) {
      return false;
    }
    out.lines.push_back(std::move(line));
  }
  std::uint8_t bits = 0;
  if (!cursor.u8(bits)) {
    return false;
  }
  out.fill_bits = static_cast<unsigned>(bits >> 4);
  out.line_bits = static_cast<unsigned>(bits & 0x0Fu);
  return true;
}

bool read_shape_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                        bool morph, SwfShapeStyles& styles, ShapeRecordCounts& counts,
                        std::size_t& next) {
  BitReader bits(data, start * 8);
  while (true) {
    if (counts.records >= kMaxShapeRecords) {
      return false;
    }
    std::uint32_t edge = 0;
    if (!bits.read_unsigned(1, edge)) {
      return false;
    }
    if (edge != 0) {
      std::uint32_t straight = 0;
      std::uint32_t raw_bits = 0;
      if (!bits.read_unsigned(1, straight) || !bits.read_unsigned(4, raw_bits)) {
        return false;
      }
      const auto width = static_cast<unsigned>(raw_bits) + 2u;
      std::int32_t value = 0;
      std::int32_t other = 0;
      if (straight != 0) {
        std::uint32_t general = 0;
        if (!bits.read_unsigned(1, general)) {
          return false;
        }
        if (general != 0) {
          if (!bits.read_signed(width, value) || !bits.read_signed(width, other)) {
            return false;
          }
        } else {
          std::uint32_t vertical = 0;
          if (!bits.read_unsigned(1, vertical)) {
            return false;
          }
          if (!bits.read_signed(width, value)) {
            return false;
          }
        }
      } else {
        for (unsigned term = 0; term < 4; ++term) {
          if (!bits.read_signed(width, value)) {
            return false;
          }
        }
        ++counts.curved_edges;
      }
      ++counts.edges;
      ++counts.records;
      continue;
    }
    std::uint32_t flags = 0;
    if (!bits.read_unsigned(5, flags)) {
      return false;
    }
    if (flags == 0) {
      break;
    }
    if ((flags & 0x01u) != 0) {
      std::uint32_t move_bits = 0;
      std::int32_t x = 0;
      std::int32_t y = 0;
      if (!bits.read_unsigned(5, move_bits) || !bits.read_signed(move_bits, x) ||
          !bits.read_signed(move_bits, y)) {
        return false;
      }
    }
    std::uint32_t index = 0;
    for (std::uint32_t mask = 0x02u; mask <= 0x08u; mask <<= 1) {
      if ((flags & mask) == 0) {
        continue;
      }
      if (!bits.read_unsigned(mask == 0x08u ? styles.line_bits : styles.fill_bits, index)) {
        return false;
      }
    }
    if ((flags & 0x10u) != 0) {
      bits.align();
      Cursor cursor{data, bits.byte_position()};
      SwfShapeStyles replacement;
      std::size_t fill_count = 0;
      std::size_t line_count = 0;
      const bool parsed =
          morph ? read_morph_styles(cursor, version, replacement, fill_count, line_count)
                : read_shape_styles(cursor, version, replacement);
      if (!parsed) {
        return false;
      }
      styles = std::move(replacement);
      bits = BitReader(data, cursor.position * 8);
    }
    ++counts.style_changes;
    ++counts.records;
  }
  bits.align();
  next = bits.byte_position();
  return true;
}

SwfStatus decode_shape_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  unsigned version = 1;
  switch (out_tag.code) {
    case tag::kDefineShape:
      version = 1;
      break;
    case tag::kDefineShape2:
      version = 2;
      break;
    case tag::kDefineShape3:
      version = 3;
      break;
    default:
      version = 4;
      break;
  }
  Cursor cursor{data, 0};
  SwfShape shape;
  if (!cursor.u16(shape.shape_id)) {
    return SwfStatus::malformed_tag;
  }
  std::size_t next = 0;
  if (!read_rect(data, cursor.position, shape.bounds, next)) {
    return SwfStatus::malformed_tag;
  }
  cursor.position = next;
  if (version >= 4) {
    if (!read_rect(data, cursor.position, shape.edge_bounds, next)) {
      return SwfStatus::malformed_tag;
    }
    shape.has_edge_bounds = true;
    cursor.position = next;
    if (!cursor.u8(shape.flags)) {
      return SwfStatus::malformed_tag;
    }
  } else {
    shape.edge_bounds = shape.bounds;
  }
  SwfShapeStyles styles;
  if (!read_shape_styles(cursor, version, styles)) {
    return SwfStatus::malformed_tag;
  }
  ShapeRecordCounts counts;
  std::size_t records_end = 0;
  if (!read_shape_records(data, cursor.position, version, false, styles, counts, records_end)) {
    return SwfStatus::malformed_tag;
  }
  if (records_end != data.size()) {
    return SwfStatus::malformed_tag;
  }
  shape.fill_styles = std::move(styles.fills);
  shape.line_styles = std::move(styles.lines);
  shape.edge_count = counts.edges;
  shape.curved_edge_count = counts.curved_edges;
  shape.style_change_count = counts.style_changes;
  out_tag.shape = std::move(shape);
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail
