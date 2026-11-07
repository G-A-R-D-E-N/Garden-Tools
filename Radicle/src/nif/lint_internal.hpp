#pragma once

#include "fo4/nif/nif.hpp"
#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Requires a tri-shape block; the sphere follows the AV header, the extra-data list and the collision reference.
std::size_t bounding_sphere_offset(const NifFile& nif, std::uint32_t block);
bool bounding_sphere_misses(const NifFile& nif, const Shape& shape);
bool finite_position(const Shape& shape, std::size_t v);
// Returns the path from its Textures folder on, or an empty string when there is no such folder to cut to.
std::string relative_texture_path(std::string_view path);

struct VertexRun {
  std::size_t first;
  std::size_t stride;
  std::uint32_t flags;
  std::uint32_t data_size;
};
// Requires a tri-shape block; the triangle count, vertex count and data size sit in the 10 bytes before first.
VertexRun vertex_run(const NifFile& nif, std::uint32_t block);
// Invariant: only BSTriShape and BSDynamicTriShape have triangles the fix can drop; the others keep their triangles.
bool repairs_triangles(const NifFile& nif, const Shape& shape);
bool repeats_a_vertex(const Shape& shape, std::size_t triangle);
// Constraint: a shape left with no triangles keeps them all, since an empty shape fails another check.
std::vector<std::uint16_t> triangles_after_fix(const NifFile& nif, const Shape& shape);
void lint_fixes(const NifFile& nif, write_detail::Model& model, std::string_view check);

}  // namespace fo4::nif
