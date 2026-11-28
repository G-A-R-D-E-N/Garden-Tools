#include "fo4/nif/shape_model_ops.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/customization_remap.hpp"
#include "fo4/nif/model_ops.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "import_mesh.hpp"
#include "nif_internal.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

using import_detail::Influence;
using import_detail::Mesh;
using import_detail::Vec2;
using import_detail::Vec4;
using shape_detail::Layout;
using write_detail::Bytes;
using write_detail::append_int;
using write_detail::half_bits;
using write_detail::unit_byte;
using write_detail::write_int;

constexpr std::uint32_t k_has_position = 1U;
constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr std::uint32_t k_supported_flags = k_has_position | k_has_uv | k_has_normal | k_has_tangent |
                                            k_has_colour | k_has_skin | k_full_precision;
constexpr std::size_t k_max_vertices = 65535;
constexpr float k_half_max = 65504.0f;
constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_influences = 4;
constexpr std::string_view k_plain_type = "BSTriShape";
constexpr std::string_view k_skin_type = "BSSkin::Instance";

constexpr std::array<const char*, 5> k_op_names{"extrude_faces", "bridge_loops", "knife_plane", "cut_loop",
                                                "bevel_edges"};

[[noreturn]] void refuse(ModelOp op, std::uint32_t block, const std::string& reason) {
  throw std::invalid_argument(std::string(model_op_name(op)) + ": block " + std::to_string(block) + " " + reason);
}

// Requires the shape prologue read_shape decodes: node header, the unused i32, the 16-byte sphere, then the skin reference.
std::int32_t skin_ref_of(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_av_header(cursor, nif);
  cursor.read_i32();
  cursor.read_bytes(16);
  return cursor.read_i32();
}

Mesh load_mesh(const Shape& shape) {
  Mesh mesh;
  mesh.positions = shape_detail::to_vec3(shape.verts);
  mesh.normals = shape_detail::to_vec3(shape.normals);
  mesh.tangents = shape_detail::to_vec3(shape.tangents);
  mesh.bitangents = shape_detail::to_vec3(shape.bitangents);
  for (std::size_t i = 0; i + 1 < shape.uvs.size(); i += 2) mesh.uvs.push_back(Vec2{shape.uvs[i], shape.uvs[i + 1]});
  for (std::size_t i = 0; i + 3 < shape.colors.size(); i += 4) {
    mesh.colours.push_back(Vec4{shape.colors[i], shape.colors[i + 1], shape.colors[i + 2], shape.colors[i + 3]});
  }
  for (std::size_t i = 0; i + 3 < shape.weights.size(); i += 4) {
    mesh.weights.push_back(Vec4{shape.weights[i], shape.weights[i + 1], shape.weights[i + 2], shape.weights[i + 3]});
  }
  for (std::size_t i = 0; i + 3 < shape.bone_indices.size(); i += 4) {
    mesh.bones.push_back(Influence{shape.bone_indices[i], shape.bone_indices[i + 1], shape.bone_indices[i + 2],
                                   shape.bone_indices[i + 3]});
  }
  mesh.tris.assign(shape.tris.begin(), shape.tris.end());
  return mesh;
}

void put_scalar(Bytes& out, float value, bool full) {
  if (full) {
    append_int(out, std::bit_cast<std::uint32_t>(value));
  } else {
    append_int(out, half_bits(value));
  }
}

std::uint8_t colour_byte(float value) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Invariant: the vertex field order is the one read_shape decodes, so a layout it reads is a layout this writes.
Bytes encode_vertices(const Mesh& mesh, std::uint32_t flags) {
  const bool full = (flags & k_full_precision) != 0;
  const bool frame = (flags & k_has_normal) != 0 && (flags & k_has_tangent) != 0;
  Bytes out;
  for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
    for (const float value : mesh.positions[v]) {
      // Constraint: a position must be finite and, in half precision, within the largest finite half.
      if (!std::isfinite(value) || (!full && std::fabs(value) > k_half_max)) {
        throw std::invalid_argument("vertex " + std::to_string(v) + " has a position the vertex precision cannot hold");
      }
      put_scalar(out, value, full);
    }
    put_scalar(out, frame ? mesh.bitangents[v][0] : 0.0f, full);
    if ((flags & k_has_uv) != 0) {
      append_int(out, half_bits(mesh.uvs[v][0]));
      append_int(out, half_bits(mesh.uvs[v][1]));
    }
    if ((flags & k_has_normal) != 0) {
      for (const float value : mesh.normals[v]) out.push_back(std::byte{unit_byte(value)});
      out.push_back(std::byte{frame ? unit_byte(mesh.bitangents[v][1]) : std::uint8_t{0}});
    }
    if ((flags & k_has_tangent) != 0) {
      for (const float value : mesh.tangents[v]) out.push_back(std::byte{unit_byte(value)});
      out.push_back(std::byte{frame ? unit_byte(mesh.bitangents[v][2]) : std::uint8_t{0}});
    }
    if ((flags & k_has_colour) != 0) {
      for (const float channel : mesh.colours[v]) out.push_back(std::byte{colour_byte(channel)});
    }
    if ((flags & k_has_skin) != 0) {
      for (const float weight : mesh.weights[v]) append_int(out, half_bits(weight));
      for (const auto index : mesh.bones[v]) out.push_back(std::byte{index});
    }
  }
  return out;
}

bool channels_match(const Mesh& mesh, std::uint32_t flags) {
  const std::size_t n = mesh.positions.size();
  const bool frame = (flags & k_has_normal) != 0 && (flags & k_has_tangent) != 0;
  return mesh.normals.size() == ((flags & k_has_normal) != 0 ? n : 0U) &&
         mesh.tangents.size() == ((flags & k_has_tangent) != 0 ? n : 0U) &&
         mesh.bitangents.size() == (frame ? n : 0U) && mesh.uvs.size() == ((flags & k_has_uv) != 0 ? n : 0U) &&
         mesh.colours.size() == ((flags & k_has_colour) != 0 ? n : 0U) &&
         mesh.weights.size() == ((flags & k_has_skin) != 0 ? n : 0U) &&
         mesh.bones.size() == ((flags & k_has_skin) != 0 ? n : 0U);
}

// Invariant: everything before the first vertex and after the old triangle list is copied, so only the counts change there.
Bytes rebuild_block(const Bytes& original, const Layout& layout, std::uint32_t flags, const Mesh& mesh) {
  if (!channels_match(mesh, flags)) throw std::runtime_error("internal error: the mesh channels lost their counts");
  for (const auto index : mesh.tris) {
    if (index >= mesh.positions.size()) throw std::runtime_error("internal error: a triangle names a missing vertex");
  }
  const auto vertices = encode_vertices(mesh, flags);
  if (vertices.size() != mesh.positions.size() * layout.stride) {
    throw std::runtime_error("internal error: the encoded vertex size does not match the stride");
  }
  Bytes out(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at));
  out.insert(out.end(), vertices.begin(), vertices.end());
  for (const auto index : mesh.tris) append_int(out, static_cast<std::uint16_t>(index));
  const auto tail_at = layout.triangles_at + layout.triangle_count * k_triangle_bytes;
  out.insert(out.end(), original.begin() + static_cast<std::ptrdiff_t>(tail_at), original.end());
  // Invariant: the three count fields sit at fixed negative offsets from the vertex data.
  write_int(out, layout.vertices_at - 10, static_cast<std::uint32_t>(mesh.tris.size() / 3));
  write_int(out, layout.vertices_at - 6, static_cast<std::uint16_t>(mesh.positions.size()));
  write_int(out, layout.vertices_at - 4,
            static_cast<std::uint32_t>(vertices.size() + mesh.tris.size() * 2));
  return out;
}

}  // namespace

std::optional<ModelOp> parse_model_op(std::string_view name) {
  for (std::size_t i = 0; i < k_op_names.size(); ++i) {
    if (name == k_op_names[i]) return static_cast<ModelOp>(i);
  }
  return std::nullopt;
}

const char* model_op_name(ModelOp op) noexcept { return k_op_names[static_cast<std::size_t>(op)]; }

std::vector<std::byte> apply_model_op(const NifFile& nif, const ModelOpRequest& request, ModelOpReport* report) {
  const auto op = request.op;
  const auto block = request.block;
  if (block >= nif.blocks().size()) {
    throw std::invalid_argument(std::string(model_op_name(op)) + " names a block past the file");
  }
  if (nif.blocks()[block].type != k_plain_type) {
    refuse(op, block, "is " + nif.blocks()[block].type + "; only BSTriShape is supported");
  }
  std::int32_t owned_remap = -1;
  try {
    owned_remap = read_customization_remap(nif, block).remap_block;
  } catch (const parse_error& error) {
    refuse(op, block, std::string("has CustomizationRemapData that cannot be read: ") + error.what());
  }
  if (owned_remap >= 0) {
    refuse(op, block, "owns CustomizationRemapData, and a vertex the op creates has no source record to take");
  }
  const auto desc = read_vertex_desc(nif, block);
  const auto flags = desc.flags();
  if ((flags & k_has_uv2) != 0) refuse(op, block, "has a second UV set, which the mesh cannot carry");
  if ((flags & ~k_supported_flags) != 0) refuse(op, block, "has vertex attributes beyond UV, normal, tangent, colour and skin");
  if (((flags & k_has_normal) != 0) != ((flags & k_has_tangent) != 0)) {
    refuse(op, block, "has a normal without a tangent or the reverse, so its bitangent would be lost");
  }
  if (const auto fault = vertex_layout_fault(desc); !fault.empty()) refuse(op, block, fault);
  const auto skin_ref = skin_ref_of(nif, block);
  if (skin_ref >= 0) {
    if (static_cast<std::size_t>(skin_ref) >= nif.blocks().size()) refuse(op, block, "names a skin past the file");
    const auto& skin_type = nif.blocks()[static_cast<std::size_t>(skin_ref)].type;
    if (skin_type != k_skin_type) {
      refuse(op, block, "uses " + skin_type + ", whose triangle partitions the new triangle list would leave stale");
    }
  }

  const auto shape = read_shape(nif, block);
  if (shape.verts.empty()) refuse(op, block, "has no vertex data");
  if (shape.tris.empty()) refuse(op, block, "has no triangles");
  const std::size_t vertex_count = shape.verts.size() / 3;
  if ((flags & k_has_skin) != 0 &&
      (shape.weights.size() != vertex_count * k_influences || shape.bone_indices.size() != vertex_count * k_influences)) {
    refuse(op, block, "has skin weights that did not decode for every vertex");
  }
  const auto layout = shape_detail::read_layout(nif, block);
  auto model = write_detail::load_model(nif);
  const Bytes original = model.blocks[block];
  auto mesh = load_mesh(shape);
  if (rebuild_block(original, layout, flags, mesh) != original) {
    refuse(op, block, "would change on rewrite, so it is left alone");
  }

  std::optional<std::string> reason;
  model_detail::KnifeResult knife;
  switch (op) {
    case ModelOp::extrude_faces:
      reason = model_detail::extrude_faces(mesh, request.tris, request.distance);
      break;
    case ModelOp::bridge_loops:
      reason = model_detail::bridge_loops(mesh, request.loop_a, request.loop_b);
      break;
    case ModelOp::knife_plane:
      reason = model_detail::knife_plane(mesh, request.normal, request.offset, &knife);
      break;
    case ModelOp::cut_loop:
      reason = model_detail::cut_loop(mesh, request.edges);
      break;
    case ModelOp::bevel_edges:
      reason = model_detail::bevel_edges(mesh, request.edges, request.width);
      break;
  }
  if (reason) throw std::invalid_argument(std::string(model_op_name(op)) + ": " + *reason);
  if (op == ModelOp::knife_plane && mesh.positions.size() == vertex_count) {
    refuse(op, block, "is not crossed by the plane, so the cut would change nothing");
  }
  if (mesh.positions.size() > k_max_vertices) refuse(op, block, "would need past 65535 vertices");
  if (mesh.tris.empty()) refuse(op, block, "would be left with no triangles");

  auto rebuilt = rebuild_block(original, layout, flags, mesh);
  shape_detail::write_sphere(rebuilt, layout, import_detail::bounding_sphere(mesh.positions));
  model.blocks[block] = std::move(rebuilt);
  auto bytes = write_detail::serialize(model);

  const auto check = read_shape(read_nif(bytes), block);
  if (check.verts.size() / 3 != mesh.positions.size() || check.tris.size() / 3 != mesh.tris.size() / 3) {
    throw std::runtime_error("internal error: the edited shape does not hold every vertex and triangle");
  }
  if (report != nullptr) {
    report->vertices_before = vertex_count;
    report->vertices_after = mesh.positions.size();
    report->triangles_before = shape.tris.size() / 3;
    report->triangles_after = mesh.tris.size() / 3;
    report->triangles_added = knife.triangles_added;
    report->degenerate_dropped = knife.degenerate_dropped;
  }
  return bytes;
}

}  // namespace fo4::nif
