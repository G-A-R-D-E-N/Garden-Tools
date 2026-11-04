#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

enum class ShapeOp : std::uint8_t {
  recalc_normals,
  recalc_tangents,
  flip_normals,
  flip_winding,
  apply_transform,
  refresh_bounds,
};

struct ShapeOpRequest {
  ShapeOp op{ShapeOp::recalc_normals};
  // Invariant: an empty block list selects every shape.
  std::vector<std::uint32_t> blocks;
  // Requires 0 to 180; only recalc_normals reads it.
  float crease_degrees{180.0f};
};

struct ShapeOpReport {
  std::vector<std::uint32_t> changed;
  // Invariant: each entry reads "block N: reason".
  std::vector<std::string> skipped;
};

std::optional<ShapeOp> parse_shape_op(std::string_view name);
const char* shape_op_name(ShapeOp op) noexcept;

// Invariant: the vertex and triangle counts never change, so every reference in the file stays valid.
std::vector<std::byte> apply_shape_op(const NifFile& nif, const ShapeOpRequest& request,
                                      ShapeOpReport* report = nullptr);

}  // namespace fo4::nif
