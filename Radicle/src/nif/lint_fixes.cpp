#include "lint_internal.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {

std::vector<std::string> parse_texture_slots(const write_detail::Bytes& bytes) {
  binary::Cursor cursor(bytes, 0);
  const auto count = cursor.read_u32();
  std::vector<std::string> slots;
  for (std::uint32_t slot = 0; slot < count; ++slot) {
    const auto text = cursor.read_bytes(cursor.read_u32());
    slots.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
  }
  if (!cursor.at_end()) throw parse_error(cursor.absolute_offset(), "texture set has bytes after its slots");
  return slots;
}

namespace {

using write_detail::Bytes;
using write_detail::Model;
using write_detail::append_int;
using write_detail::append_sized;
using write_detail::read_int;

void relocate_texture_paths(Model& model, std::uint32_t block) {
  const auto slots = parse_texture_slots(model.blocks[block]);
  Bytes rebuilt;
  append_int(rebuilt, static_cast<std::uint32_t>(slots.size()));
  bool changed = false;
  for (const auto& path : slots) {
    const auto relative = relative_texture_path(path);
    changed = changed || !relative.empty();
    append_sized(rebuilt, relative.empty() ? path : relative);
  }
  if (changed) model.blocks[block] = rebuilt;
}

void refit_bounding_sphere(const NifFile& nif, Model& model, const Shape& shape) {
  const auto fitted = fitted_bounding_sphere(shape);
  if (!fitted) {
    throw parse_error(nif.blocks()[shape.block].offset,
                      "bounding sphere of shape '" + shape.name + "' cannot be represented as finite floats");
  }
  const std::vector<float> sphere(fitted->begin(), fitted->end());
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
