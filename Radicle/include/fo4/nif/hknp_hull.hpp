#pragma once

#include "fo4/nif/hknp_decode.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::nif {

using HknpTriangle = std::array<std::uint32_t, 3>;

// Constraint: a hull refused for its size or work raises this, so a caller can tell it from a mesh that forms no solid.
class hull_limit_error : public std::invalid_argument {
public:
  explicit hull_limit_error(const std::string& reason) : std::invalid_argument(reason) {}
};

// Requires four non-coplanar points and at most 252 hull vertices; anything else raises std::invalid_argument, or hull_limit_error for a size or work refusal.
// Constraint: only referenced points are used, indices must be in range, and every point must be finite.
// Invariant: the returned shape is a closed convex polytope whose faces wind outward.
// Constraint: vertex and plane counts pad to four, and each face carries its quantized bend.
// Invariant: mass properties are the solid's, with inertia stored at 1.5 times the physical tensor.
// Invariant: inertia_orientation, when given, receives the (x, y, z, w) rotation that takes the stored inertia axes onto the body axes.
HknpShape make_hknp_hull(const std::vector<HknpVec3>& points, const std::vector<HknpTriangle>& triangles,
                         std::uint32_t material_crc, HknpVec4* inertia_orientation = nullptr);

}  // namespace fo4::nif
