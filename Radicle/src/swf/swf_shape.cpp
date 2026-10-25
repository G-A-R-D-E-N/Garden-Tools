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

bool style_count_fits(const Cursor& cursor, std::size_t count, std::size_t minimum_bytes) {
  const auto remaining =
      cursor.position < cursor.data.size() ? cursor.data.size() - cursor.position : 0;
  return count <= remaining / minimum_bytes;
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
  if (!style_count_fits(cursor, fill_count, kMinFillStyleBytes)) {
    return false;
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
  if (!style_count_fits(cursor, line_count, kMinLineStyleBytes)) {
    return false;
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

namespace {

bool read_styles_block(Cursor& cursor, unsigned version, SwfShapeStyles& out) {
  return read_shape_styles(cursor, version, out);
}

bool read_styles_block(Cursor& cursor, unsigned version, SwfMorphStyles& out) {
  return read_morph_styles(cursor, version, out);
}

void push_record(std::vector<SwfShapeRecord>* records, SwfShapeRecord record) {
  if (records != nullptr) {
    records->push_back(record);
  }
}

template <typename Styles>
bool read_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                  Styles& styles, StyleBounds& bounds, ShapeRecordCounts& counts,
                  std::vector<SwfShapeRecord>* records, std::size_t& next) {
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
      std::int32_t delta_x = 0;
      std::int32_t delta_y = 0;
      if (straight != 0) {
        std::uint32_t general = 0;
        if (!bits.read_unsigned(1, general)) {
          return false;
        }
        if (general != 0) {
          if (!bits.read_signed(width, delta_x) || !bits.read_signed(width, delta_y)) {
            return false;
          }
        } else {
          std::uint32_t vertical = 0;
          if (!bits.read_unsigned(1, vertical)) {
            return false;
          }
          if (vertical != 0) {
            if (!bits.read_signed(width, delta_y)) {
              return false;
            }
          } else if (!bits.read_signed(width, delta_x)) {
            return false;
          }
        }
        SwfShapeRecord record{SwfShapeRecordKind::straight_edge};
        record.x = delta_x;
        record.y = delta_y;
        push_record(records, record);
      } else {
        SwfShapeRecord record{SwfShapeRecordKind::curved_edge};
        if (!bits.read_signed(width, record.control_x) || !bits.read_signed(width, record.control_y) ||
            !bits.read_signed(width, record.x) || !bits.read_signed(width, record.y)) {
          return false;
        }
        push_record(records, record);
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
      SwfShapeRecord record{SwfShapeRecordKind::move};
      if (!bits.read_unsigned(5, move_bits) || !bits.read_signed(move_bits, record.x) ||
          !bits.read_signed(move_bits, record.y)) {
        return false;
      }
      push_record(records, record);
    }
    std::uint32_t fill0 = 0;
    std::uint32_t fill1 = 0;
    std::uint32_t line = 0;
    for (std::uint32_t mask = 0x02u; mask <= 0x08u; mask <<= 1) {
      if ((flags & mask) == 0) {
        continue;
      }
      std::uint32_t& index = mask == 0x02u ? fill0 : (mask == 0x04u ? fill1 : line);
      if (!bits.read_unsigned(mask == 0x08u ? styles.line_bits : styles.fill_bits, index)) {
        return false;
      }
    }
    std::size_t fill_generation = bounds.fills;
    std::size_t line_generation = bounds.lines;
    std::size_t fill_generation_base = bounds.fill_base;
    std::size_t line_generation_base = bounds.line_base;
    std::size_t replaced_fills = bounds.fills;
    std::size_t replaced_lines = bounds.lines;
    std::size_t replaced_fill_base = bounds.fill_base;
    std::size_t replaced_line_base = bounds.line_base;
    if ((flags & 0x10u) != 0) {
      bits.align();
      Cursor cursor{data, bits.byte_position()};
      Styles replacement;
      if (!read_styles_block(cursor, version, replacement)) {
        return false;
      }
      fill_generation = replacement.fills.size();
      line_generation = replacement.lines.size();
      fill_generation_base = styles.fills.size();
      line_generation_base = styles.lines.size();
      styles.fills.insert(styles.fills.end(), replacement.fills.begin(), replacement.fills.end());
      styles.lines.insert(styles.lines.end(), replacement.lines.begin(), replacement.lines.end());
      styles.fill_bits = replacement.fill_bits;
      styles.line_bits = replacement.line_bits;
      bits = BitReader(data, cursor.position * 8);
      bounds.fills = fill_generation;
      bounds.lines = line_generation;
      bounds.fill_base = fill_generation_base;
      bounds.line_base = line_generation_base;
    }
    const std::size_t fill_bound = fill_generation > replaced_fills ? fill_generation : replaced_fills;
    const std::size_t line_bound = line_generation > replaced_lines ? line_generation : replaced_lines;
    if (((flags & 0x02u) != 0 && fill0 > fill_bound) || ((flags & 0x04u) != 0 && fill1 > fill_bound) ||
        ((flags & 0x08u) != 0 && line > line_bound)) {
      return false;
    }
    if ((flags & 0x0Eu) != 0) {
      const auto rebase = [](std::uint32_t index, std::size_t generation, std::size_t generation_base,
                             std::size_t older_base) {
        if (index == 0) {
          return index;
        }
        return index <= generation ? static_cast<std::uint32_t>(generation_base + index)
                                   : static_cast<std::uint32_t>(older_base + index);
      };
      fill0 = rebase(fill0, fill_generation, fill_generation_base, replaced_fill_base);
      fill1 = rebase(fill1, fill_generation, fill_generation_base, replaced_fill_base);
      line = rebase(line, line_generation, line_generation_base, replaced_line_base);
      if (fill0 > 0xFFFFu || fill1 > 0xFFFFu || line > 0xFFFFu) {
        return false;
      }
      SwfShapeRecord record{SwfShapeRecordKind::style_change};
      record.fill0 = static_cast<std::uint16_t>(fill0);
      record.fill1 = static_cast<std::uint16_t>(fill1);
      record.line = static_cast<std::uint16_t>(line);
      push_record(records, record);
    }
    ++counts.style_changes;
    ++counts.records;
  }
  bits.align();
  next = bits.byte_position();
  return true;
}

}  // namespace

bool read_shape_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                        SwfShapeStyles& styles, StyleBounds& bounds, ShapeRecordCounts& counts,
                        std::vector<SwfShapeRecord>& records, std::size_t& next) {
  return read_records(data, start, version, styles, bounds, counts, &records, next);
}

bool read_morph_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                        SwfMorphStyles& styles, StyleBounds& bounds, ShapeRecordCounts& counts,
                        std::vector<SwfShapeRecord>& records, std::size_t& next) {
  return read_records(data, start, version, styles, bounds, counts, &records, next);
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
  StyleBounds bounds{styles.fills.size(), styles.lines.size()};
  if (!read_shape_records(data, cursor.position, version, styles, bounds, counts, shape.records,
                          records_end)) {
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
