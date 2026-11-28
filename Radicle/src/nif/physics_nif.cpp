#include "fo4/nif/physics_nif.hpp"

#include "fo4/constants.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "import_blocks.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;

// Constraint: the authored collision surface uses the same material word the import path stores on every shape.
constexpr std::uint32_t k_physics_material_crc = 0x064003D4U;
constexpr std::int32_t k_bsx_block = 1;
constexpr std::int32_t k_physics_system_block = 3;

HknpSystemBody static_mesh_body(HknpShape shape) {
  HknpSystemBody body;
  body.shape = std::move(shape);
  body.mass = 0.0f;
  return body;
}

// Constraint: a section tree keeps its left leaf count in one byte, so a shape stays under 511 sections and the split keeps room to spare.
constexpr std::size_t k_section_budget = 480;
// Constraint: the packfile indexes at most 255 bodies, and a split doubles the count, so the surface budget stays below that.
constexpr std::size_t k_max_bodies = 255;

HknpShape compressed_shape() {
  HknpShape shape;
  shape.kind = HknpShapeKind::compressed_mesh;
  shape.class_name = "hknpCompressedMeshShape";
  shape.material_crc = k_physics_material_crc;
  return shape;
}

// Invariant: the returned shape stores only the vertices this triangle range names, renumbered from zero.
HknpShape shape_for_range(const std::vector<HknpVec3>& vertices, const std::vector<HknpMeshTriangle>& triangles,
                          std::size_t first, std::size_t last) {
  auto shape = compressed_shape();
  std::vector<std::int64_t> local(vertices.size(), -1);
  for (std::size_t t = first; t < last; ++t) {
    HknpMeshTriangle triangle = triangles[t];
    const auto used = triangle.quad ? 4U : 3U;
    for (std::uint32_t k = 0; k < used; ++k) {
      const auto global = triangle.indices[k];
      if (local[global] < 0) {
        local[global] = static_cast<std::int64_t>(shape.mesh_vertices.size());
        shape.mesh_vertices.push_back(vertices[global]);
      }
      triangle.indices[k] = static_cast<std::uint32_t>(local[global]);
    }
    shape.mesh_triangles.push_back(triangle);
  }
  return shape;
}

void append_bodies(std::vector<HknpSystemBody>& bodies, const std::vector<HknpVec3>& vertices,
                   const std::vector<HknpMeshTriangle>& triangles, std::size_t first, std::size_t last) {
  auto shape = shape_for_range(vertices, triangles, first, last);
  // Constraint: one section per 128 primitives at best, so a large surface is split into bodies rather than truncated.
  if (hknp_section_count(shape) <= k_section_budget || last - first <= 1U) {
    bodies.push_back(static_mesh_body(std::move(shape)));
    return;
  }
  if (bodies.size() + 2U > k_max_bodies) {
    throw std::invalid_argument("a cell physics NIF needs more than 255 bodies to hold its sections");
  }
  const auto middle = first + (last - first) / 2U;
  append_bodies(bodies, vertices, triangles, first, middle);
  append_bodies(bodies, vertices, triangles, middle, last);
}

Bytes build_packfile(std::span<const PhysicsMeshInput> meshes) {
  std::vector<HknpVec3> vertices;
  std::vector<HknpMeshTriangle> triangles;
  for (const auto& mesh : meshes) {
    const auto count = mesh.positions.size() / 3U;
    if (count == 0 || mesh.indices.size() < 3) continue;
    const auto base = static_cast<std::uint32_t>(vertices.size());
    for (std::size_t v = 0; v < count; ++v) {
      vertices.push_back({mesh.positions[3 * v] / k_hknp_game_units_per_metre,
                          mesh.positions[3 * v + 1] / k_hknp_game_units_per_metre,
                          mesh.positions[3 * v + 2] / k_hknp_game_units_per_metre});
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      HknpMeshTriangle triangle;
      triangle.indices = {base + mesh.indices[i], base + mesh.indices[i + 1], base + mesh.indices[i + 2], 0U};
      triangle.material_crc = k_physics_material_crc;
      triangles.push_back(triangle);
    }
  }
  if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::invalid_argument("a cell physics mesh names more vertices than a triangle index can hold");
  }
  if (triangles.empty()) throw std::invalid_argument("a cell physics NIF needs at least one triangle");
  std::vector<HknpSystemBody> bodies;
  append_bodies(bodies, vertices, triangles, 0, triangles.size());
  if (bodies.empty()) throw std::invalid_argument("a cell physics NIF needs at least one triangle");
  if (bodies.size() > k_max_bodies) throw std::invalid_argument("a cell physics NIF holds at most 255 bodies");
  return write_hknp_packfile(author_hknp_system(bodies));
}

}  // namespace

std::vector<std::byte> build_physics_nif(std::string_view node_name, std::span<const PhysicsMeshInput> meshes) {
  const auto packfile = build_packfile(meshes);
  write_detail::Model model;
  model.banner = "Gamebryo File Format, Version 20.2.0.7";
  model.version = k_nif_version;
  model.user_version = k_nif_user_version;
  model.bs_version = k_nif_bs_version;
  model.export_strings.assign(4, std::string{});
  model.block_types = {"NiNode", "BSXFlags", "bhkNPCollisionObject", "bhkPhysicsSystem"};
  model.block_type_index = {0, 1, 2, 3};
  model.roots = {0};
  const auto root_name = write_detail::intern_string(model, std::string(node_name));
  const auto bsx_name = write_detail::intern_string(model, "BSX");
  model.blocks = {import_detail::ni_node(Transform{}, root_name, {k_bsx_block}, {}, -1),
                  import_detail::bsx_block(bsx_name, 0U),
                  import_detail::collision_object_block(k_physics_system_block),
                  import_detail::physics_system_block(packfile)};
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
