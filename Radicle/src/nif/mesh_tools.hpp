#pragma once

#include "fo4/nif/shape_ops.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif::mesh_detail {

using shape_detail::Layout;
using shape_detail::Vec3;
using write_detail::Bytes;

// Invariant: a vertex is its whole stride of raw bytes, so skin weights, colour and tail slots survive every op.
using Vertex = Bytes;

struct Geometry {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> triangles;
};

struct Delta {
  std::size_t vertices_before{0};
  std::size_t vertices_after{0};
  std::size_t triangles_before{0};
  std::size_t triangles_after{0};
  float max_error{0.0f};
  std::size_t vertices_changed{0};
  float max_weight_change{0.0f};
};

// Requires a tri-shape block that read_shape accepted; returns the refusal reason when the block stays unchanged.
std::optional<std::string> apply_mesh_op(write_detail::Model& model, std::uint32_t block, ShapeOp op,
                                         const Layout& layout, float ratio, Delta* delta);

// Invariant: sanitize only rewrites a normal or tangent that cannot be a stored unit vector, so both counts stay.
std::optional<std::string> sanitize_normals(Bytes& bytes, const Shape& shape, const Layout& layout);

}  // namespace fo4::nif::mesh_detail
