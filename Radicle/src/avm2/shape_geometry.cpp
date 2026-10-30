#include <fo4/avm2/shape_geometry.hpp>

#include <cstddef>
#include <cstdint>

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
  std::vector<ShapePoint> current;
  double pen_x = 0.0;
  double pen_y = 0.0;
  const auto close_loop = [&] {
    if (!current.empty()) {
      geometry.fills.push_back(current);
      current.clear();
    }
  };
  const auto to_pixels = [&](std::int32_t twips) {
    return static_cast<double>(twips) / twips_per_pixel;
  };
  for (const swf::SwfShapeRecord& record : records) {
    switch (record.kind) {
      case swf::SwfShapeRecordKind::move:
        // A move must only lift the pen, so stroke-only paths leave no loop behind.
        close_loop();
        pen_x = to_pixels(record.x);
        pen_y = to_pixels(record.y);
        break;
      case swf::SwfShapeRecordKind::style_change:
        // A style change must break the loop, so the next fill starts its own boundary.
        close_loop();
        break;
      case swf::SwfShapeRecordKind::straight_edge:
      case swf::SwfShapeRecordKind::curved_edge: {
        const double end_x = to_pixels(record.x);
        const double end_y = to_pixels(record.y);
        if (record.has_fill0 || record.has_fill1) {
          if (current.empty()) {
            current.push_back({pen_x, pen_y});
          }
          if (record.kind == swf::SwfShapeRecordKind::straight_edge) {
            current.push_back({end_x, end_y});
          } else {
            flatten_quadratic(current, pen_x, pen_y, to_pixels(record.control_x),
                              to_pixels(record.control_y), end_x, end_y);
          }
        }
        pen_x = end_x;
        pen_y = end_y;
        break;
      }
    }
  }
  close_loop();
  return geometry;
}

bool shape_contains(const ShapeGeometry& geometry, double x, double y) {
  std::size_t crossings = 0;
  for (const std::vector<ShapePoint>& loop : geometry.fills) {
    if (loop.size() < 2) {
      continue;
    }
    for (std::size_t index = 0; index < loop.size(); ++index) {
      if (crosses_right(loop[index], loop[(index + 1) % loop.size()], x, y)) {
        ++crossings;
      }
    }
  }
  return (crossings % 2) == 1;
}

}  // namespace fo4::avm2
