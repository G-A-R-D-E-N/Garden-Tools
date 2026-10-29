#include "fo4/nif/nif_import.hpp"

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
constexpr std::array<float, 13> k_identity_transform{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                                     0.0f, 0.0f, 1.0f, 1.0f};

void put_float(Bytes& out, float value) { append_int(out, std::bit_cast<std::uint32_t>(value)); }

void append_av(Bytes& out, std::int32_t name, const std::vector<std::int32_t>& extras) {
  append_int(out, name);
  append_int(out, static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) append_int(out, extra);
  append_int(out, std::int32_t{-1});
  append_int(out, k_node_flags);
  for (const float value : k_identity_transform) put_float(out, value);
  append_int(out, std::int32_t{-1});
}

Bytes root_block(std::int32_t name) {
  Bytes out;
  append_av(out, name, {k_bsx_block});
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

Bytes bsx_block(std::int32_t name) {
  Bytes out;
  append_int(out, name);
  append_int(out, std::uint32_t{0});
  return out;
}

}

std::vector<std::byte> import_obj(std::string_view obj_text, const ImportOptions& options) {
  if (options.box_collision) {
    throw std::runtime_error("box collision needs niftool; the in-process import writes no collision");
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
  model.roots = {0};

  const auto root_name = write_detail::intern_string(model, "Scene Root");
  const auto shape_name = write_detail::intern_string(model, mesh.name);
  const auto material = write_detail::intern_string(model, options.material);
  const auto bsx_name = write_detail::intern_string(model, "BSX");
  model.blocks = {root_block(root_name), shape_block(mesh, shape_name), shader_block(material, k_texture_set_block),
                  texture_set_block(options), bsx_block(bsx_name)};
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }

  auto bytes = write_detail::serialize(model);
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  if (shapes.size() != 1U || !shapes[0].warning.empty() || shapes[0].verts.size() != 3 * mesh.positions.size() ||
      shapes[0].tris.size() != mesh.tris.size()) {
    throw std::runtime_error("internal error: the written NIF does not read back as one clean shape");
  }
  return bytes;
}

}
