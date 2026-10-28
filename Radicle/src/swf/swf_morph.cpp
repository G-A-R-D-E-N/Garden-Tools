#include "swf_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace fo4::swf::detail {

namespace {

bool read_rgba(Cursor& cursor, std::uint32_t& out) {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;
  if (!cursor.u8(red) || !cursor.u8(green) || !cursor.u8(blue) || !cursor.u8(alpha)) {
    return false;
  }
  out = (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(red) << 16) |
        (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
  return true;
}

bool read_color_pair(Cursor& cursor, std::uint32_t& start, std::uint32_t& end) {
  return read_rgba(cursor, start) && read_rgba(cursor, end);
}

bool read_matrix_pair(Cursor& cursor, std::optional<SwfMatrix>& start,
                      std::optional<SwfMatrix>& end) {
  SwfMatrix first;
  SwfMatrix second;
  if (!cursor.matrix(first) || !cursor.matrix(second)) {
    return false;
  }
  start = first;
  end = second;
  return true;
}

bool read_morph_fill_style(Cursor& cursor, SwfMorphFillStyle& out) {
  std::uint8_t kind = 0;
  if (!cursor.u8(kind)) {
    return false;
  }
  out.type = kind;
  switch (kind) {
    case 0x00:
      return read_color_pair(cursor, out.start_color, out.end_color);
    case 0x10:
    case 0x12:
    case 0x13: {
      if (!read_matrix_pair(cursor, out.start_matrix, out.end_matrix)) {
        return false;
      }
      std::uint8_t flags = 0;
      if (!cursor.u8(flags)) {
        return false;
      }
      out.spread = static_cast<std::uint8_t>((flags >> 6) & 0x03u);
      out.interpolation = static_cast<std::uint8_t>((flags >> 4) & 0x03u);
      const auto count = static_cast<unsigned>(flags & 0x0Fu);
      for (unsigned index = 0; index < count; ++index) {
        SwfMorphGradientStop stop;
        if (!cursor.u8(stop.start_ratio) || !read_rgba(cursor, stop.start_color) ||
            !cursor.u8(stop.end_ratio) || !read_rgba(cursor, stop.end_color)) {
          return false;
        }
        out.stops.push_back(stop);
      }
      if (kind != 0x13) {
        return true;
      }
      std::uint16_t start_focal = 0;
      std::uint16_t end_focal = 0;
      if (!cursor.u16(start_focal) || !cursor.u16(end_focal)) {
        return false;
      }
      out.start_focal_point = static_cast<std::int16_t>(start_focal);
      out.end_focal_point = static_cast<std::int16_t>(end_focal);
      return true;
    }
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
      return cursor.u16(out.bitmap_id) && read_matrix_pair(cursor, out.start_matrix, out.end_matrix);
    default:
      return false;
  }
}

bool read_morph_line_style(Cursor& cursor, unsigned version, SwfMorphLineStyle& out) {
  if (!cursor.u16(out.start_width) || !cursor.u16(out.end_width)) {
    return false;
  }
  if (version < 2) {
    return read_color_pair(cursor, out.start_color, out.end_color);
  }
  std::uint8_t high = 0;
  std::uint8_t low = 0;
  if (!cursor.u8(high) || !cursor.u8(low)) {
    return false;
  }
  out.flags = static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8 | low);
  if (((out.flags >> 12) & 0x03u) == 2) {
    std::uint16_t start_miter = 0;
    std::uint16_t end_miter = 0;
    if (!cursor.u16(start_miter) || !cursor.u16(end_miter)) {
      return false;
    }
    out.start_miter_limit = start_miter;
    out.end_miter_limit = end_miter;
  }
  if ((out.flags & 0x0800u) == 0) {
    return read_color_pair(cursor, out.start_color, out.end_color);
  }
  SwfMorphFillStyle fill;
  if (!read_morph_fill_style(cursor, fill)) {
    return false;
  }
  out.fill = std::move(fill);
  return true;
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

bool read_morph_styles(Cursor& cursor, unsigned version, SwfMorphStyles& out) {
  std::size_t fill_count = 0;
  if (!read_style_count(cursor, fill_count) ||
      !style_count_fits(cursor, fill_count, kMinFillStyleBytes)) {
    return false;
  }
  for (std::size_t index = 0; index < fill_count; ++index) {
    SwfMorphFillStyle fill;
    if (!read_morph_fill_style(cursor, fill)) {
      return false;
    }
    out.fills.push_back(std::move(fill));
  }
  std::size_t line_count = 0;
  if (!read_style_count(cursor, line_count) ||
      !style_count_fits(cursor, line_count, kMinLineStyleBytes)) {
    return false;
  }
  for (std::size_t index = 0; index < line_count; ++index) {
    SwfMorphLineStyle line;
    if (!read_morph_line_style(cursor, version, line)) {
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
    if (!read_rect(data, cursor.position, morph.end_edge_bounds, next)) {
      return SwfStatus::malformed_tag;
    }
    cursor.position = next;
    if (!cursor.u8(morph.flags)) {
      return SwfStatus::malformed_tag;
    }
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
  SwfMorphStyles styles;
  if (!read_morph_styles(cursor, version, styles)) {
    return SwfStatus::malformed_tag;
  }
  StyleBounds bounds{styles.fills.size(), styles.lines.size()};
  ShapeRecordCounts start_counts;
  std::size_t start_end = 0;
  if (!read_morph_records(data, cursor.position, version, styles, bounds, start_counts, morph.start_records,
                          start_end)) {
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
  styles.fill_bits = static_cast<unsigned>(end_bits >> 4);
  styles.line_bits = static_cast<unsigned>(end_bits & 0x0Fu);
  ShapeRecordCounts end_counts;
  std::size_t end_end = 0;
  if (!read_morph_records(data, edges.position, version, styles, bounds, end_counts, morph.end_records,
                          end_end)) {
    return SwfStatus::malformed_tag;
  }
  if (end_end != data.size()) {
    return SwfStatus::malformed_tag;
  }
  morph.fill_style_count = static_cast<std::uint32_t>(styles.fills.size());
  morph.line_style_count = static_cast<std::uint32_t>(styles.lines.size());
  morph.fill_styles = std::move(styles.fills);
  morph.line_styles = std::move(styles.lines);
  morph.start_edge_count = start_counts.edges;
  morph.end_edge_count = end_counts.edges;
  out_tag.morph_shape = morph;
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail

namespace fo4::swf {

namespace {

double clamp_ratio(double ratio) {
  if (!(ratio > 0.0)) {
    return 0.0;
  }
  return ratio > 1.0 ? 1.0 : ratio;
}

std::int32_t blend_twips(double start, double end, double ratio) {
  return static_cast<std::int32_t>(std::lround(start + (end - start) * ratio));
}

std::int32_t blend_twips(std::int32_t start, std::int32_t end, double ratio) {
  return blend_twips(static_cast<double>(start), static_cast<double>(end), ratio);
}

SwfShapeRecord blend_edges(const SwfShapeRecord& from, const SwfShapeRecord& to, double ratio) {
  const bool from_curves = from.kind == SwfShapeRecordKind::curved_edge;
  const bool to_curves = to.kind == SwfShapeRecordKind::curved_edge;
  if (!from_curves && !to_curves) {
    SwfShapeRecord command{SwfShapeRecordKind::straight_edge};
    command.x = blend_twips(from.x, to.x, ratio);
    command.y = blend_twips(from.y, to.y, ratio);
    return command;
  }
  SwfShapeRecord command{SwfShapeRecordKind::curved_edge};
  const double from_control_x = from_curves ? static_cast<double>(from.control_x) : from.x / 2.0;
  const double from_control_y = from_curves ? static_cast<double>(from.control_y) : from.y / 2.0;
  const double from_anchor_x = from_curves ? static_cast<double>(from.x) : from.x / 2.0;
  const double from_anchor_y = from_curves ? static_cast<double>(from.y) : from.y / 2.0;
  const double to_control_x = to_curves ? static_cast<double>(to.control_x) : to.x / 2.0;
  const double to_control_y = to_curves ? static_cast<double>(to.control_y) : to.y / 2.0;
  const double to_anchor_x = to_curves ? static_cast<double>(to.x) : to.x / 2.0;
  const double to_anchor_y = to_curves ? static_cast<double>(to.y) : to.y / 2.0;
  command.control_x = blend_twips(from_control_x, to_control_x, ratio);
  command.control_y = blend_twips(from_control_y, to_control_y, ratio);
  command.x = blend_twips(from_anchor_x, to_anchor_x, ratio);
  command.y = blend_twips(from_anchor_y, to_anchor_y, ratio);
  return command;
}

constexpr std::uint8_t kSolidFill = 0x00;

std::uint32_t blend_color(std::uint32_t start, std::uint32_t end, double ratio) {
  std::uint32_t blended = 0;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const auto from = static_cast<std::int32_t>((start >> shift) & 0xFFu);
    const auto to = static_cast<std::int32_t>((end >> shift) & 0xFFu);
    blended |= static_cast<std::uint32_t>(blend_twips(from, to, ratio)) << shift;
  }
  return blended;
}

SwfFillStyle blend_solid_fill(const SwfMorphFillStyle& fill, double ratio) {
  SwfFillStyle blended;
  blended.type = kSolidFill;
  blended.color = blend_color(fill.start_color, fill.end_color, ratio);
  return blended;
}

SwfLineStyle blend_line_style(const SwfMorphLineStyle& line, double ratio) {
  SwfLineStyle blended;
  blended.width = static_cast<std::uint16_t>(blend_twips(line.start_width, line.end_width, ratio));
  blended.color = blend_color(line.start_color, line.end_color, ratio);
  blended.flags = line.flags;
  if (line.start_miter_limit && line.end_miter_limit) {
    blended.miter_limit = static_cast<std::uint16_t>(
        blend_twips(*line.start_miter_limit, *line.end_miter_limit, ratio));
  }
  return blended;
}

SwfRect blend_rect(const SwfRect& start, const SwfRect& end, double ratio) {
  SwfRect blended;
  blended.x_min = blend_twips(start.x_min, end.x_min, ratio);
  blended.y_min = blend_twips(start.y_min, end.y_min, ratio);
  blended.x_max = blend_twips(start.x_max, end.x_max, ratio);
  blended.y_max = blend_twips(start.y_max, end.y_max, ratio);
  return blended;
}

}  // namespace

std::vector<SwfShapeRecord> interpolate_morph_commands(const SwfMorphShape& morph, double ratio) {
  const double t = clamp_ratio(ratio);
  std::vector<SwfShapeRecord> commands;
  commands.reserve(morph.start_records.size());
  std::size_t end_index = 0;
  for (const auto& from : morph.start_records) {
    if (from.kind == SwfShapeRecordKind::style_change) {
      commands.push_back(from);
      continue;
    }
    while (end_index < morph.end_records.size() &&
           morph.end_records[end_index].kind == SwfShapeRecordKind::style_change) {
      ++end_index;
    }
    if (end_index >= morph.end_records.size()) {
      break;
    }
    const auto& to = morph.end_records[end_index++];
    if (from.kind == SwfShapeRecordKind::move) {
      SwfShapeRecord command{SwfShapeRecordKind::move};
      command.x = blend_twips(from.x, to.x, t);
      command.y = blend_twips(from.y, to.y, t);
      commands.push_back(command);
      continue;
    }
    commands.push_back(blend_edges(from, to, t));
  }
  return commands;
}

std::optional<SwfShape> interpolate_morph_shape(const SwfMorphShape& morph, double ratio) {
  const double t = clamp_ratio(ratio);
  SwfShape shape;
  shape.shape_id = morph.character_id;
  shape.bounds = blend_rect(morph.start_bounds, morph.end_bounds, t);
  for (const SwfMorphFillStyle& fill : morph.fill_styles) {
    if (fill.type != kSolidFill) {
      return std::nullopt;
    }
    shape.fill_styles.push_back(blend_solid_fill(fill, t));
  }
  for (const SwfMorphLineStyle& line : morph.line_styles) {
    if (line.fill) {
      return std::nullopt;
    }
    shape.line_styles.push_back(blend_line_style(line, t));
  }
  shape.records = interpolate_morph_commands(morph, t);
  return shape;
}

}  // namespace fo4::swf
