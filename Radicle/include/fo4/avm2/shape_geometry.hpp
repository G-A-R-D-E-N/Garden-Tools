#pragma once

#include <fo4/swf/swf.hpp>

#include <vector>

namespace fo4::avm2 {

struct ShapePoint {
  double x{0.0};
  double y{0.0};

  bool operator==(const ShapePoint&) const = default;
};

class DisplayObject;

/* A fill loop must close implicitly, so even-odd testing joins the last point to the first. */
struct ShapeGeometry {
  std::vector<std::vector<ShapePoint>> fills;

  bool operator==(const ShapeGeometry&) const = default;
};

/* Requires the divisor that converts edge units to pixels; the returned loops are in pixels. */
ShapeGeometry geometry_from_shape_records(const std::vector<swf::SwfShapeRecord>& records,
                                          double twips_per_pixel = 20.0);

/* Requires closed-loop reading; a point on no loop edge counts by the even-odd rule. */
bool shape_contains(const ShapeGeometry& geometry, double x, double y);

/* Requires the object's graphics value; a missing Graphics object yields no geometry. */
ShapeGeometry drawn_geometry_of(const DisplayObject& object);

/* Requires nothing; placed geometry wins over drawn geometry. */
ShapeGeometry effective_shape_geometry(const DisplayObject& object);

}  // namespace fo4::avm2
