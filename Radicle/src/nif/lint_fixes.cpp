#include "lint_internal.hpp"

#include "fo4/nif/nif.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;
using write_detail::Model;
using write_detail::append_int;
using write_detail::append_sized;
using write_detail::read_int;

void relocate_texture_paths(Model& model, std::uint32_t block) {
  const auto& bytes = model.blocks[block];
  const auto count = read_int<std::uint32_t>(bytes, 0);
  Bytes rebuilt;
  append_int(rebuilt, count);
  bool changed = false;
  std::size_t at = 4;
  for (std::uint32_t slot = 0; slot < count; ++slot) {
    const auto length = read_int<std::uint32_t>(bytes, at);
    at += 4;
    const std::string path(reinterpret_cast<const char*>(bytes.data() + at), length);
    at += length;
    const auto relative = relative_texture_path(path);
    changed = changed || !relative.empty();
    append_sized(rebuilt, relative.empty() ? path : relative);
  }
  if (changed) model.blocks[block] = rebuilt;
}

void refit_bounding_sphere(const NifFile& nif, Model& model, const Shape& shape) {
  constexpr float inf = std::numeric_limits<float>::infinity();
  std::array<float, 3> low{inf, inf, inf};
  std::array<float, 3> high{-inf, -inf, -inf};
  for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
    if (!finite_position(shape, v)) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      low[k] = std::min(low[k], shape.verts[v + k]);
      high[k] = std::max(high[k], shape.verts[v + k]);
    }
  }
  std::vector<float> sphere(4);
  for (std::size_t k = 0; k < 3; ++k) sphere[k] = (low[k] + high[k]) * 0.5f;
  float radius_sq = 0.0f;
  for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
    if (!finite_position(shape, v)) continue;
    float distance_sq = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) {
      const float d = shape.verts[v + k] - sphere[k];
      distance_sq += d * d;
    }
    radius_sq = std::max(radius_sq, distance_sq);
  }
  sphere[3] = std::sqrt(radius_sq);
  edit_detail::write_floats(nif, model, shape.block, bounding_sphere_offset(nif, shape.block), sphere);
}

void drop_repeated_vertex_triangles(const NifFile& nif, Model& model, const Shape& shape) {
  const auto kept = triangles_after_fix(nif, shape);
  if (kept.size() == shape.tris.size()) return;
  const auto run = vertex_run(nif, shape.block);
  const auto& bytes = model.blocks[shape.block];
  const auto vertex_count = read_int<std::uint16_t>(bytes, run.first - 6);
  const auto vertex_end = run.first + static_cast<std::size_t>(vertex_count) * run.stride;
  const auto block_end = run.first + run.data_size;
  Bytes rebuilt(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(run.first - 10));
  append_int(rebuilt, static_cast<std::uint32_t>(kept.size() / 3));
  append_int(rebuilt, vertex_count);
  append_int(rebuilt, static_cast<std::uint32_t>(vertex_end - run.first + kept.size() * 2));
  rebuilt.insert(rebuilt.end(), bytes.begin() + static_cast<std::ptrdiff_t>(run.first),
                 bytes.begin() + static_cast<std::ptrdiff_t>(vertex_end));
  for (const auto index : kept) append_int(rebuilt, index);
  rebuilt.insert(rebuilt.end(), bytes.begin() + static_cast<std::ptrdiff_t>(block_end), bytes.end());
  model.blocks[shape.block] = std::move(rebuilt);
}

}  // namespace

void lint_fixes(const NifFile& nif, Model& model, std::string_view check) {
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if ((check.empty() || check == "absolute texture paths can be made relative") &&
        nif.blocks()[block].type == edit_detail::k_texture_set_type) relocate_texture_paths(model, block);
  }
  for (const auto& shape : read_shapes(nif)) {
    if ((check.empty() || check == "rigid shapes carry a bounding sphere") &&
        !shape.verts.empty() && bounding_sphere_misses(nif, shape)) refit_bounding_sphere(nif, model, shape);
    if (check.empty() || check == "no triangles name a vertex twice") drop_repeated_vertex_triangles(nif, model, shape);
  }
}

}  // namespace fo4::nif
