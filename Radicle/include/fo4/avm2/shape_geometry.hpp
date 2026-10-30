#pragma once

#include <fo4/swf/swf.hpp>

#include <cstdint>
#include <vector>

namespace fo4::avm2 {

struct ShapePoint {
  double x{0.0};
  double y{0.0};

  bool operator==(const ShapePoint&) const = default;
};

class DisplayObject;

/* A border fragment must stay open, so even-odd testing counts each raw segment once. */
struct ShapeEdge {
  std::vector<ShapePoint> points;
  std::uint32_t fill0{0};
  std::uint32_t fill1{0};

  bool operator==(const ShapeEdge&) const = default;
};

/* Raw border fragments must stay unlooped, because real edges border fill0 and fill1 without forming loops. */
struct ShapeGeometry {
  std::vector<ShapeEdge> edges;

  bool operator==(const ShapeGeometry&) const = default;
};

/* Requires the divisor that converts edge units to pixels; the returned edges are in pixels. */
ShapeGeometry geometry_from_shape_records(const std::vector<swf::SwfShapeRecord>& records,
                                          double twips_per_pixel = 20.0);

/* Requires open fragments; crossings count over raw segments, and a point with an odd count for any fill style hits. */
bool shape_contains(const ShapeGeometry& geometry, double x, double y);

/* Requires the object's graphics value; a missing Graphics object yields no geometry. */
ShapeGeometry drawn_geometry_of(const DisplayObject& object);

/* Requires nothing; placed geometry wins over drawn geometry. */
ShapeGeometry effective_shape_geometry(const DisplayObject& object);

}  // namespace fo4::avm2
