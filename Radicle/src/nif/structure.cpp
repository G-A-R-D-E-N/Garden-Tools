#include "fo4/nif/structure.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/shader.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"
#include "write_refs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using edit_detail::k_alpha_type;
using edit_detail::k_lighting_type;
using edit_detail::k_texture_set_type;
using edit_detail::share_or_copy;
using edit_detail::texture_set_bytes;
using write_detail::Bytes;
using write_detail::ChildList;
using write_detail::Model;
using write_detail::append_int;
using write_detail::child_list;
using write_detail::int_at;
using write_detail::ref_offsets;
using write_detail::uint_at;
using write_detail::write_int;

constexpr std::string_view k_tri_shape = "BSTriShape";
constexpr std::string_view k_mesh_lod_tri_shape = "BSMeshLODTriShape";
constexpr std::size_t k_shape_shader_at = 92;
constexpr std::size_t k_shape_alpha_at = 96;

struct Referrer {
  std::uint32_t holder{0};
  std::size_t offset{0};
  bool child{false};
};

std::string named(const Model& model, std::uint32_t block) {
  return "block " + std::to_string(block) + " (" + std::string(write_detail::block_type(model, block)) + ")";
}

void require_block(const Model& model, std::uint32_t block) {
  if (block >= model.blocks.size()) throw parse_error(0, "block " + std::to_string(block) + " is not in the file");
}

bool is_root(const Model& model, std::uint32_t block) {
  return std::find(model.roots.begin(), model.roots.end(), static_cast<std::int32_t>(block)) != model.roots.end();
}

ChildList require_child_list(const Model& model, std::uint32_t block) {
  ref_offsets(model, block);
  const auto list = child_list(model, block);
  if (!list) throw parse_error(0, named(model, block) + " has no child list, so it cannot hold children");
  return *list;
}

std::vector<Referrer> referrers(const Model& model, std::uint32_t target) {
  std::vector<Referrer> found;
  for (std::uint32_t holder = 0; holder < model.blocks.size(); ++holder) {
    const auto list = child_list(model, holder);
    for (const auto offset : ref_offsets(model, holder)) {
      if (int_at(model.blocks[holder], offset) != static_cast<std::int32_t>(target)) continue;
      const bool child = list && offset >= list->first && offset < list->first + 4 * list->count;
      found.push_back(Referrer{holder, offset, child});
    }
  }
  return found;
}

void unlink_child(Model& model, std::uint32_t holder, std::size_t offset) {
  const auto list = *child_list(model, holder);
  auto& bytes = model.blocks[holder];
  bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4));
  write_int(bytes, list.count_at, static_cast<std::uint32_t>(list.count - 1));
}

void link_child(Model& model, std::uint32_t holder, std::uint32_t child) {
  const auto list = *child_list(model, holder);
  auto& bytes = model.blocks[holder];
  Bytes ref;
  append_int(ref, static_cast<std::int32_t>(child));
  const auto at = list.first + 4 * list.count;
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), ref.begin(), ref.end());
  write_int(bytes, list.count_at, static_cast<std::uint32_t>(list.count + 1));
}

std::uint16_t type_index(Model& model, std::string_view type) {
  auto found = std::find(model.block_types.begin(), model.block_types.end(), std::string(type));
  if (found == model.block_types.end()) {
    model.block_types.emplace_back(type);
    found = model.block_types.end() - 1;
  }
  return static_cast<std::uint16_t>(found - model.block_types.begin());
}

std::uint32_t append_block(Model& model, std::string_view type, Bytes bytes) {
  model.blocks.push_back(std::move(bytes));
  model.block_type_index.push_back(type_index(model, type));
  return static_cast<std::uint32_t>(model.blocks.size() - 1);
}

void drop_unused_type(Model& model, std::uint16_t index) {
  const auto& indices = model.block_type_index;
  if (std::find(indices.begin(), indices.end(), index) != indices.end()) return;
  model.block_types.erase(model.block_types.begin() + index);
  for (auto& value : model.block_type_index) {
    if (value > index) --value;
  }
}

std::int32_t renumber(std::int32_t value, std::uint32_t removed, std::size_t count) {
  if (value < 0) return value;
  if (static_cast<std::size_t>(value) >= count) throw parse_error(0, "reference lies outside the block table");
  if (static_cast<std::uint32_t>(value) == removed) throw parse_error(0, "a block still references the removed block");
  return value > static_cast<std::int32_t>(removed) ? value - 1 : value;
}

void erase_block(Model& model, std::uint32_t block) {
  const auto count = model.blocks.size();
  const auto type = model.block_type_index[block];
  model.blocks.erase(model.blocks.begin() + static_cast<std::ptrdiff_t>(block));
  model.block_type_index.erase(model.block_type_index.begin() + static_cast<std::ptrdiff_t>(block));
  for (std::uint32_t holder = 0; holder < model.blocks.size(); ++holder) {
    for (const auto offset : ref_offsets(model, holder)) {
      auto& bytes = model.blocks[holder];
      write_int(bytes, offset, renumber(int_at(bytes, offset), block, count));
    }
  }
  for (auto& root : model.roots) root = renumber(root, block, count);
  drop_unused_type(model, type);
}

bool reaches(const Model& model, std::uint32_t from, std::uint32_t target) {
  std::vector<std::uint32_t> pending{from};
  std::vector<bool> seen(model.blocks.size(), false);
  while (!pending.empty()) {
    const auto block = pending.back();
    pending.pop_back();
    if (block == target) return true;
    if (seen[block]) continue;
    seen[block] = true;
    const auto list = child_list(model, block);
    if (!list) continue;
    for (std::size_t i = 0; i < list->count; ++i) {
      const auto child = int_at(model.blocks[block], list->first + 4 * i);
      if (child >= 0 && static_cast<std::size_t>(child) < model.blocks.size()) {
        pending.push_back(static_cast<std::uint32_t>(child));
      }
    }
  }
  return false;
}

std::vector<std::byte> finish(const Model& model) {
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace

std::vector<std::byte> add_block(const NifFile& nif, std::uint32_t parent, std::string_view type,
                                 std::span<const std::byte> payload) {
  auto model = write_detail::load_model(nif);
  require_block(model, parent);
  require_child_list(model, parent);
  const auto added = static_cast<std::uint32_t>(model.blocks.size());
  append_block(model, type, Bytes(payload.begin(), payload.end()));
  for (const auto offset : ref_offsets(model, added)) {
    const auto value = int_at(model.blocks[added], offset);
    if (value < -1 || value >= static_cast<std::int32_t>(added)) {
      throw parse_error(0, "new " + std::string(type) + " block names block " + std::to_string(value) +
                               ", which is not an earlier block");
    }
  }
  link_child(model, parent, added);
  return finish(model);
}

std::vector<std::byte> remove_block(const NifFile& nif, std::uint32_t block) {
  auto model = write_detail::load_model(nif);
  require_block(model, block);
  if (is_root(model, block)) throw parse_error(0, named(model, block) + " is a root, so it cannot be removed");
  if (const auto list = child_list(model, block); list && list->count != 0) {
    throw parse_error(0, named(model, block) + " still has " + std::to_string(list->count) +
                             " children; reparent or remove them first");
  }
  auto refs = referrers(model, block);
  for (const auto& ref : refs) {
    if (!ref.child) {
      throw parse_error(0, named(model, block) + " is still referenced by " + named(model, ref.holder) +
                               ", so it cannot be removed");
    }
  }
  std::sort(refs.begin(), refs.end(), [](const Referrer& a, const Referrer& b) {
    return a.holder != b.holder ? a.holder > b.holder : a.offset > b.offset;
  });
  for (const auto& ref : refs) unlink_child(model, ref.holder, ref.offset);
  erase_block(model, block);
  return finish(model);
}

std::vector<std::byte> reparent_block(const NifFile& nif, std::uint32_t block, std::uint32_t parent) {
  auto model = write_detail::load_model(nif);
  require_block(model, block);
  require_block(model, parent);
  if (is_root(model, block)) throw parse_error(0, named(model, block) + " is a root, so it has no parent to leave");
  const auto refs = referrers(model, block);
  for (const auto& ref : refs) {
    if (!ref.child) {
      throw parse_error(0, named(model, block) + " is still referenced by " + named(model, ref.holder) +
                               ", so it cannot be reparented");
    }
  }
  if (refs.size() != 1) {
    throw parse_error(0, named(model, block) + " has " + std::to_string(refs.size()) +
                             " parents, so it cannot be reparented");
  }
  require_child_list(model, parent);
  if (reaches(model, block, parent)) {
    throw parse_error(0, named(model, parent) + " lies inside " + named(model, block) +
                             ", so the move would make a cycle");
  }
  unlink_child(model, refs.front().holder, refs.front().offset);
  link_child(model, parent, block);
  return finish(model);
}

std::vector<std::byte> attach_alpha_property(const NifFile& nif, std::uint32_t shape, std::uint16_t flags,
                                             std::uint8_t threshold) {
  auto model = write_detail::load_model(nif);
  require_block(model, shape);
  ref_offsets(model, shape);
  const auto type = write_detail::block_type(model, shape);
  if (type != k_tri_shape && type != k_mesh_lod_tri_shape) {
    throw parse_error(0, named(model, shape) + " has no alpha slot, so it cannot take an alpha property");
  }
  const auto alpha_at = k_shape_alpha_at + 4 * std::size_t{uint_at(model.blocks[shape], 4)};
  if (int_at(model.blocks[shape], alpha_at) >= 0) {
    throw parse_error(0, named(model, shape) + " already has an alpha property");
  }
  Bytes bytes;
  append_int(bytes, std::int32_t{-1});
  append_int(bytes, std::uint32_t{0});
  append_int(bytes, std::int32_t{-1});
  append_int(bytes, flags);
  append_int(bytes, threshold);
  const auto alpha = append_block(model, k_alpha_type, std::move(bytes));
  write_int(model.blocks[shape], alpha_at, static_cast<std::int32_t>(alpha));
  return finish(model);
}

std::vector<std::byte> create_texture_set(const NifFile& nif, std::uint32_t shape,
                                          const std::vector<std::string>& textures) {
  if (textures.empty()) throw parse_error(0, "a texture set needs at least one texture path");
  auto model = write_detail::load_model(nif);
  require_block(model, shape);
  ref_offsets(model, shape);
  const auto type = write_detail::block_type(model, shape);
  if (type != k_tri_shape && type != k_mesh_lod_tri_shape) {
    throw parse_error(0, named(model, shape) + " has no shader slot, so it cannot take a texture set");
  }
  const auto shader_at = k_shape_shader_at + 4 * std::size_t{uint_at(model.blocks[shape], 4)};
  const auto shader = int_at(model.blocks[shape], shader_at);
  if (shader < 0 || static_cast<std::size_t>(shader) >= model.blocks.size()) {
    throw parse_error(0, named(model, shape) + " has no shader property");
  }
  const auto shader_index = static_cast<std::uint32_t>(shader);
  if (write_detail::block_type(model, shader_index) != k_lighting_type) {
    throw parse_error(0, "shape's shader is " + std::string(write_detail::block_type(model, shader_index)) +
                             ", which has no texture set slot");
  }
  const auto lighting = read_lighting_shader(nif, shader_index);
  if (lighting.texture_set_ref >= 0) throw parse_error(0, "shape's shader already has a texture set");
  const auto target = share_or_copy(nif, model, shape, shader_at, referrers(model, shader_index).size());
  const auto set = append_block(model, k_texture_set_type, texture_set_bytes(textures));
  write_int(model.blocks[target], lighting.offsets.texture_set, static_cast<std::int32_t>(set));
  return finish(model);
}

}  // namespace fo4::nif
