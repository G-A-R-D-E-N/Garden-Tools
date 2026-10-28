#include "fo4/nif/nif_fix.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "import_mesh.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using import_detail::Mesh;
using write_detail::Bytes;
using write_detail::Model;
using write_detail::append_int;
using write_detail::append_sized;
using write_detail::half_bits;
using write_detail::intern_string;
using write_detail::unit_byte;
using write_detail::write_int;

constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_full_precision = 1U << 10;
constexpr std::size_t k_full_head = 16;
constexpr std::size_t k_half_head = 8;
constexpr std::size_t k_full_bitangent_x = 12;
constexpr std::size_t k_half_bitangent_x = 6;
constexpr std::size_t k_uv_bytes = 4;
constexpr std::size_t k_extra_count_at = 4;
constexpr std::size_t k_extra_refs_at = 8;
constexpr std::size_t k_shader_name_at = 4;
constexpr std::string_view k_blanks = " \t\r\n";
constexpr std::string_view k_bsx_type = "BSXFlags";
constexpr std::string_view k_bsx_name = "BSX";
constexpr std::string_view k_shader_type = "BSLightingShaderProperty";
constexpr std::string_view k_texture_set_type = "BSShaderTextureSet";

struct VertexRun {
  std::size_t first;
  std::size_t stride;
  std::uint32_t flags;
};

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
  cursor.read_u32();
  return {cursor.position(), static_cast<std::size_t>(descriptor & 0xFU) * 4,
          static_cast<std::uint32_t>(descriptor >> 44U)};
}

Mesh shape_mesh(const Shape& shape) {
  Mesh mesh;
  for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
    mesh.positions.push_back({shape.verts[v], shape.verts[v + 1], shape.verts[v + 2]});
  }
  for (std::size_t v = 0; v + 2 < shape.normals.size(); v += 3) {
    mesh.normals.push_back({shape.normals[v], shape.normals[v + 1], shape.normals[v + 2]});
  }
  for (std::size_t v = 0; v + 1 < shape.uvs.size(); v += 2) {
    mesh.uvs.push_back({shape.uvs[v], shape.uvs[v + 1]});
  }
  mesh.tris.assign(shape.tris.begin(), shape.tris.end());
  return mesh;
}

void rewrite_tangent_frame(const NifFile& nif, Model& model, const Shape& shape) {
  if (shape.normals.empty() || shape.tangents.empty() || shape.uvs.empty()) return;
  const auto run = vertex_run(nif, shape.block);
  const bool full = (run.flags & k_full_precision) != 0;
  const std::size_t uv2_bytes = (run.flags & k_has_uv2) != 0 ? k_uv_bytes : std::size_t{0};
  auto mesh = shape_mesh(shape);
  import_detail::add_tangent_frame(mesh);
  auto& bytes = model.blocks[shape.block];
  for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
    const auto vertex = run.first + v * run.stride;
    const auto& tangent = mesh.tangents[v];
    const auto& bitangent = mesh.bitangents[v];
    if (full) {
      write_int(bytes, vertex + k_full_bitangent_x, std::bit_cast<std::uint32_t>(bitangent[0]));
    } else {
      write_int(bytes, vertex + k_half_bitangent_x, half_bits(bitangent[0]));
    }
    const auto normal = vertex + (full ? k_full_head : k_half_head) + k_uv_bytes + uv2_bytes;
    bytes[normal + 3] = std::byte{unit_byte(bitangent[1])};
    for (std::size_t k = 0; k < 3; ++k) bytes[normal + 4 + k] = std::byte{unit_byte(tangent[k])};
    bytes[normal + 7] = std::byte{unit_byte(bitangent[2])};
  }
}

std::string_view trim_blanks(std::string_view text) {
  const auto first = text.find_first_not_of(k_blanks);
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(k_blanks) - first + 1);
}

void trim_shader_name(Model& model, std::uint32_t block) {
  binary::Cursor cursor(model.blocks[block], 0);
  cursor.read_u32();
  const auto index = cursor.read_i32();
  if (index < 0 || static_cast<std::size_t>(index) >= model.strings.size()) return;
  const std::string name = model.strings[static_cast<std::size_t>(index)];
  const auto trimmed = trim_blanks(name);
  if (trimmed.size() == name.size()) return;
  write_int(model.blocks[block], k_shader_name_at, intern_string(model, std::string(trimmed)));
}

void trim_texture_set(Model& model, std::uint32_t block) {
  binary::Cursor cursor(model.blocks[block], 0);
  const auto count = cursor.read_u32();
  Bytes trimmed;
  append_int(trimmed, count);
  bool changed = false;
  for (std::uint32_t slot = 0; slot < count; ++slot) {
    const auto text = cursor.read_bytes(cursor.read_u32());
    const std::string_view path(reinterpret_cast<const char*>(text.data()), text.size());
    const auto kept = trim_blanks(path);
    changed = changed || kept.size() != path.size();
    append_sized(trimmed, kept);
  }
  if (!cursor.at_end()) throw parse_error(cursor.absolute_offset(), "texture set has bytes after its slots");
  if (changed) model.blocks[block] = trimmed;
}

std::vector<std::int32_t> extra_refs(const Model& model, std::uint32_t block) {
  binary::Cursor cursor(model.blocks[block], 0);
  cursor.read_i32();
  const auto count = cursor.read_u32();
  std::vector<std::int32_t> refs;
  for (std::uint32_t i = 0; i < count; ++i) refs.push_back(cursor.read_i32());
  return refs;
}

bool has_bsx(const Model& model, std::uint32_t root) {
  const auto refs = extra_refs(model, root);
  return std::any_of(refs.begin(), refs.end(), [&model](std::int32_t ref) {
    return ref >= 0 && static_cast<std::size_t>(ref) < model.blocks.size() &&
           write_detail::block_type(model, static_cast<std::uint32_t>(ref)) == k_bsx_type;
  });
}

std::uint16_t type_index(Model& model, std::string_view type) {
  auto found = std::find(model.block_types.begin(), model.block_types.end(), std::string(type));
  if (found == model.block_types.end()) {
    model.block_types.emplace_back(type);
    found = model.block_types.end() - 1;
  }
  return static_cast<std::uint16_t>(found - model.block_types.begin());
}

void attach_bsx(Model& model, std::uint32_t root) {
  const auto type = type_index(model, k_bsx_type);
  Bytes flags;
  append_int(flags, intern_string(model, std::string(k_bsx_name)));
  append_int(flags, std::uint32_t{0});
  model.blocks.push_back(std::move(flags));
  model.block_type_index.push_back(type);
  Bytes ref;
  append_int(ref, static_cast<std::int32_t>(model.blocks.size() - 1));
  const auto count = extra_refs(model, root).size();
  auto& bytes = model.blocks[root];
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(k_extra_refs_at + 4 * count), ref.begin(), ref.end());
  write_int(bytes, k_extra_count_at, static_cast<std::uint32_t>(count + 1));
}

void require_same_shapes(const NifFile& nif, const Bytes& bytes) {
  const auto before = read_shapes(nif);
  const auto after = read_shapes(read_nif(bytes));
  if (before.size() != after.size()) {
    throw std::runtime_error("internal error: the fixed NIF has a different shape count");
  }
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (after[i].verts.size() != before[i].verts.size() || after[i].tris.size() != before[i].tris.size() ||
        !after[i].warning.empty()) {
      throw std::runtime_error("internal error: the fixed NIF does not read back as the same shapes");
    }
  }
}

}  // namespace

std::vector<std::byte> fix_nif(const NifFile& nif) {
  if (nif.roots().empty()) throw parse_error(0, "NIF has no root block");
  const auto root = nif.roots().front();
  if (nif.node_for_block(root) < 0) {
    throw parse_error(nif.blocks().at(root).offset, "root block is not a node, so BSXFlags cannot be attached");
  }
  auto model = write_detail::load_model(nif);
  for (const auto& shape : read_shapes(nif)) rewrite_tangent_frame(nif, model, shape);
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    const auto& type = nif.blocks()[block].type;
    if (type == k_shader_type) trim_shader_name(model, block);
    if (type == k_texture_set_type) trim_texture_set(model, block);
  }
  if (!has_bsx(model, root)) attach_bsx(model, root);
  auto bytes = write_detail::serialize(model);
  require_same_shapes(nif, bytes);
  return bytes;
}

}  // namespace fo4::nif
