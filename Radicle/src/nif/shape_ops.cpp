#include "fo4/nif/shape_ops.hpp"

#include "import_mesh.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using shape_detail::Layout;
using shape_detail::Vec3;
using shape_detail::dot;
using shape_detail::scale;
using shape_detail::unit;
using write_detail::Bytes;
using write_detail::Model;

constexpr std::array<std::pair<std::string_view, ShapeOp>, 6> k_op_names{{
    {"recalc_normals", ShapeOp::recalc_normals},
    {"recalc_tangents", ShapeOp::recalc_tangents},
    {"flip_normals", ShapeOp::flip_normals},
    {"flip_winding", ShapeOp::flip_winding},
    {"apply_transform", ShapeOp::apply_transform},
    {"refresh_bounds", ShapeOp::refresh_bounds},
}};
constexpr std::string_view k_dynamic_type = "BSDynamicTriShape";
constexpr float k_max_crease_degrees = 180.0f;

using Reason = std::optional<std::string>;

std::string skipped_text(std::uint32_t block, std::string_view reason) {
  return "block " + std::to_string(block) + ": " + std::string(reason);
}

float determinant(const Transform& xf) {
  const auto& r = xf.r;
  return r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
         r[2] * (r[3] * r[7] - r[4] * r[6]);
}

Vec3 linear(const Transform& xf, const Vec3& v) {
  return {xf.r[0] * v[0] + xf.r[1] * v[1] + xf.r[2] * v[2], xf.r[3] * v[0] + xf.r[4] * v[1] + xf.r[5] * v[2],
          xf.r[6] * v[0] + xf.r[7] * v[1] + xf.r[8] * v[2]};
}

Vec3 place(const Transform& xf, const Vec3& p) {
  const auto moved = linear(xf, p);
  return {xf.s * moved[0] + xf.t[0], xf.s * moved[1] + xf.t[1], xf.s * moved[2] + xf.t[2]};
}

Vec3 direction(const Transform& xf, const Vec3& v) {
  const auto turned = linear(xf, v);
  return dot(turned, turned) > 0.0f ? unit(turned) : turned;
}

void negate(std::vector<Vec3>& vectors) {
  for (auto& v : vectors) v = scale(v, -1.0f);
}

Reason recalc_normals_block(const Shape& shape, const Layout& layout, float crease_degrees, Bytes& bytes) {
  if (shape.normals.empty()) return "no normals stored";
  if (shape.tris.empty()) return "no triangles";
  auto mesh = shape_detail::shape_mesh(shape);
  auto frame = shape_detail::read_frame(shape, layout);
  frame.normals = shape_detail::smooth_normals(mesh, crease_degrees);
  if (layout.has_tangent && layout.has_uv) {
    mesh.normals = frame.normals;
    import_detail::add_tangent_frame(mesh);
    frame.tangents = mesh.tangents;
    frame.bitangents = mesh.bitangents;
  }
  shape_detail::write_frame(bytes, layout, frame);
  return std::nullopt;
}

Reason recalc_tangents_block(const Shape& shape, const Layout& layout, Bytes& bytes) {
  if (shape.normals.empty()) return "no normals stored";
  if (!layout.has_tangent) return "no tangents stored";
  if (!layout.has_uv) return "no UVs to take the tangent frame from";
  if (shape.tris.empty()) return "no triangles";
  auto mesh = shape_detail::shape_mesh(shape);
  import_detail::add_tangent_frame(mesh);
  auto frame = shape_detail::read_frame(shape, layout);
  frame.tangents = mesh.tangents;
  frame.bitangents = mesh.bitangents;
  shape_detail::write_frame(bytes, layout, frame);
  return std::nullopt;
}

Reason flip_normals_block(const Shape& shape, const Layout& layout, Bytes& bytes) {
  if (shape.normals.empty()) return "no normals stored";
  auto frame = shape_detail::read_frame(shape, layout);
  negate(frame.normals);
  negate(frame.tangents);
  negate(frame.bitangents);
  shape_detail::write_frame(bytes, layout, frame);
  return std::nullopt;
}

Reason flip_winding_block(const Shape& shape, const Layout& layout, Bytes& bytes) {
  if (shape.tris.empty()) return "no triangles";
  shape_detail::swap_winding(bytes, layout);
  return std::nullopt;
}

Reason apply_transform_block(const NifFile& nif, std::uint32_t block, const Shape& shape, const Layout& layout,
                             Bytes& bytes) {
  if (layout.skinned || shape.skin_ref >= 0) return "skinned, so its vertices are in skin space";
  if (!layout.full) return "half-precision positions have no exact bake";
  if (nif.blocks()[block].type == k_dynamic_type) {
    return "BSDynamicTriShape keeps dynamic vertices after its triangles, which are not baked";
  }
  const auto xf = block_transform(nif, block);
  if (!(xf.s > 0.0f) || determinant(xf) <= 0.0f) return "the transform mirrors or flattens the shape";
  auto positions = shape_detail::to_vec3(shape.verts);
  for (auto& p : positions) p = place(xf, p);
  auto frame = shape_detail::read_frame(shape, layout);
  for (auto& n : frame.normals) n = direction(xf, n);
  for (auto& t : frame.tangents) t = direction(xf, t);
  for (auto& b : frame.bitangents) b = direction(xf, b);
  shape_detail::write_positions(bytes, layout, positions);
  shape_detail::write_frame(bytes, layout, frame);
  shape_detail::write_sphere(bytes, layout, import_detail::bounding_sphere(positions));
  shape_detail::write_identity_transform(bytes, layout);
  return std::nullopt;
}

Reason refresh_bounds_block(const Shape& shape, const Layout& layout, Bytes& bytes) {
  shape_detail::write_sphere(bytes, layout, import_detail::bounding_sphere(shape_detail::to_vec3(shape.verts)));
  return std::nullopt;
}

Reason apply_to_block(const NifFile& nif, Model& model, const ShapeOpRequest& request, std::uint32_t block) {
  const auto shape = read_shape(nif, block);
  if (shape.verts.empty()) return "no vertex data";
  const auto layout = shape_detail::read_layout(nif, block);
  auto& bytes = model.blocks[block];
  switch (request.op) {
    case ShapeOp::recalc_normals:
      return recalc_normals_block(shape, layout, request.crease_degrees, bytes);
    case ShapeOp::recalc_tangents:
      return recalc_tangents_block(shape, layout, bytes);
    case ShapeOp::flip_normals:
      return flip_normals_block(shape, layout, bytes);
    case ShapeOp::flip_winding:
      return flip_winding_block(shape, layout, bytes);
    case ShapeOp::apply_transform:
      return apply_transform_block(nif, block, shape, layout, bytes);
    case ShapeOp::refresh_bounds:
      return refresh_bounds_block(shape, layout, bytes);
  }
  return "unknown operation";
}

std::vector<std::uint32_t> target_blocks(const NifFile& nif, std::vector<std::uint32_t> requested) {
  if (requested.empty()) {
    for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
      if (is_shape_block_type(nif.blocks()[block].type)) requested.push_back(block);
    }
    return requested;
  }
  std::sort(requested.begin(), requested.end());
  requested.erase(std::unique(requested.begin(), requested.end()), requested.end());
  return requested;
}

void require_same_shapes(const NifFile& nif, const Bytes& bytes) {
  const auto before = read_shapes(nif);
  const auto after = read_shapes(read_nif(bytes));
  if (before.size() != after.size()) {
    throw std::runtime_error("internal error: the edited NIF has a different shape count");
  }
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (after[i].verts.size() != before[i].verts.size() || after[i].tris.size() != before[i].tris.size()) {
      throw std::runtime_error("internal error: the edited NIF does not read back as the same shapes");
    }
  }
}

}  // namespace

std::optional<ShapeOp> parse_shape_op(std::string_view name) {
  for (const auto& [text, op] : k_op_names) {
    if (text == name) return op;
  }
  return std::nullopt;
}

const char* shape_op_name(ShapeOp op) noexcept {
  for (const auto& [text, value] : k_op_names) {
    if (value == op) return text.data();
  }
  return "";
}

std::vector<std::byte> apply_shape_op(const NifFile& nif, const ShapeOpRequest& request, ShapeOpReport* report) {
  if (!(request.crease_degrees >= 0.0f && request.crease_degrees <= k_max_crease_degrees)) {
    throw std::invalid_argument("crease_degrees must be between 0 and 180");
  }
  auto model = write_detail::load_model(nif);
  ShapeOpReport result;
  for (const auto block : target_blocks(nif, request.blocks)) {
    if (block >= nif.blocks().size()) {
      result.skipped.push_back(skipped_text(block, "no such block"));
    } else if (!is_shape_block_type(nif.blocks()[block].type)) {
      result.skipped.push_back(skipped_text(block, "not a tri-shape"));
    } else if (const auto reason = apply_to_block(nif, model, request, block)) {
      result.skipped.push_back(skipped_text(block, *reason));
    } else {
      result.changed.push_back(block);
    }
  }
  auto bytes = write_detail::serialize(model);
  require_same_shapes(nif, bytes);
  if (report != nullptr) *report = std::move(result);
  return bytes;
}

}  // namespace fo4::nif
