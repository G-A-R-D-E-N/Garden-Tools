#include "fo4/swf/swf_art.hpp"

#include "fo4/constants.hpp"
#include "fo4/swf/swf_bitmap_png.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr double kFixedPointOne = 65536.0;
constexpr double kFocalPointOne = 256.0;
constexpr double kMiterLimitOne = 256.0;
constexpr double kFontUnits = 1024.0;
constexpr double kFont3Units = 20480.0;
constexpr std::size_t kCurveSegments = 8;
constexpr std::uint8_t kSolidFill = 0x00;
constexpr std::uint8_t kFirstBitmapFill = 0x40;
constexpr std::uint8_t kLastBitmapFill = 0x43;
constexpr std::uint32_t kOpaqueBlack = 0xFF000000u;

struct Point {
  std::int64_t x{0};
  std::int64_t y{0};

  auto operator<=>(const Point&) const = default;
};

using StartIndex = std::map<Point, std::vector<std::size_t>>;

struct Edge {
  Point start;
  Point control;
  Point end;
  bool curve{false};
  std::size_t fill0{0};
  std::size_t fill1{0};
  std::size_t line{0};
};

ArtPoint twips_point(Point point) {
  return ArtPoint{static_cast<double>(point.x), static_cast<double>(point.y)};
}

std::vector<Edge> trace_edges(const SwfShape& shape) {
  std::vector<Edge> edges;
  Point pen;
  std::size_t fill0 = 0;
  std::size_t fill1 = 0;
  std::size_t line = 0;
  for (const SwfShapeRecord& record : shape.records) {
    switch (record.kind) {
      case SwfShapeRecordKind::move:
        pen = Point{record.x, record.y};
        break;
      case SwfShapeRecordKind::style_change:
        if (record.has_fill0 || record.fill0 != 0) {
          fill0 = record.fill0;
        }
        if (record.has_fill1 || record.fill1 != 0) {
          fill1 = record.fill1;
        }
        if (record.has_line || record.line != 0) {
          line = record.line;
        }
        break;
      case SwfShapeRecordKind::straight_edge: {
        const Point end{pen.x + record.x, pen.y + record.y};
        edges.push_back(Edge{pen, pen, end, false, fill0, fill1, line});
        pen = end;
        break;
      }
      case SwfShapeRecordKind::curved_edge: {
        const Point control{pen.x + record.control_x, pen.y + record.control_y};
        const Point end{control.x + record.x, control.y + record.y};
        edges.push_back(Edge{pen, control, end, true, fill0, fill1, line});
        pen = end;
        break;
      }
    }
  }
  return edges;
}

Edge reversed(Edge edge) {
  std::swap(edge.start, edge.end);
  return edge;
}

std::vector<Edge> fill_edges(const std::vector<Edge>& edges, std::size_t index) {
  std::vector<Edge> out;
  for (const Edge& edge : edges) {
    if (edge.fill0 == index) {
      out.push_back(edge);
    }
    if (edge.fill1 == index) {
      out.push_back(reversed(edge));
    }
  }
  return out;
}

StartIndex index_by_start(const std::vector<Edge>& edges) {
  StartIndex index;
  for (std::size_t edge = edges.size(); edge-- > 0;) {
    index[edges[edge].start].push_back(edge);
  }
  return index;
}

std::optional<std::size_t> take_unused_from(StartIndex& index, std::vector<bool>& used,
                                            Point from) {
  const auto bucket = index.find(from);
  if (bucket == index.end()) {
    return std::nullopt;
  }
  auto& starting = bucket->second;
  while (!starting.empty() && used[starting.back()]) {
    starting.pop_back();
  }
  if (starting.empty()) {
    return std::nullopt;
  }
  used[starting.back()] = true;
  return starting.back();
}

void append_edge(const Edge& edge, std::vector<ArtPoint>& out) {
  if (!edge.curve) {
    out.push_back(twips_point(edge.end));
    return;
  }
  const ArtPoint start = twips_point(edge.start);
  const ArtPoint control = twips_point(edge.control);
  const ArtPoint end = twips_point(edge.end);
  for (std::size_t index = 1; index < kCurveSegments; ++index) {
    const double t = static_cast<double>(index) / static_cast<double>(kCurveSegments);
    const double rest = 1.0 - t;
    out.push_back({rest * rest * start.x + 2.0 * rest * t * control.x + t * t * end.x,
                   rest * rest * start.y + 2.0 * rest * t * control.y + t * t * end.y});
  }
  out.push_back(end);
}

ArtContour contour_of(std::vector<ArtPoint> points, bool closed, double scale) {
  if (closed) {
    points.pop_back();
  }
  ArtContour contour;
  contour.closed = closed;
  contour.points.reserve(points.size());
  for (const ArtPoint& point : points) {
    contour.points.push_back({point.x * scale, point.y * scale});
  }
  return contour;
}

std::vector<ArtContour> fill_contours(const std::vector<Edge>& edges, std::size_t index,
                                      double scale) {
  const std::vector<Edge> directed = fill_edges(edges, index);
  std::vector<ArtContour> contours;
  std::vector<bool> used(directed.size(), false);
  StartIndex starts = index_by_start(directed);
  for (std::size_t first = 0; first < directed.size(); ++first) {
    if (used[first]) {
      continue;
    }
    used[first] = true;
    const Point origin = directed[first].start;
    Point pen = directed[first].end;
    std::vector<ArtPoint> points{twips_point(origin)};
    append_edge(directed[first], points);
    while (pen != origin) {
      const std::optional<std::size_t> next = take_unused_from(starts, used, pen);
      if (!next) {
        break;
      }
      append_edge(directed[*next], points);
      pen = directed[*next].end;
    }
    contours.push_back(contour_of(std::move(points), pen == origin, scale));
  }
  return contours;
}

std::vector<ArtContour> line_contours(const std::vector<Edge>& edges, std::size_t index,
                                      double scale) {
  std::vector<ArtContour> contours;
  std::vector<ArtPoint> points;
  bool connected = false;
  Point origin;
  Point pen;
  const auto flush = [&]() {
    if (connected) {
      contours.push_back(contour_of(std::move(points), pen == origin, scale));
    }
    points.clear();
    connected = false;
  };
  for (const Edge& edge : edges) {
    if (edge.line != index) {
      continue;
    }
    if (!connected || edge.start != pen) {
      flush();
      origin = edge.start;
      points.push_back(twips_point(edge.start));
    }
    append_edge(edge, points);
    pen = edge.end;
    connected = true;
  }
  flush();
  return contours;
}

bool is_bitmap_fill(std::uint8_t type) {
  return type >= kFirstBitmapFill && type <= kLastBitmapFill;
}

ArtFill fill_art(const SwfFillStyle& style) {
  ArtFill fill;
  fill.kind = style.type;
  fill.color = style.color;
  fill.bitmap_id = style.bitmap_id;
  fill.spread = style.spread;
  fill.interpolation = style.interpolation;
  if (style.focal_point) {
    fill.focal_point = *style.focal_point / kFocalPointOne;
  }
  const SwfMatrix matrix = style.matrix.value_or(SwfMatrix{});
  const double linear =
      is_bitmap_fill(style.type) ? kFixedPointOne * k_twips_per_pixel : kFixedPointOne;
  fill.matrix = {matrix.scale_x / linear,
                 matrix.rotate_skew0 / linear,
                 matrix.rotate_skew1 / linear,
                 matrix.scale_y / linear,
                 matrix.translate_x / k_twips_per_pixel,
                 matrix.translate_y / k_twips_per_pixel};
  for (const SwfGradientStop& stop : style.stops) {
    fill.stops.push_back({stop.ratio, stop.color});
  }
  return fill;
}

std::uint32_t line_colour(const SwfLineStyle& line) {
  if (!line.fill) {
    return line.color;
  }
  if (line.fill->type == kSolidFill) {
    return line.fill->color;
  }
  return line.fill->stops.empty() ? 0u : line.fill->stops.front().color;
}

ArtLine line_art(const SwfLineStyle& style, double scale) {
  ArtLine line;
  line.width = style.width * scale;
  line.color = line_colour(style);
  line.caps = static_cast<std::uint8_t>((style.flags >> 14) & 0x03u);
  line.joins = static_cast<std::uint8_t>((style.flags >> 12) & 0x03u);
  if (style.miter_limit) {
    line.miter_limit = *style.miter_limit / kMiterLimitOne;
  }
  return line;
}

}  // namespace

ArtShape shape_art(const SwfShape& shape, double scale) {
  ArtShape art;
  const std::vector<Edge> edges = trace_edges(shape);
  for (std::size_t index = 0; index < shape.fill_styles.size(); ++index) {
    ArtFill fill = fill_art(shape.fill_styles[index]);
    fill.contours = fill_contours(edges, index + 1, scale);
    if (!fill.contours.empty()) {
      art.fills.push_back(std::move(fill));
    }
  }
  for (std::size_t index = 0; index < shape.line_styles.size(); ++index) {
    ArtLine line = line_art(shape.line_styles[index], scale);
    line.contours = line_contours(edges, index + 1, scale);
    if (!line.contours.empty()) {
      art.lines.push_back(std::move(line));
    }
  }
  return art;
}

MovieArt movie_art(const SwfFile& file) {
  MovieArt art;
  std::set<std::uint16_t> shape_ids;
  std::set<std::uint16_t> bitmap_ids;
  std::set<std::uint16_t> font_ids;
  for (const SwfTag& tag : file.tags) {
    if (tag.shape) {
      ArtShape shape = shape_art(*tag.shape, 1.0 / k_twips_per_pixel);
      shape.character_id = tag.shape->shape_id;
      if ((!shape.fills.empty() || !shape.lines.empty()) &&
          shape_ids.insert(shape.character_id).second) {
        art.shapes.push_back(std::move(shape));
      }
    }
    if (tag.bitmap && bitmap_ids.count(tag.bitmap->character_id) == 0) {
      std::optional<std::vector<std::uint8_t>> png = bitmap_to_png(*tag.bitmap);
      if (png) {
        ArtBitmap bitmap;
        bitmap.character_id = tag.bitmap->character_id;
        bitmap.width = tag.bitmap->width;
        bitmap.height = tag.bitmap->height;
        bitmap.png = std::move(*png);
        bitmap_ids.insert(bitmap.character_id);
        art.bitmaps.push_back(std::move(bitmap));
      }
    }
    if (tag.font && font_ids.insert(tag.font->font_id).second) {
      ArtFont font;
      font.font_id = tag.font->font_id;
      const double units = tag.font->define_font3 ? kFont3Units : kFontUnits;
      SwfFillStyle solid;
      solid.type = kSolidFill;
      solid.color = kOpaqueBlack;
      for (std::size_t index = 0; index < tag.font->glyphs.size(); ++index) {
        SwfShape glyph;
        glyph.fill_styles.push_back(solid);
        glyph.records = tag.font->glyphs[index].records;
        ArtShape outline = shape_art(glyph, 1.0 / units);
        outline.character_id = static_cast<std::uint16_t>(index);
        font.glyphs.push_back(std::move(outline));
      }
      art.fonts.push_back(std::move(font));
    }
  }
  for (ArtShape& shape : art.shapes) {
    for (ArtFill& fill : shape.fills) {
      fill.bitmap_missing = is_bitmap_fill(fill.kind) && bitmap_ids.count(fill.bitmap_id) == 0;
    }
  }
  return art;
}

}  // namespace fo4::swf
