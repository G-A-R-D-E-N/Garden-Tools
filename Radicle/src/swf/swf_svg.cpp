#include "fo4/swf/swf_svg.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr double kTwipsPerPixel = 20.0;
constexpr double kFixedPointOne = 65536.0;
constexpr double kFocalPointOne = 256.0;
constexpr double kMiterLimitOne = 256.0;
constexpr double kRatioOne = 255.0;
constexpr double kAlphaOne = 255.0;
constexpr double kGradientHalfPixels = 819.2;
constexpr std::uint8_t kSolidFill = 0x00;
constexpr std::uint8_t kLinearFill = 0x10;
constexpr std::uint8_t kRadialFill = 0x12;
constexpr std::uint8_t kFocalFill = 0x13;
constexpr unsigned kMiterJoin = 2;
constexpr const char* kPlaceholderColor = "#808080";

struct Point {
  std::int64_t x{0};
  std::int64_t y{0};

  bool operator==(const Point&) const = default;
};

struct Edge {
  Point start;
  Point control;
  Point end;
  bool curve{false};
  std::size_t fill0{0};
  std::size_t fill1{0};
  std::size_t line{0};
};

std::string number(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.6f", value);
  std::string text(buffer);
  while (text.back() == '0') {
    text.pop_back();
  }
  if (text.back() == '.') {
    text.pop_back();
  }
  return text;
}

std::string pixels(std::int64_t twips) {
  return number(static_cast<double>(twips) / kTwipsPerPixel);
}

std::string point_text(Point point) {
  return pixels(point.x) + " " + pixels(point.y);
}

std::string hex_color(std::uint32_t argb) {
  char buffer[8];
  std::snprintf(buffer, sizeof buffer, "#%06x", static_cast<unsigned>(argb & 0xFFFFFFu));
  return buffer;
}

std::string alpha_text(std::uint32_t argb) {
  return number(static_cast<double>(argb >> 24) / kAlphaOne);
}

std::string paint(const char* name, std::uint32_t argb) {
  return std::string(name) + "=\"" + hex_color(argb) + "\" " + name + "-opacity=\"" +
         alpha_text(argb) + "\"";
}

std::string edge_text(const Edge& edge) {
  if (edge.curve) {
    return "Q" + point_text(edge.control) + " " + point_text(edge.end);
  }
  return "L" + point_text(edge.end);
}

std::vector<Edge> trace_edges(const SwfShape& shape) {
  std::vector<Edge> edges;
  Point pen;
  std::size_t fill0 = 0;
  std::size_t fill1 = 0;
  std::size_t line = 0;
  for (const auto& record : shape.records) {
    switch (record.kind) {
      case SwfShapeRecordKind::move:
        pen = Point{record.x, record.y};
        break;
      case SwfShapeRecordKind::style_change:
        // Requires a zero index to mean unchanged: the decoded record cannot tell unset from zero.
        if (record.fill0 != 0) {
          fill0 = record.fill0;
        }
        if (record.fill1 != 0) {
          fill1 = record.fill1;
        }
        if (record.line != 0) {
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
  for (const auto& edge : edges) {
    if (edge.fill0 == index) {
      out.push_back(edge);
    }
    if (edge.fill1 == index) {
      out.push_back(reversed(edge));
    }
  }
  return out;
}

std::size_t find_unused_from(const std::vector<Edge>& edges, const std::vector<bool>& used,
                             Point from) {
  for (std::size_t index = 0; index < edges.size(); ++index) {
    if (!used[index] && edges[index].start == from) {
      return index;
    }
  }
  return edges.size();
}

std::string loops_text(const std::vector<Edge>& edges) {
  std::string text;
  std::vector<bool> used(edges.size(), false);
  for (std::size_t first = 0; first < edges.size(); ++first) {
    if (used[first]) {
      continue;
    }
    used[first] = true;
    const Point origin = edges[first].start;
    Point pen = edges[first].end;
    text += "M" + point_text(origin) + edge_text(edges[first]);
    while (pen != origin) {
      const std::size_t next = find_unused_from(edges, used, pen);
      if (next == edges.size()) {
        break;
      }
      used[next] = true;
      text += edge_text(edges[next]);
      pen = edges[next].end;
    }
    if (pen == origin) {
      text += "Z";
    }
  }
  return text;
}

std::string stroke_text(const std::vector<Edge>& edges, std::size_t line) {
  std::string text;
  bool connected = false;
  Point pen;
  for (const auto& edge : edges) {
    if (edge.line != line) {
      continue;
    }
    if (!connected || edge.start != pen) {
      text += "M" + point_text(edge.start);
    }
    text += edge_text(edge);
    pen = edge.end;
    connected = true;
  }
  return text;
}

bool is_gradient(std::uint8_t type) {
  return type == kLinearFill || type == kRadialFill || type == kFocalFill;
}

const char* spread_name(std::uint8_t spread) {
  if (spread == 1) {
    return "reflect";
  }
  if (spread == 2) {
    return "repeat";
  }
  return "pad";
}

std::string matrix_text(const SwfMatrix& matrix) {
  return "matrix(" + number(matrix.scale_x / kFixedPointOne) + " " +
         number(matrix.rotate_skew0 / kFixedPointOne) + " " +
         number(matrix.rotate_skew1 / kFixedPointOne) + " " +
         number(matrix.scale_y / kFixedPointOne) + " " +
         number(matrix.translate_x / kTwipsPerPixel) + " " +
         number(matrix.translate_y / kTwipsPerPixel) + ")";
}

std::string focal_text(const SwfFillStyle& fill) {
  if (fill.type != kFocalFill) {
    return "";
  }
  const double focal = fill.focal_point.value_or(0) / kFocalPointOne;
  return " fx=\"" + number(focal * kGradientHalfPixels) + "\"";
}

std::string stop_text(const SwfGradientStop& stop) {
  return "<stop offset=\"" + number(stop.ratio / kRatioOne) + "\" stop-color=\"" +
         hex_color(stop.color) + "\" stop-opacity=\"" + alpha_text(stop.color) + "\"/>\n";
}

std::string gradient_text(const SwfFillStyle& fill, std::size_t index) {
  const bool linear = fill.type == kLinearFill;
  const std::string tag = linear ? "linearGradient" : "radialGradient";
  const std::string half = number(kGradientHalfPixels);
  const std::string geometry =
      linear ? " x1=\"-" + half + "\" y1=\"0\" x2=\"" + half + "\" y2=\"0\""
             : " cx=\"0\" cy=\"0\" r=\"" + half + "\"" + focal_text(fill);
  std::string text = "<" + tag + " id=\"fill" + std::to_string(index) + "\"" + geometry +
                     " gradientUnits=\"userSpaceOnUse\" gradientTransform=\"" +
                     matrix_text(fill.matrix.value_or(SwfMatrix{})) + "\" spreadMethod=\"" +
                     spread_name(fill.spread) + "\">\n";
  for (const auto& stop : fill.stops) {
    text += stop_text(stop);
  }
  return text + "</" + tag + ">\n";
}

std::string gradient_defs(const std::vector<SwfFillStyle>& fills) {
  std::string text;
  for (std::size_t index = 0; index < fills.size(); ++index) {
    if (is_gradient(fills[index].type)) {
      text += gradient_text(fills[index], index + 1);
    }
  }
  return text.empty() ? text : "<defs>\n" + text + "</defs>\n";
}

std::string fill_attributes(const SwfFillStyle& fill, std::size_t index) {
  if (fill.type == kSolidFill) {
    return paint("fill", fill.color);
  }
  if (is_gradient(fill.type)) {
    return "fill=\"url(#fill" + std::to_string(index) + ")\"";
  }
  return std::string("fill=\"") + kPlaceholderColor + "\"";
}

std::string path_element(const std::string& data, const std::string& attributes) {
  return "<path d=\"" + data + "\" " + attributes + "/>\n";
}

std::string fill_paths(const SwfShape& shape, const std::vector<Edge>& edges) {
  std::string text;
  for (std::size_t index = 0; index < shape.fill_styles.size(); ++index) {
    const std::string data = loops_text(fill_edges(edges, index + 1));
    if (!data.empty()) {
      text += path_element(data, fill_attributes(shape.fill_styles[index], index + 1) +
                                     " fill-rule=\"nonzero\"");
    }
  }
  return text;
}

std::string stroke_paint(const SwfLineStyle& line) {
  if (!line.fill) {
    return paint("stroke", line.color);
  }
  if (line.fill->type == kSolidFill) {
    return paint("stroke", line.fill->color);
  }
  return std::string("stroke=\"") + kPlaceholderColor + "\"";
}

unsigned join_style(std::uint16_t flags) {
  return (flags >> 12) & 0x03u;
}

const char* cap_name(std::uint16_t flags) {
  const unsigned style = (flags >> 14) & 0x03u;
  if (style == 1) {
    return "butt";
  }
  if (style == 2) {
    return "square";
  }
  return "round";
}

const char* join_name(std::uint16_t flags) {
  const unsigned style = join_style(flags);
  if (style == 1) {
    return "bevel";
  }
  if (style == kMiterJoin) {
    return "miter";
  }
  return "round";
}

std::string stroke_attributes(const SwfLineStyle& line) {
  const double width = line.width == 0 ? 1.0 : line.width / kTwipsPerPixel;
  std::string text = stroke_paint(line) + " fill=\"none\" stroke-width=\"" + number(width) +
                     "\" stroke-linecap=\"" + cap_name(line.flags) + "\" stroke-linejoin=\"" +
                     join_name(line.flags) + "\"";
  if (line.miter_limit && join_style(line.flags) == kMiterJoin) {
    text += " stroke-miterlimit=\"" + number(*line.miter_limit / kMiterLimitOne) + "\"";
  }
  return text;
}

std::string stroke_paths(const SwfShape& shape, const std::vector<Edge>& edges) {
  std::string text;
  for (std::size_t index = 0; index < shape.line_styles.size(); ++index) {
    const std::string data = stroke_text(edges, index + 1);
    if (!data.empty()) {
      text += path_element(data, stroke_attributes(shape.line_styles[index]));
    }
  }
  return text;
}

std::string svg_open(const SwfRect& bounds) {
  const std::int64_t width = static_cast<std::int64_t>(bounds.x_max) - bounds.x_min;
  const std::int64_t height = static_cast<std::int64_t>(bounds.y_max) - bounds.y_min;
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + pixels(bounds.x_min) + " " +
         pixels(bounds.y_min) + " " + pixels(width) + " " + pixels(height) + "\" width=\"" +
         pixels(width) + "\" height=\"" + pixels(height) + "\">\n";
}

}  // namespace

std::string shape_to_svg(const SwfShape& shape) {
  const auto edges = trace_edges(shape);
  return svg_open(shape.bounds) + gradient_defs(shape.fill_styles) +
         fill_paths(shape, edges) + stroke_paths(shape, edges) + "</svg>\n";
}

}  // namespace fo4::swf
