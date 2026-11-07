#include "fo4/nif/nif_import.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_hull.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "import_mesh.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

using import_detail::Mesh;
using import_detail::Vec3;
using write_detail::Bytes;
using write_detail::append_int;
using write_detail::append_sized;
using write_detail::half_bits;
using write_detail::unit_byte;

constexpr std::uint32_t k_nif_version = 0x14020007U;
constexpr std::uint32_t k_user_version = 12;
constexpr std::uint32_t k_bs_version = 130;
constexpr std::uint32_t k_node_flags = 14;
constexpr std::uint64_t k_vertex_flags = 0x41B;
constexpr std::uint64_t k_vertex_stride = 28;
constexpr std::uint64_t k_vertex_offsets = 0x650400;
constexpr std::uint32_t k_shader_flags1 = 0x80400201U;
constexpr std::uint32_t k_shader_flags2 = 0x21U;
constexpr std::uint32_t k_texture_clamp = 3;
constexpr std::size_t k_max_vertices = 65535;
constexpr std::size_t k_texture_slots = 10;
constexpr std::int32_t k_shape_block = 1;
constexpr std::int32_t k_shader_block = 2;
constexpr std::int32_t k_texture_set_block = 3;
constexpr std::int32_t k_bsx_block = 4;
constexpr std::int32_t k_collision_object_block = 5;
constexpr std::int32_t k_physics_system_block = 6;
constexpr std::uint32_t k_collision_bsx_flags = 130;
constexpr std::uint16_t k_collision_owner_flags = 0x80;
constexpr std::uint32_t k_collision_material_crc = 0x064003D4U;
constexpr float k_min_box_half_extent = 0.01f;
constexpr std::array<float, 13> k_identity_transform{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                                     0.0f, 0.0f, 1.0f, 1.0f};

void put_float(Bytes& out, float value) { append_int(out, std::bit_cast<std::uint32_t>(value)); }

void append_av(Bytes& out, std::int32_t name, const std::vector<std::int32_t>& extras,
               std::int32_t collision = -1) {
  append_int(out, name);
  append_int(out, static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) append_int(out, extra);
  append_int(out, std::int32_t{-1});
  append_int(out, k_node_flags);
  for (const float value : k_identity_transform) put_float(out, value);
  append_int(out, collision);
}

Bytes root_block(std::int32_t name, std::int32_t collision) {
  Bytes out;
  append_av(out, name, {k_bsx_block}, collision);
  append_int(out, std::uint32_t{1});
  append_int(out, k_shape_block);
  return out;
}

std::array<float, 4> bounding_sphere(const std::vector<Vec3>& positions) {
  std::array<float, 3> center{};
  const auto count = static_cast<float>(positions.size());
  for (const auto& p : positions) {
    for (std::size_t k = 0; k < 3; ++k) center[k] += p[k] / count;
  }
  float radius_sq = 0.0f;
  for (const auto& p : positions) {
    float distance_sq = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) distance_sq += (p[k] - center[k]) * (p[k] - center[k]);
    radius_sq = std::max(radius_sq, distance_sq);
  }
  return {center[0], center[1], center[2], std::sqrt(radius_sq)};
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

Bytes shape_block(const Mesh& mesh, std::int32_t name) {
  Bytes out;
  append_av(out, name, {});
  for (const float value : bounding_sphere(mesh.positions)) put_float(out, value);
  append_int(out, std::int32_t{-1});
  append_int(out, k_shader_block);
  append_int(out, std::int32_t{-1});
  const auto vertex_count = mesh.positions.size();
  append_int(out, (k_vertex_flags << 44U) | k_vertex_offsets | (k_vertex_stride / 4U));
  append_int(out, static_cast<std::uint32_t>(mesh.tris.size() / 3));
  append_int(out, static_cast<std::uint16_t>(vertex_count));
  append_int(out, static_cast<std::uint32_t>(vertex_count * k_vertex_stride + mesh.tris.size() * 2));
  for (std::size_t v = 0; v < vertex_count; ++v) append_vertex(out, mesh, v);
  for (const auto index : mesh.tris) append_int(out, static_cast<std::uint16_t>(index));
  return out;
}

Bytes shader_block(std::int32_t name, std::int32_t texture_set) {
  Bytes out;
  append_int(out, std::uint32_t{0});
  append_int(out, name);
  append_int(out, std::uint32_t{0});
  append_int(out, std::int32_t{-1});
  append_int(out, k_shader_flags1);
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

Bytes texture_set_block(const ImportOptions& options) {
  std::array<std::string_view, k_texture_slots> slots{};
  slots[0] = options.tex_diffuse;
  slots[1] = options.tex_normal;
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

Bytes collision_object_block() {
  Bytes out;
  append_int(out, std::int32_t{0});
  append_int(out, k_collision_owner_flags);
  append_int(out, k_physics_system_block);
  append_int(out, std::uint32_t{0});
  return out;
}

Bytes physics_system_block(const Bytes& packfile) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(packfile.size()));
  out.insert(out.end(), packfile.begin(), packfile.end());
  return out;
}

HknpShape box_shape(const Mesh& mesh) {
  Vec3 low = mesh.positions.front();
  Vec3 high = low;
  for (const auto& p : mesh.positions) {
    for (std::size_t k = 0; k < 3; ++k) {
      low[k] = std::min(low[k], p[k]);
      high[k] = std::max(high[k], p[k]);
    }
  }
  Vec3 center{};
  Vec3 half{};
  for (std::size_t k = 0; k < 3; ++k) {
    center[k] = (low[k] + high[k]) / 2.0f / k_hknp_game_units_per_metre;
    half[k] = std::max((high[k] - low[k]) / 2.0f / k_hknp_game_units_per_metre, k_min_box_half_extent);
  }
  return make_hknp_box(center, half, k_collision_material_crc);
}

HknpShape hull_shape(const Mesh& mesh, HknpVec4& inertia_orientation) {
  std::vector<HknpVec3> points;
  points.reserve(mesh.positions.size());
  for (const auto& p : mesh.positions) {
    points.push_back({p[0] / k_hknp_game_units_per_metre, p[1] / k_hknp_game_units_per_metre,
                      p[2] / k_hknp_game_units_per_metre});
  }
  std::vector<HknpTriangle> triangles;
  triangles.reserve(mesh.tris.size() / 3);
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    triangles.push_back({mesh.tris[t], mesh.tris[t + 1], mesh.tris[t + 2]});
  }
  return make_hknp_hull(points, triangles, k_collision_material_crc, &inertia_orientation);
}

HknpShape mesh_shape(const Mesh& mesh) {
  HknpShape shape;
  shape.kind = HknpShapeKind::compressed_mesh;
  shape.class_name = "hknpCompressedMeshShape";
  shape.material_crc = k_collision_material_crc;
  for (const auto& p : mesh.positions) {
    shape.mesh_vertices.push_back({p[0] / k_hknp_game_units_per_metre, p[1] / k_hknp_game_units_per_metre,
                                   p[2] / k_hknp_game_units_per_metre});
  }
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    HknpMeshTriangle triangle;
    triangle.indices = {mesh.tris[t], mesh.tris[t + 1], mesh.tris[t + 2], 0U};
    triangle.material_crc = k_collision_material_crc;
    shape.mesh_triangles.push_back(triangle);
  }
  return shape;
}

Bytes collision_packfile(const Mesh& mesh, const ImportOptions& options) {
  HknpSystemBody body;
  if (options.hull_collision) {
    body.shape = hull_shape(mesh, body.inertia_orientation);
  } else if (options.mesh_collision) {
    body.shape = mesh_shape(mesh);
  } else {
    body.shape = box_shape(mesh);
  }
  body.mass = options.body_mass;
  return write_hknp_packfile(author_hknp_system({body}));
}

bool reads_one_collision_body(const NifFile& nif) {
  const auto blobs = read_collision_blobs(nif);
  if (blobs.size() != 1U) return false;
  try {
    return decode_hknp_system(read_hknp_packfile(blobs[0].bytes)).bodies.size() == 1U;
  } catch (const fo4::parse_error&) {
    return false;
  }
}

}

std::vector<std::byte> import_obj(std::string_view obj_text, const ImportOptions& options) {
  const bool box = options.box_collision;
  const bool hull = options.hull_collision;
  const bool mesh_choice = options.mesh_collision;
  const bool collision = box || hull || mesh_choice;
  if ((box ? 1 : 0) + (hull ? 1 : 0) + (mesh_choice ? 1 : 0) > 1) {
    throw std::invalid_argument("choose one collision shape, box, hull or mesh");
  }
  if (!std::isfinite(options.body_mass) || options.body_mass < 0.0f) {
    throw std::invalid_argument("body mass must be a finite number of kilograms, and 0 keeps the body static");
  }
  if (options.body_mass > 0.0f && !collision) {
    throw std::invalid_argument("a body mass needs a collision choice to carry it");
  }
  if (options.body_mass > 0.0f && mesh_choice) {
    throw std::invalid_argument("a mesh collision is static only; a body mass needs a box or hull");
  }
  const auto mesh = import_detail::read_obj(obj_text, options.from_blender);
  if (mesh.positions.size() > k_max_vertices) {
    throw std::runtime_error("OBJ has " + std::to_string(mesh.positions.size()) +
                             " unique vertices; a NIF shape holds at most 65535");
  }

  write_detail::Model model;
  model.banner = "Gamebryo File Format, Version 20.2.0.7";
  model.version = k_nif_version;
  model.user_version = k_user_version;
  model.bs_version = k_bs_version;
  model.export_strings.assign(4, std::string{});
  model.block_types = {"NiNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet", "BSXFlags"};
  model.block_type_index = {0, 1, 2, 3, 4};
  if (collision) {
    model.block_types.push_back("bhkNPCollisionObject");
    model.block_types.push_back("bhkPhysicsSystem");
    model.block_type_index.push_back(5);
    model.block_type_index.push_back(6);
  }
  model.roots = {0};

  const auto root_name = write_detail::intern_string(model, "Scene Root");
  const auto shape_name = write_detail::intern_string(model, mesh.name);
  const auto material = write_detail::intern_string(model, options.material);
  const auto bsx_name = write_detail::intern_string(model, "BSX");
  model.blocks = {root_block(root_name, collision ? k_collision_object_block : -1), shape_block(mesh, shape_name),
                  shader_block(material, k_texture_set_block), texture_set_block(options),
                  bsx_block(bsx_name, collision ? k_collision_bsx_flags : 0U)};
  if (collision) {
    model.blocks.push_back(collision_object_block());
    model.blocks.push_back(physics_system_block(collision_packfile(mesh, options)));
  }
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }

  auto bytes = write_detail::serialize(model);
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  if (shapes.size() != 1U || !shapes[0].warning.empty() || shapes[0].verts.size() != 3 * mesh.positions.size() ||
      shapes[0].tris.size() != mesh.tris.size() || (collision && !reads_one_collision_body(nif))) {
    throw std::runtime_error("internal error: the written NIF does not read back as one clean shape");
  }
  return bytes;
}

}
