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
  weld_vertices,
  remove_degenerate_triangles,
  remove_unused_vertices,
  simplify,
  sanitize,
  normalize_weights,
  limit_influences,
  prune_weights,
  copy_weights,
};

struct ShapeOpRequest {
  ShapeOp op{ShapeOp::recalc_normals};
  // Invariant: an empty block list selects every shape.
  std::vector<std::uint32_t> blocks;
  // Requires 0 to 180; only recalc_normals reads it.
  float crease_degrees{180.0f};
  // Requires above 0 and below 1; only simplify reads it.
  float ratio{0.5f};
  // Requires 1 to 4; only limit_influences reads it.
  std::uint32_t max_influences{4};
  // Requires at least 0 and below 1; only prune_weights reads it.
  float prune_threshold{0.0f};
  // Requires a tri-shape block; only copy_weights reads it.
  std::optional<std::uint32_t> source_block;
};

struct ShapeOpReport {
  std::vector<std::uint32_t> changed;
  // Invariant: each entry reads "block N: reason".
  std::vector<std::string> skipped;
  // Invariant: these four sums cover only the changed blocks, so a refusal leaves them at zero.
  std::size_t vertices_before{0};
  std::size_t vertices_after{0};
  std::size_t triangles_before{0};
  std::size_t triangles_after{0};
  // Invariant: simplify only, in NIF units: the largest distance any vertex moved.
  float max_error{0.0f};
  // Invariant: skin weight ops only, the number of vertices whose weight or bone bytes were rewritten.
  std::size_t vertices_changed{0};
  // Invariant: skin weight ops only, the largest absolute weight change before half-precision rounding.
  float max_weight_change{0.0f};
};

// Invariant: only these four ops may change the vertex and triangle counts.
bool is_mesh_shape_op(ShapeOp op) noexcept;

std::optional<ShapeOp> parse_shape_op(std::string_view name);
const char* shape_op_name(ShapeOp op) noexcept;

// Invariant: the attribute ops keep both counts, so every index in the file stays valid; a mesh op re-reads its shape.
std::vector<std::byte> apply_shape_op(const NifFile& nif, const ShapeOpRequest& request,
                                      ShapeOpReport* report = nullptr);

}  // namespace fo4::nif
