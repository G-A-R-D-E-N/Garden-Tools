#include "fo4/nif/shape_ops.hpp"

#include "import_mesh.hpp"
#include "mesh_tools.hpp"
#include "shape_ops_internal.hpp"
#include "skin_weights.hpp"
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

using mesh_detail::Delta;
using shape_detail::Layout;
using shape_detail::Vec3;
using shape_detail::dot;
using shape_detail::linear;
using shape_detail::place;
using shape_detail::scale;
using shape_detail::unit;
using write_detail::Bytes;
using write_detail::Model;

constexpr std::array<std::pair<std::string_view, ShapeOp>, 15> k_op_names{{
    {"recalc_normals", ShapeOp::recalc_normals},
    {"recalc_tangents", ShapeOp::recalc_tangents},
    {"flip_normals", ShapeOp::flip_normals},
    {"flip_winding", ShapeOp::flip_winding},
    {"apply_transform", ShapeOp::apply_transform},
    {"refresh_bounds", ShapeOp::refresh_bounds},
    {"weld_vertices", ShapeOp::weld_vertices},
    {"remove_degenerate_triangles", ShapeOp::remove_degenerate_triangles},
    {"remove_unused_vertices", ShapeOp::remove_unused_vertices},
    {"simplify", ShapeOp::simplify},
    {"sanitize", ShapeOp::sanitize},
    {"normalize_weights", ShapeOp::normalize_weights},
    {"limit_influences", ShapeOp::limit_influences},
    {"prune_weights", ShapeOp::prune_weights},
    {"copy_weights", ShapeOp::copy_weights},
}};
constexpr std::string_view k_dynamic_type = "BSDynamicTriShape";
constexpr float k_max_crease_degrees = 180.0f;
constexpr std::uint32_t k_max_influences = 4;

using Reason = std::optional<std::string>;

std::string skipped_text(std::uint32_t block, std::string_view reason) {
  return "block " + std::to_string(block) + ": " + std::string(reason);
}

float determinant(const Transform& xf) {
  const auto& r = xf.r;
  return r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
         r[2] * (r[3] * r[7] - r[4] * r[6]);
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

Reason apply_to_block(const NifFile& nif, Model& model, const ShapeOpRequest& request, std::uint32_t block,
                      Delta* delta) {
  const auto shape = read_shape(nif, block);
  if (shape.verts.empty()) return "no vertex data";
  const auto layout = shape_detail::read_layout(nif, block);
  if (is_mesh_shape_op(request.op)) {
    return mesh_detail::apply_mesh_op(model, block, request.op, layout, request.ratio, delta);
  }
  auto& bytes = model.blocks[block];
  Reason reason;
  switch (request.op) {
    case ShapeOp::recalc_normals:
      reason = recalc_normals_block(shape, layout, request.crease_degrees, bytes);
      break;
    case ShapeOp::recalc_tangents:
      reason = recalc_tangents_block(shape, layout, bytes);
      break;
    case ShapeOp::flip_normals:
      reason = flip_normals_block(shape, layout, bytes);
      break;
    case ShapeOp::flip_winding:
      reason = flip_winding_block(shape, layout, bytes);
      break;
    case ShapeOp::apply_transform:
      reason = apply_transform_block(nif, block, shape, layout, bytes);
      break;
    case ShapeOp::refresh_bounds:
      reason = refresh_bounds_block(shape, layout, bytes);
      break;
    case ShapeOp::sanitize:
      reason = mesh_detail::sanitize_normals(bytes, shape, layout);
      break;
    case ShapeOp::normalize_weights:
    case ShapeOp::limit_influences:
    case ShapeOp::prune_weights:
    case ShapeOp::copy_weights:
      reason = skin_detail::apply_skin_op(nif, request, block, shape, layout, bytes, delta);
      break;
    default:
      reason = "unknown operation";
      break;
  }
  // Invariant: an attribute op keeps both counts, so the report carries one count on both sides.
  if (!reason && delta != nullptr) {
    delta->vertices_before = shape.verts.size() / 3;
    delta->vertices_after = delta->vertices_before;
    delta->triangles_before = shape.tris.size() / 3;
    delta->triangles_after = delta->triangles_before;
  }
  return reason;
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

void require_round_trip(const NifFile& nif, const std::vector<std::byte>& bytes, const ShapeOpRequest& request,
                        const ShapeOpReport& report) {
  const auto before = read_shapes(nif);
  const auto after = read_shapes(read_nif(bytes));
  if (before.size() != after.size()) {
    throw std::runtime_error("internal error: the edited NIF has a different shape count");
  }
  for (std::size_t i = 0; i < before.size(); ++i) {
    const bool changed =
        std::find(report.changed.begin(), report.changed.end(), before[i].block) != report.changed.end();
    // Invariant: only a mesh op may change a count, so every other shape keeps both counts exactly.
    if (changed && is_mesh_shape_op(request.op)) continue;
    if (after[i].verts.size() != before[i].verts.size() || after[i].tris.size() != before[i].tris.size()) {
      throw std::runtime_error("internal error: a shape kept its counts but reads back differently");
    }
  }
}

}  // namespace

bool is_mesh_shape_op(ShapeOp op) noexcept {
  return op == ShapeOp::weld_vertices || op == ShapeOp::remove_degenerate_triangles ||
         op == ShapeOp::remove_unused_vertices || op == ShapeOp::simplify;
}

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
  if (request.op == ShapeOp::simplify && !(request.ratio > 0.0f && request.ratio < 1.0f)) {
    throw std::invalid_argument("ratio must be above 0 and below 1");
  }
  if (request.op == ShapeOp::limit_influences &&
      (request.max_influences < 1 || request.max_influences > k_max_influences)) {
    throw std::invalid_argument("max_influences must be between 1 and 4");
  }
  if (request.op == ShapeOp::prune_weights && !(request.prune_threshold >= 0.0f && request.prune_threshold < 1.0f)) {
    throw std::invalid_argument("prune_threshold must be at least 0 and below 1");
  }
  if (request.op == ShapeOp::copy_weights) {
    if (!request.source_block) throw std::invalid_argument("copy_weights needs a source_block");
    const auto source = *request.source_block;
    if (source >= nif.blocks().size() || !is_shape_block_type(nif.blocks()[source].type)) {
      throw std::invalid_argument("source_block must be a tri-shape block");
    }
  }
  auto model = write_detail::load_model(nif);
  ShapeOpReport result;
  for (const auto block : target_blocks(nif, request.blocks)) {
    if (block >= nif.blocks().size()) {
      result.skipped.push_back(skipped_text(block, "no such block"));
    } else if (!is_shape_block_type(nif.blocks()[block].type)) {
      result.skipped.push_back(skipped_text(block, "not a tri-shape"));
    } else {
      Delta delta;
      if (const auto reason = apply_to_block(nif, model, request, block, &delta)) {
        result.skipped.push_back(skipped_text(block, *reason));
      } else {
        result.changed.push_back(block);
        result.vertices_before += delta.vertices_before;
        result.vertices_after += delta.vertices_after;
        result.triangles_before += delta.triangles_before;
        result.triangles_after += delta.triangles_after;
        result.max_error = std::max(result.max_error, delta.max_error);
        result.vertices_changed += delta.vertices_changed;
        result.max_weight_change = std::max(result.max_weight_change, delta.max_weight_change);
      }
    }
  }
  auto bytes = write_detail::serialize(model);
  require_round_trip(nif, bytes, request, result);
  if (report != nullptr) *report = std::move(result);
  return bytes;
}

}  // namespace fo4::nif
