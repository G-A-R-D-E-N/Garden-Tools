#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace fo4::nif {
namespace {

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_has_tail_slot = 1U << 8;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr std::size_t k_skin_bytes = 12;
constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_bone_weight_count = 4;
constexpr std::size_t k_bone_index_count = 4;

float half_to_float(std::uint16_t bits) {
  const auto value = static_cast<std::uint32_t>(bits);
  const float sign = (value & 0x8000U) != 0 ? -1.0f : 1.0f;
  const auto exponent = static_cast<int>((value >> 10U) & 0x1FU);
  const auto fraction = static_cast<float>(value & 0x3FFU);
  if (exponent == 0) return sign * std::ldexp(fraction, -24);
  if (exponent == 0x1F) {
    return fraction == 0.0f ? sign * std::numeric_limits<float>::infinity()
                            : std::numeric_limits<float>::quiet_NaN();
  }
  return sign * std::ldexp(fraction + 1024.0f, exponent - 25);
}

float signed_unit_from_byte(std::uint8_t value) {
  return static_cast<float>(value) / 255.0f * 2.0f - 1.0f;
}

float unit_from_byte(std::uint8_t value) {
  return static_cast<float>(value) / 255.0f;
}

std::size_t vertex_bytes_for(std::uint32_t flags) {
  std::size_t bytes = (flags & k_full_precision) != 0 ? 16 : 8;
  for (const std::uint32_t bit : {k_has_uv, k_has_uv2, k_has_normal, k_has_tangent, k_has_colour}) {
    if ((flags & bit) != 0) bytes += 4;
  }
  if ((flags & k_has_skin) != 0) bytes += k_skin_bytes;
  if ((flags & k_has_tail_slot) != 0) bytes += 4;
  return bytes;
}

std::uint8_t read_signed_unit_vector(binary::Cursor& vertex, std::vector<float>& out) {
  for (std::size_t i = 0; i < 3; ++i) out.push_back(signed_unit_from_byte(vertex.read_u8()));
  return vertex.read_u8();
}

void read_vertex(binary::Cursor& vertex, std::uint32_t flags, Shape& shape) {
  const bool full = (flags & k_full_precision) != 0;
  for (std::size_t i = 0; i < 3; ++i) {
    shape.verts.push_back(full ? vertex.read_f32() : half_to_float(vertex.read_u16()));
  }
  const float bitangent_x = full ? vertex.read_f32() : half_to_float(vertex.read_u16());
  if ((flags & k_has_uv) != 0) {
    shape.uvs.push_back(half_to_float(vertex.read_u16()));
    shape.uvs.push_back(half_to_float(vertex.read_u16()));
  }
  if ((flags & k_has_uv2) != 0) vertex.read_bytes(4);
  std::uint8_t bitangent_y = 0;
  std::uint8_t bitangent_z = 0;
  if ((flags & k_has_normal) != 0) bitangent_y = read_signed_unit_vector(vertex, shape.normals);
  if ((flags & k_has_tangent) != 0) bitangent_z = read_signed_unit_vector(vertex, shape.tangents);
  if ((flags & k_has_normal) != 0 && (flags & k_has_tangent) != 0) {
    shape.bitangents.push_back(bitangent_x);
    shape.bitangents.push_back(signed_unit_from_byte(bitangent_y));
    shape.bitangents.push_back(signed_unit_from_byte(bitangent_z));
  }
  if ((flags & k_has_colour) != 0) {
    for (std::size_t i = 0; i < 4; ++i) shape.colors.push_back(unit_from_byte(vertex.read_u8()));
  }
  if ((flags & k_has_skin) != 0) {
    for (std::size_t i = 0; i < k_bone_weight_count; ++i) shape.weights.push_back(half_to_float(vertex.read_u16()));
    for (std::size_t i = 0; i < k_bone_index_count; ++i) shape.bone_indices.push_back(vertex.read_u8());
  }
  if ((flags & k_has_tail_slot) != 0) vertex.read_bytes(4);
}

}  // namespace

bool is_shape_block_type(std::string_view type) {
  return type == "BSTriShape" || type == "BSSubIndexTriShape" || type == "BSMeshLODTriShape" ||
         type == "BSDynamicTriShape";
}

Shape read_shape(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  const auto header = detail::read_av_header(cursor, nif);
  cursor.read_i32();
  cursor.read_bytes(16);

  Shape shape;
  shape.block = block;
  shape.name = header.name;
  shape.parent_node = shape_parent_node(nif, block);
  shape.skin_ref = cursor.read_i32();
  shape.shader_ref = cursor.read_i32();
  shape.alpha_ref = cursor.read_i32();
  const auto descriptor_low = cursor.read_u32();
  const auto descriptor_high = cursor.read_u32();
  const auto descriptor =
      static_cast<std::uint64_t>(descriptor_low) | (static_cast<std::uint64_t>(descriptor_high) << 32U);
  const auto triangle_count = static_cast<std::size_t>(cursor.read_u32());
  const auto vertex_count = static_cast<std::size_t>(cursor.read_u16());
  const auto data_size = static_cast<std::size_t>(cursor.read_u32());
  const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  const bool skinned = (flags & k_has_skin) != 0;

  if (shape.shader_ref >= 0 && static_cast<std::size_t>(shape.shader_ref) >= nif.blocks().size()) {
    throw parse_error(cursor.absolute_offset(), "shader reference is out of range");
  }
  if (skinned && shape.skin_ref >= 0 && static_cast<std::size_t>(shape.skin_ref) >= nif.blocks().size()) {
    throw parse_error(cursor.absolute_offset(), "skin reference is out of range");
  }
  if (shape.shader_ref >= 0) {
    try {
      const auto shader = read_shader(nif, static_cast<std::uint32_t>(shape.shader_ref));
      shape.material = shader.name;
      shape.textures = shader.textures;
    } catch (const parse_error& error) {
      shape.warning = std::string{"shader block: "} + error.parse_message();
    }
  }

  if (data_size == 0 || vertex_count == 0 || stride == 0) return shape;

  if (triangle_count > cursor.remaining() / k_triangle_bytes) {
    throw parse_error(cursor.absolute_offset(), "triangle count runs past the end of its block");
  }
  if (vertex_count * stride + triangle_count * k_triangle_bytes != data_size) {
    throw parse_error(cursor.absolute_offset(), "vertex data size does not match the vertex descriptor");
  }
  if (stride != vertex_bytes_for(flags)) {
    throw parse_error(cursor.absolute_offset(), "vertex attributes do not fill the vertex stride exactly");
  }

  const auto vertex_data = cursor.read_bytes(vertex_count * stride);
  const auto triangle_data = cursor.read_bytes(triangle_count * k_triangle_bytes);
  for (std::size_t v = 0; v < vertex_count; ++v) {
    binary::Cursor vertex(vertex_data.subspan(v * stride, stride));
    read_vertex(vertex, flags, shape);
  }

  binary::Cursor triangles(triangle_data);
  for (std::size_t i = 0; i < triangle_count * 3; ++i) {
    const auto index = static_cast<std::size_t>(triangles.read_u16());
    if (index >= vertex_count) throw parse_error(cursor.absolute_offset(), "triangle index is out of range");
    shape.tris.push_back(static_cast<std::uint16_t>(index));
  }

  if (skinned && shape.skin_ref >= 0) {
    shape.bones = read_bones(nif, static_cast<std::uint32_t>(shape.skin_ref));
    if (shape.bones.empty() && !shape.bone_indices.empty()) {
      shape.weights.clear();
      shape.bone_indices.clear();
      const std::string note = "the skin lists no bones, so its weights were dropped";
      shape.warning = shape.warning.empty() ? note : shape.warning + "; " + note;
    }
    if (!shape.bones.empty()) {
      for (const auto index : shape.bone_indices) {
        if (static_cast<std::size_t>(index) >= shape.bones.size()) {
          throw parse_error(cursor.absolute_offset(), "bone index is past the end of the skin's bone list");
        }
      }
    }
  }
  return shape;
}

std::vector<Shape> read_shapes(const NifFile& nif) {
  std::vector<Shape> shapes;
  for (std::size_t i = 0; i < nif.blocks().size(); ++i) {
    if (is_shape_block_type(nif.blocks()[i].type)) shapes.push_back(read_shape(nif, static_cast<std::uint32_t>(i)));
  }
  return shapes;
}

}  // namespace fo4::nif
