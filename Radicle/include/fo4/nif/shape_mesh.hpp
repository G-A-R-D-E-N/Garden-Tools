#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::nif {

// Constraint: the CSG object carries the shape's own vertex bytes and descriptor.
struct ShapeMeshData {
  std::uint64_t vertex_desc{0};
  std::vector<std::byte> vertices;
  std::vector<std::uint16_t> indices;
};

// Constraint: empty when the block has no vertex data or its descriptor disagrees with the data size.
std::optional<ShapeMeshData> read_shape_mesh(const NifFile& nif, std::uint32_t block);

std::optional<ShapeMeshData> read_first_shape_mesh(const NifFile& nif);

}  // namespace fo4::nif
