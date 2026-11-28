#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Constraint: a cell ships one `<cell8>_Physics.NIF` carrying its collision.
struct PhysicsMeshInput {
  // Constraint: positions are xyz triples in NIF game units, as the source shape stores them.
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
};

// Constraint: the block order is the vanilla one and the root is named after the cell.
// Constraint: a mesh past 65535 vertices starts a new static body, so every body stays a u16 mesh.
std::vector<std::byte> build_physics_nif(std::string_view node_name, std::span<const PhysicsMeshInput> meshes);

}  // namespace fo4::nif
