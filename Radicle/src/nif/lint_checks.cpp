#include "fo4/nif/lint.hpp"

#include "fo4/error.hpp"
#include "lint_internal.hpp"
#include "nif_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

// name, extra-data count, controller, flags, transform and collision reference come before the sphere
constexpr std::size_t k_sphere_fixed_bytes = 72;
constexpr std::string_view k_textures_folder = "textures";
constexpr std::string_view k_sub_index_type = "BSSubIndexTriShape";
constexpr std::size_t k_segment_head_bytes = 12;
constexpr std::size_t k_subsegment_bytes = 16;
constexpr std::size_t k_per_segment_head_bytes = 8;

bool is_separator(char ch) { return ch == '\\' || ch == '/'; }

bool is_textures_folder(std::string_view segment) {
  return segment.size() == k_textures_folder.size() &&
         std::equal(segment.begin(), segment.end(), k_textures_folder.begin(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

std::string where(const Shape& shape) {
  return "shape '" + shape.name + "' (block " + std::to_string(shape.block) + ")";
}

std::string leaf_name(std::string_view path) {
  const auto cut = path.find_last_of("\\/");
  return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
}

std::string relocatable_texture_fault(const std::vector<Shape>& shapes) {
  for (const auto& shape : shapes) {
    for (const auto& path : shape.textures) {
      const auto relative = relative_texture_path(path);
      if (!relative.empty()) {
        return where(shape) + " texture '" + path + "' can be made relative to '" + relative + "'";
      }
    }
  }
  return {};
}

std::string sphere_fault(const NifFile& nif, const std::vector<Shape>& shapes) {
  for (const auto& shape : shapes) {
    if (!shape.verts.empty() && bounding_sphere_misses(nif, shape)) {
      return where(shape) + " has no bounding sphere";
    }
  }
  return {};
}

// Constraint: collinear and coincident triangles with distinct vertices are common in vanilla, so only index repeats are judged.
std::string repeated_vertex_fault(const NifFile& nif, const std::vector<Shape>& shapes) {
  for (const auto& shape : shapes) {
    if (!repairs_triangles(nif, shape)) continue;
    std::size_t count = 0;
    for (std::size_t triangle = 0; triangle < shape.tris.size() / 3; ++triangle) {
      if (repeats_a_vertex(shape, triangle)) ++count;
    }
    if (count > 0) return where(shape) + " has " + std::to_string(count) + " triangle(s) that name a vertex twice";
  }
  return {};
}

std::string unused_string_fault(const NifFile& nif) {
  const auto& strings = nif.strings();
  std::vector<bool> used(strings.size(), false);
  for (const auto& block : nif.blocks()) {
    const auto end = block.offset + block.size;
    for (std::size_t at = block.offset; at + 4 <= end; ++at) {
      const auto value = write_detail::read_int<std::uint32_t>(nif.bytes(), at);
      if (value < used.size()) used[value] = true;
    }
  }
  for (std::size_t index = 0; index < strings.size(); ++index) {
    if (!used[index] && !strings[index].empty()) {
      return "string " + std::to_string(index) + " '" + strings[index] + "' is used by no block";
    }
  }
  return {};
}

// Requires a BSSubIndexTriShape block.
std::string segment_sidecar(const NifFile& nif, std::uint32_t block) {
  const auto run = vertex_run(nif, block);
  if (run.data_size == 0) return {};
  auto cursor = detail::block_cursor(nif, block);
  cursor.seek(run.first + run.data_size);
  cursor.read_u32();
  const auto segments = cursor.read_u32();
  const auto total = cursor.read_u32();
  for (std::uint32_t segment = 0; segment < segments; ++segment) {
    cursor.read_bytes(k_segment_head_bytes);
    const auto subsegments = cursor.read_u32();
    cursor.read_bytes(k_subsegment_bytes * static_cast<std::size_t>(subsegments));
  }
  if (segments >= total) return {};
  const auto shared_segments = cursor.read_u32();
  const auto shared_total = cursor.read_u32();
  cursor.read_bytes(4 * static_cast<std::size_t>(shared_segments));
  for (std::uint32_t entry = 0; entry < shared_total; ++entry) {
    cursor.read_bytes(k_per_segment_head_bytes);
    const auto cut_offsets = cursor.read_u32();
    cursor.read_bytes(4 * static_cast<std::size_t>(cut_offsets));
  }
  const auto length = cursor.read_u16();
  const auto text = cursor.read_bytes(length);
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// Constraint: a sidecar that is not present passes, because vanilla names sidecars it does not ship.
std::string sidecar_fault(const NifFile& nif, const std::vector<Shape>& shapes, const SidecarProbe& readable) {
  if (!readable) return {};
  for (const auto& shape : shapes) {
    if (nif.blocks()[shape.block].type != k_sub_index_type) continue;
    std::string sidecar;
    try {
      sidecar = segment_sidecar(nif, shape.block);
    } catch (const parse_error& error) {
      return where(shape) + " segment data does not parse: " + error.parse_message();
    }
    if (!sidecar.empty() && !readable(leaf_name(sidecar))) {
      return where(shape) + " names sidecar '" + sidecar + "', which cannot be read beside the mesh";
    }
  }
  return {};
}

}  // namespace

std::size_t bounding_sphere_offset(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  cursor.read_i32();
  return k_sphere_fixed_bytes + 4 * static_cast<std::size_t>(cursor.read_u32());
}

bool finite_position(const Shape& shape, std::size_t v) {
  return std::isfinite(shape.verts[v]) && std::isfinite(shape.verts[v + 1]) && std::isfinite(shape.verts[v + 2]);
}

bool bounding_sphere_misses(const NifFile& nif, const Shape& shape) {
  // Constraint: skinned shapes carry no authored sphere in vanilla, so they are not judged.
  if (shape.skin_ref >= 0) return false;
  auto cursor = detail::block_cursor(nif, shape.block);
  cursor.read_bytes(bounding_sphere_offset(nif, shape.block));
  std::array<float, 4> sphere{};
  for (float& value : sphere) value = cursor.read_f32();
  if (sphere[3] != 0.0f) return false;
  for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
    if (!finite_position(shape, v)) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      if (shape.verts[v + k] != sphere[k]) return true;
    }
  }
  return false;
}

std::string relative_texture_path(std::string_view path) {
  const bool absolute = (path.size() >= 2 && path[1] == ':') || (!path.empty() && is_separator(path.front()));
  if (!absolute) return {};
  for (std::size_t begin = 0; begin < path.size();) {
    const auto end = path.find_first_of("\\/", begin);
    if (end == std::string_view::npos) break;
    if (is_textures_folder(path.substr(begin, end - begin)) && end + 1 < path.size()) {
      return std::string(path.substr(begin));
    }
    begin = end + 1;
  }
  return {};
}

VertexRun vertex_run(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_av_header(cursor, nif);
  cursor.read_i32();
  cursor.read_bytes(16);
  cursor.read_bytes(12);
  const auto descriptor_low = cursor.read_u32();
  const auto descriptor_high = cursor.read_u32();
  const auto descriptor =
      static_cast<std::uint64_t>(descriptor_low) | (static_cast<std::uint64_t>(descriptor_high) << 32U);
  cursor.read_u32();
  cursor.read_u16();
  const auto data_size = cursor.read_u32();
  return {cursor.position(), static_cast<std::size_t>(descriptor & 0xFU) * 4,
          static_cast<std::uint32_t>(descriptor >> 44U), data_size};
}

bool repairs_triangles(const NifFile& nif, const Shape& shape) {
  const auto& type = nif.blocks()[shape.block].type;
  return type == "BSTriShape" || type == "BSDynamicTriShape";
}

bool repeats_a_vertex(const Shape& shape, std::size_t triangle) {
  const auto a = shape.tris[3 * triangle];
  const auto b = shape.tris[3 * triangle + 1];
  const auto c = shape.tris[3 * triangle + 2];
  return a == b || b == c || a == c;
}

std::vector<std::uint16_t> triangles_after_fix(const NifFile& nif, const Shape& shape) {
  if (!repairs_triangles(nif, shape)) return shape.tris;
  std::vector<std::uint16_t> kept;
  for (std::size_t triangle = 0; triangle < shape.tris.size() / 3; ++triangle) {
    if (repeats_a_vertex(shape, triangle)) continue;
    for (std::size_t slot = 0; slot < 3; ++slot) kept.push_back(shape.tris[3 * triangle + slot]);
  }
  if (kept.empty()) return shape.tris;
  return kept;
}

std::vector<VerifyCheck> lint_checks(const NifFile& nif, const std::vector<Shape>& shapes,
                                     const SidecarProbe& sidecar_readable) {
  const bool triangles_fixable = std::all_of(shapes.begin(), shapes.end(), [&nif](const Shape& shape) {
    if (!repairs_triangles(nif, shape)) return true;
    bool repeated = false;
    bool valid = false;
    for (std::size_t triangle = 0; triangle < shape.tris.size() / 3; ++triangle) {
      if (repeats_a_vertex(shape, triangle)) repeated = true;
      else valid = true;
    }
    return !repeated || valid;
  });
  return {
      {"absolute texture paths can be made relative", relocatable_texture_fault(shapes), true},
      {"rigid shapes carry a bounding sphere", sphere_fault(nif, shapes), true},
      {"no triangles name a vertex twice", repeated_vertex_fault(nif, shapes), triangles_fixable},
      {"no unused strings in the header string table", unused_string_fault(nif), false},
      {"named SSF sidecars that exist beside the mesh are readable", sidecar_fault(nif, shapes, sidecar_readable),
       false},
  };
}

}  // namespace fo4::nif
