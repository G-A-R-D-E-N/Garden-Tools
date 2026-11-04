#include "shape_ops_internal.hpp"

#include "fo4/binary/cursor.hpp"
#include "nif_internal.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::nif::shape_detail {
namespace {

using write_detail::half_bits;
using write_detail::read_int;
using write_detail::unit_byte;
using write_detail::write_int;

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr std::size_t k_transform_bytes = 52;
constexpr std::size_t k_sphere_bytes = 16;
constexpr std::size_t k_reference_bytes = 12;
constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_full_head = 16;
constexpr std::size_t k_half_head = 8;
constexpr std::size_t k_full_bitangent_x = 12;
constexpr std::size_t k_half_bitangent_x = 6;
constexpr std::size_t k_uv_bytes = 4;
constexpr std::size_t k_component_bytes = 4;
constexpr std::size_t k_axes = 3;
constexpr std::size_t k_sphere_terms = 4;
constexpr std::size_t k_transform_terms = 13;

constexpr std::array<float, k_transform_terms> k_identity_transform{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                                                    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};

void put_float_at(Bytes& bytes, std::size_t offset, float value) {
  write_int(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void put_unit_bytes(Bytes& bytes, std::size_t offset, const Vec3& value) {
  for (std::size_t k = 0; k < k_axes; ++k) bytes[offset + k] = std::byte{unit_byte(value[k])};
}

}  // namespace

std::vector<Vec3> to_vec3(const std::vector<float>& flat) {
  std::vector<Vec3> out;
  out.reserve(flat.size() / k_axes);
  for (std::size_t i = 0; i + k_axes <= flat.size(); i += k_axes) out.push_back({flat[i], flat[i + 1], flat[i + 2]});
  return out;
}

Mesh shape_mesh(const Shape& shape) {
  Mesh mesh;
  mesh.positions = to_vec3(shape.verts);
  mesh.normals = to_vec3(shape.normals);
  for (std::size_t i = 0; i + 1 < shape.uvs.size(); i += 2) mesh.uvs.push_back({shape.uvs[i], shape.uvs[i + 1]});
  mesh.tris.assign(shape.tris.begin(), shape.tris.end());
  return mesh;
}

Layout read_layout(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_av_header(cursor, nif);
  Layout layout;
  layout.transform_at = cursor.position() - k_transform_bytes;
  cursor.read_i32();
  layout.sphere_at = cursor.position();
  cursor.read_bytes(k_sphere_bytes);
  cursor.read_bytes(k_reference_bytes);
  const auto low = cursor.read_u32();
  const auto high = cursor.read_u32();
  const auto descriptor = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
  layout.triangle_count = static_cast<std::size_t>(cursor.read_u32());
  layout.vertex_count = static_cast<std::size_t>(cursor.read_u16());
  cursor.read_u32();
  layout.vertices_at = cursor.position();
  layout.stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  const bool has_uv2 = (flags & k_has_uv2) != 0;
  layout.full = (flags & k_full_precision) != 0;
  layout.skinned = (flags & k_has_skin) != 0;
  layout.has_uv = (flags & k_has_uv) != 0;
  layout.has_normal = (flags & k_has_normal) != 0;
  layout.has_tangent = (flags & k_has_tangent) != 0;
  layout.bitangent_x_at = layout.full ? k_full_bitangent_x : k_half_bitangent_x;
  layout.normal_at = (layout.full ? k_full_head : k_half_head) + (layout.has_uv ? k_uv_bytes : 0) +
                     (has_uv2 ? k_uv_bytes : 0);
  layout.triangles_at = layout.vertices_at + layout.vertex_count * layout.stride;
  return layout;
}

Frame read_frame(const Shape& shape, const Layout& layout) {
  Frame frame;
  if (layout.has_normal) frame.normals = to_vec3(shape.normals);
  if (layout.has_tangent) frame.tangents = to_vec3(shape.tangents);
  if (layout.has_normal && layout.has_tangent) frame.bitangents = to_vec3(shape.bitangents);
  return frame;
}

void write_frame(Bytes& bytes, const Layout& layout, const Frame& frame) {
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    const auto vertex = layout.vertices_at + v * layout.stride;
    const auto attribute = vertex + layout.normal_at;
    if (layout.has_normal) put_unit_bytes(bytes, attribute, frame.normals[v]);
    if (layout.has_tangent) {
      put_unit_bytes(bytes, attribute + (layout.has_normal ? k_component_bytes : 0), frame.tangents[v]);
    }
    if (!layout.has_normal || !layout.has_tangent) continue;
    bytes[attribute + 3] = std::byte{unit_byte(frame.bitangents[v][1])};
    bytes[attribute + 7] = std::byte{unit_byte(frame.bitangents[v][2])};
    if (layout.full) {
      put_float_at(bytes, vertex + layout.bitangent_x_at, frame.bitangents[v][0]);
    } else {
      write_int(bytes, vertex + layout.bitangent_x_at, half_bits(frame.bitangents[v][0]));
    }
  }
}

void write_positions(Bytes& bytes, const Layout& layout, const std::vector<Vec3>& positions) {
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    const auto vertex = layout.vertices_at + v * layout.stride;
    for (std::size_t k = 0; k < k_axes; ++k) put_float_at(bytes, vertex + k * k_component_bytes, positions[v][k]);
  }
}

void swap_winding(Bytes& bytes, const Layout& layout) {
  for (std::size_t t = 0; t < layout.triangle_count; ++t) {
    const auto at = layout.triangles_at + t * k_triangle_bytes;
    const auto second = read_int<std::uint16_t>(bytes, at + 2);
    write_int(bytes, at + 2, read_int<std::uint16_t>(bytes, at + 4));
    write_int(bytes, at + 4, second);
  }
}

void write_sphere(Bytes& bytes, const Layout& layout, const std::array<float, 4>& sphere) {
  for (std::size_t k = 0; k < k_sphere_terms; ++k) {
    put_float_at(bytes, layout.sphere_at + k * k_component_bytes, sphere[k]);
  }
}

void write_identity_transform(Bytes& bytes, const Layout& layout) {
  for (std::size_t k = 0; k < k_transform_terms; ++k) {
    put_float_at(bytes, layout.transform_at + k * k_component_bytes, k_identity_transform[k]);
  }
}

}  // namespace fo4::nif::shape_detail
