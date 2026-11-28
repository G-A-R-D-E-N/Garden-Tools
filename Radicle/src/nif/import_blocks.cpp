#include "import_blocks.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace fo4::nif::import_detail {
namespace {

using write_detail::append_int;
using write_detail::append_sized;
using write_detail::half_bits;
using write_detail::unit_byte;

constexpr std::uint32_t k_node_flags = 14;
constexpr std::uint64_t k_base_vertex_flags = 0x41B;
constexpr std::uint64_t k_colour_flag = 1U << 5U;
constexpr std::uint64_t k_skin_flag = 1U << 6U;
constexpr std::size_t k_base_stride = 28;
constexpr std::size_t k_colour_bytes = 4;
constexpr std::size_t k_skin_bytes = 12;
constexpr std::size_t k_influences = 4;
constexpr std::uint32_t k_shader_flags2 = 0x21U;
constexpr std::uint32_t k_texture_clamp = 3;
constexpr std::size_t k_texture_slots = 10;
constexpr std::uint16_t k_collision_owner_flags = 0x80;
constexpr std::uint32_t k_box_material_crc = 0x064003D4U;
constexpr float k_min_box_half_extent = 0.01f;

void put_float(Bytes& out, float value) { append_int(out, std::bit_cast<std::uint32_t>(value)); }

void append_transform(Bytes& out, const Transform& xf) {
  for (const float value : xf.t) put_float(out, value);
  for (const float value : xf.r) put_float(out, value);
  put_float(out, xf.s);
}

void append_av(Bytes& out, std::int32_t name, const std::vector<std::int32_t>& extras, const Transform& xf,
               std::int32_t collision) {
  append_int(out, name);
  append_int(out, static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) append_int(out, extra);
  append_int(out, std::int32_t{-1});
  append_int(out, k_node_flags);
  append_transform(out, xf);
  append_int(out, collision);
}

void append_vertex(Bytes& out, const Mesh& mesh, std::size_t v) {
  for (const float value : mesh.positions[v]) put_float(out, value);
  put_float(out, mesh.bitangents[v][0]);
  append_int(out, half_bits(mesh.uvs[v][0]));
  append_int(out, half_bits(mesh.uvs[v][1]));
  for (const float value : mesh.normals[v]) out.push_back(std::byte{unit_byte(value)});
  out.push_back(std::byte{unit_byte(mesh.bitangents[v][1])});
  for (const float value : mesh.tangents[v]) out.push_back(std::byte{unit_byte(value)});
  out.push_back(std::byte{unit_byte(mesh.bitangents[v][2])});
}

std::uint8_t colour_byte(float value) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Invariant: attribute offsets are 16, 20, 24 for UV, normal and tangent, then 28 for colour and 28 or 32 for skin.
std::uint64_t descriptor_offsets(bool colours, bool skinned, std::size_t stride) {
  std::uint64_t word = (std::uint64_t{4} << 8U) | (std::uint64_t{5} << 16U) | (std::uint64_t{6} << 20U) |
                       static_cast<std::uint64_t>(stride / 4U);
  if (colours) word |= std::uint64_t{7} << 24U;
  if (skinned) word |= (colours ? std::uint64_t{8} : std::uint64_t{7}) << 28U;
  return word;
}

}

Bytes ni_node(const Transform& xf, std::int32_t name, const std::vector<std::int32_t>& extras,
              const std::vector<std::int32_t>& children, std::int32_t collision) {
  Bytes out;
  append_av(out, name, extras, xf, collision);
  append_int(out, static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) append_int(out, child);
  return out;
}

Bytes shape_block(const Mesh& mesh, std::int32_t name, std::int32_t skin_ref, std::int32_t shader_ref) {
  const bool colours = !mesh.colours.empty();
  const bool skinned = !mesh.bones.empty();
  Bytes out;
  append_av(out, name, {}, Transform{}, -1);
  for (const float value : bounding_sphere(mesh.positions)) put_float(out, value);
  append_int(out, skin_ref);
  append_int(out, shader_ref);
  append_int(out, std::int32_t{-1});
  const auto vertex_count = mesh.positions.size();
  const std::size_t stride = k_base_stride + (colours ? k_colour_bytes : 0) + (skinned ? k_skin_bytes : 0);
  const std::uint64_t flags = k_base_vertex_flags | (colours ? k_colour_flag : 0) | (skinned ? k_skin_flag : 0);
  append_int(out, (flags << 44U) | descriptor_offsets(colours, skinned, stride));
  append_int(out, static_cast<std::uint32_t>(mesh.tris.size() / 3));
  append_int(out, static_cast<std::uint16_t>(vertex_count));
  append_int(out, static_cast<std::uint32_t>(vertex_count * stride + mesh.tris.size() * 2));
  for (std::size_t v = 0; v < vertex_count; ++v) {
    append_vertex(out, mesh, v);
    if (colours) {
      for (const float channel : mesh.colours[v]) out.push_back(std::byte{colour_byte(channel)});
    }
    if (skinned) {
      for (const float weight : mesh.weights[v]) append_int(out, half_bits(weight));
      for (const auto index : mesh.bones[v]) out.push_back(std::byte{index});
    }
  }
  for (const auto index : mesh.tris) append_int(out, static_cast<std::uint16_t>(index));
  return out;
}

Bytes shader_block(std::int32_t name, std::int32_t texture_set, std::uint32_t flags1) {
  Bytes out;
  append_int(out, std::uint32_t{0});
  append_int(out, name);
  append_int(out, std::uint32_t{0});
  append_int(out, std::int32_t{-1});
  append_int(out, flags1);
  append_int(out, k_shader_flags2);
  for (const float value : {0.0f, 0.0f, 1.0f, 1.0f}) put_float(out, value);
  append_int(out, texture_set);
  for (const float value : {0.0f, 0.0f, 0.0f, 1.0f}) put_float(out, value);
  append_int(out, std::int32_t{-1});
  append_int(out, k_texture_clamp);
  for (const float value : {1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, std::numeric_limits<float>::max(), 0.0f,
                            1.0f, 5.0f}) {
    put_float(out, value);
  }
  for (int i = 0; i < 6; ++i) put_float(out, -1.0f);
  return out;
}

Bytes texture_set_block(std::string_view diffuse, std::string_view normal) {
  std::array<std::string_view, k_texture_slots> slots{};
  slots[0] = diffuse;
  slots[1] = normal;
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(slots.size()));
  for (const auto slot : slots) append_sized(out, slot);
  return out;
}

Bytes bsx_block(std::int32_t name, std::uint32_t flags) {
  Bytes out;
  append_int(out, name);
  append_int(out, flags);
  return out;
}

Bytes skin_instance_block(std::int32_t skeleton_root, std::int32_t data, const std::vector<std::int32_t>& bones) {
  Bytes out;
  append_int(out, skeleton_root);
  append_int(out, data);
  append_int(out, static_cast<std::uint32_t>(bones.size()));
  for (const auto bone : bones) append_int(out, bone);
  append_int(out, std::uint32_t{0});
  return out;
}

Bytes bone_data_block(const Mesh& mesh, const std::vector<Transform>& binds) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(binds.size()));
  for (std::size_t bone = 0; bone < binds.size(); ++bone) {
    std::vector<Vec3> influenced;
    for (std::size_t v = 0; v < mesh.bones.size(); ++v) {
      for (std::size_t k = 0; k < k_influences; ++k) {
        if (static_cast<std::size_t>(mesh.bones[v][k]) == bone && mesh.weights[v][k] > 0.0f) {
          influenced.push_back(mesh.positions[v]);
        }
      }
    }
    for (const float value : bounding_sphere(influenced)) put_float(out, value);
    for (const float value : binds[bone].r) put_float(out, value);
    for (const float value : binds[bone].t) put_float(out, value);
    put_float(out, binds[bone].s);
  }
  return out;
}

Bytes collision_object_block(std::int32_t physics_block) {
  Bytes out;
  append_int(out, std::int32_t{0});
  append_int(out, k_collision_owner_flags);
  append_int(out, physics_block);
  append_int(out, std::uint32_t{0});
  return out;
}

Bytes physics_system_block(const Bytes& packfile) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(packfile.size()));
  out.insert(out.end(), packfile.begin(), packfile.end());
  return out;
}

std::array<Vec3, 2> bounds_of(const std::vector<Vec3>& positions) {
  std::array<Vec3, 2> bounds{};
  if (positions.empty()) return bounds;
  bounds[0] = positions.front();
  bounds[1] = positions.front();
  for (const auto& p : positions) {
    for (std::size_t k = 0; k < 3; ++k) {
      bounds[0][k] = std::min(bounds[0][k], p[k]);
      bounds[1][k] = std::max(bounds[1][k], p[k]);
    }
  }
  return bounds;
}

Bytes box_packfile(const std::array<Vec3, 2>& bounds) {
  Vec3 center{};
  Vec3 half{};
  for (std::size_t k = 0; k < 3; ++k) {
    center[k] = (bounds[0][k] + bounds[1][k]) / 2.0f / k_hknp_game_units_per_metre;
    half[k] = std::max((bounds[1][k] - bounds[0][k]) / 2.0f / k_hknp_game_units_per_metre, k_min_box_half_extent);
  }
  return write_hknp_packfile(author_hknp_static_body(make_hknp_box(center, half, k_box_material_crc), {}));
}

bool reads_one_box_body(const NifFile& nif) {
  const auto blobs = read_collision_blobs(nif);
  if (blobs.size() != 1U) return false;
  try {
    return decode_hknp_system(read_hknp_packfile(blobs[0].bytes)).bodies.size() == 1U;
  } catch (const fo4::parse_error&) {
    return false;
  }
}

}
