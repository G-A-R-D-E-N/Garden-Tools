#include <fo4/avm2/shape_geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace fo4::avm2 {

namespace {

// A curve must flatten to segments, so hit testing never evaluates a Bezier.
constexpr std::size_t kCurveSegments = 8;

void flatten_quadratic(std::vector<ShapePoint>& out, double x0, double y0, double control_x,
                       double control_y, double x1, double y1) {
  for (std::size_t index = 1; index <= kCurveSegments; ++index) {
    const double t = static_cast<double>(index) / static_cast<double>(kCurveSegments);
    const double rest = 1.0 - t;
    out.push_back({rest * rest * x0 + 2.0 * rest * t * control_x + t * t * x1,
                   rest * rest * y0 + 2.0 * rest * t * control_y + t * t * y1});
  }
}

bool crosses_right(const ShapePoint& first, const ShapePoint& second, double x, double y) {
  if ((first.y > y) == (second.y > y)) {
    return false;
  }
  return first.x + (y - first.y) * (second.x - first.x) / (second.y - first.y) > x;
}

}  // namespace

ShapeGeometry geometry_from_shape_records(const std::vector<swf::SwfShapeRecord>& records,
                                          double twips_per_pixel) {
  ShapeGeometry geometry;
  // The pen must stay in edge units, because real edge records carry deltas from it.
  double pen_x = 0.0;
  double pen_y = 0.0;
  std::uint32_t fill0 = 0;
  std::uint32_t fill1 = 0;
  const auto to_pixels = [&](double twips) { return twips / twips_per_pixel; };
  for (const swf::SwfShapeRecord& record : records) {
    switch (record.kind) {
      case swf::SwfShapeRecordKind::move:
        // A move must only lift the pen, since the parser emits it absolute.
        pen_x = static_cast<double>(record.x);
        pen_y = static_cast<double>(record.y);
        break;
      case swf::SwfShapeRecordKind::style_change:
        // Fill state must ride the style record, because edges carry deltas and no fill flags.
        if (record.has_fill0 || record.fill0 != 0) {
          fill0 = record.fill0;
        }
        if (record.has_fill1 || record.fill1 != 0) {
          fill1 = record.fill1;
        }
        break;
      case swf::SwfShapeRecordKind::straight_edge:
      case swf::SwfShapeRecordKind::curved_edge: {
        const bool lineal = record.kind == swf::SwfShapeRecordKind::straight_edge;
        const double control_x = pen_x + static_cast<double>(record.control_x);
        const double control_y = pen_y + static_cast<double>(record.control_y);
        const double end_x =
            (lineal ? pen_x : control_x) + static_cast<double>(record.x);
        const double end_y =
            (lineal ? pen_y : control_y) + static_cast<double>(record.y);
        if (fill0 != 0 || fill1 != 0) {
          ShapeEdge edge;
          edge.fill0 = fill0;
          edge.fill1 = fill1;
          edge.points.push_back({to_pixels(pen_x), to_pixels(pen_y)});
          if (lineal) {
            edge.points.push_back({to_pixels(end_x), to_pixels(end_y)});
          } else {
            flatten_quadratic(edge.points, to_pixels(pen_x), to_pixels(pen_y),
                              to_pixels(control_x), to_pixels(control_y), to_pixels(end_x),
                              to_pixels(end_y));
          }
          geometry.edges.push_back(std::move(edge));
        }
        pen_x = end_x;
        pen_y = end_y;
        break;
      }
    }
  }
  return geometry;
}

bool shape_contains(const ShapeGeometry& geometry, double x, double y) {
  // Crossings must count per fill style, so two styles over one point still hit.
  std::map<std::uint32_t, std::size_t> crossings;
  for (const ShapeEdge& edge : geometry.edges) {
    if (edge.fill0 == 0 && edge.fill1 == 0) {
      continue;
    }
    // An edge with one style on both sides borders a single region, so it must not count.
    if (edge.fill0 != 0 && edge.fill0 == edge.fill1) {
      continue;
    }
    // Fragments must stay open, so each raw segment counts once with no closing chord.
    for (std::size_t index = 0; index + 1 < edge.points.size(); ++index) {
      const ShapePoint& first = edge.points[index];
      const ShapePoint& second = edge.points[index + 1];
      if (crosses_right(first, second, x, y)) {
        if (edge.fill0 != 0) {
          ++crossings[edge.fill0];
        }
        if (edge.fill1 != 0) {
          ++crossings[edge.fill1];
        }
      }
    }
  }
  for (const auto& [style, count] : crossings) {
    if ((count % 2) == 1) {
      return true;
    }
  }
  return false;
}

}  // namespace fo4::avm2
