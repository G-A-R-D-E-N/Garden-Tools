#include "fo4/nif/collision_attach.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using write_detail::Bytes;
using write_detail::Model;
using write_detail::append_int;
using write_detail::intern_string;
using write_detail::write_int;

constexpr std::string_view k_collision_object = "bhkNPCollisionObject";
constexpr std::string_view k_physics_system = "bhkPhysicsSystem";
constexpr std::string_view k_ragdoll_system = "bhkRagdollSystem";
constexpr std::string_view k_bsx_flags = "BSXFlags";
constexpr std::string_view k_bsx_name = "BSX";
constexpr std::string_view k_cloth_extra = "BSClothExtraData";
constexpr std::uint16_t k_collision_owner_flags = 0x80;
constexpr std::uint32_t k_havok_bsx_bit = 2;
constexpr std::size_t k_object_size = 14;
constexpr std::size_t k_object_target_at = 0;
constexpr std::size_t k_object_data_at = 6;
constexpr std::size_t k_av_collision_base = 68;

bool is_system(std::string_view type) { return type == k_physics_system || type == k_ragdoll_system; }

bool is_ref_free(std::string_view type) {
  return type == "BSShaderTextureSet" || type == "BSXFlags" || type == "NiStringExtraData" || type == "BSBound" ||
         type == "BSBehaviorGraphExtraData" || type == k_cloth_extra || is_system(type);
}

std::optional<std::size_t> trailing_bytes(std::string_view type) {
  if (type == "NiNode" || type == "BSLeafAnimNode" || type == "BSTriShape") return std::size_t{0};
  if (type == "BSMeshLODTriShape") return std::size_t{12};
  if (type == "BSMultiBoundNode") return std::size_t{8};
  if (type == "BSOrderedNode") return std::size_t{17};
  if (type == "BSValueNode") return std::size_t{5};
  if (type == "NiBillboardNode") return std::size_t{2};
  if (type == "NiSwitchNode") return std::size_t{6};
  return std::nullopt;
}

void require_size(const Bytes& bytes, std::size_t size) {
  if (bytes.size() < size) throw parse_error(0, "block is shorter than its reference layout");
}

std::uint32_t uint_at(const Bytes& bytes, std::size_t at) {
  require_size(bytes, at + 4);
  return write_detail::read_int<std::uint32_t>(bytes, at);
}

std::int32_t int_at(const Bytes& bytes, std::size_t at) { return static_cast<std::int32_t>(uint_at(bytes, at)); }

// Constraint: an offset list names every i32 block reference of a block type with a known layout; any other type is refused.
std::vector<std::size_t> ref_offsets(const Model& model, std::uint32_t block) {
  const auto type = write_detail::block_type(model, block);
  const auto& bytes = model.blocks[block];
  std::vector<std::size_t> refs;
  if (is_ref_free(type)) return refs;
  if (type == k_collision_object) {
    require_size(bytes, k_object_size);
    return {k_object_target_at, k_object_data_at};
  }
  if (type == "BSLightingShaderProperty") {
    const auto n = std::size_t{uint_at(bytes, 8)};
    require_size(bytes, 44 + 4 * n);
    for (std::size_t i = 0; i < n; ++i) refs.push_back(12 + 4 * i);
    refs.push_back(12 + 4 * n);
    refs.push_back(40 + 4 * n);
    return refs;
  }
  if (type == "NiAlphaProperty") {
    const auto n = std::size_t{uint_at(bytes, 4)};
    if (bytes.size() != 15 + 4 * n) throw parse_error(0, "NiAlphaProperty block does not match its reference layout");
    for (std::size_t i = 0; i < n; ++i) refs.push_back(8 + 4 * i);
    refs.push_back(8 + 4 * n);
    return refs;
  }
  const bool segmented = type == "BSSubIndexTriShape";
  const auto tail = segmented ? std::optional<std::size_t>{std::size_t{0}} : trailing_bytes(type);
  if (!tail) {
    throw parse_error(0, "block type " + std::string(type) +
                             " has no reference layout, so collision blocks cannot be removed");
  }
  const auto n = std::size_t{uint_at(bytes, 4)};
  require_size(bytes, 12 + 4 * n);
  for (std::size_t i = 0; i < n; ++i) refs.push_back(8 + 4 * i);
  refs.push_back(8 + 4 * n);
  refs.push_back(k_av_collision_base + 4 * n);
  std::size_t end = 0;
  if (type == "BSTriShape" || type == "BSMeshLODTriShape" || segmented) {
    end = 118 + 4 * n + std::size_t{uint_at(bytes, 114 + 4 * n)};
    refs.push_back(88 + 4 * n);
    refs.push_back(92 + 4 * n);
    refs.push_back(96 + 4 * n);
  } else {
    const auto children = std::size_t{uint_at(bytes, 72 + 4 * n)};
    require_size(bytes, 76 + 4 * n + 4 * children);
    for (std::size_t i = 0; i < children; ++i) refs.push_back(76 + 4 * n + 4 * i);
    end = 76 + 4 * n + 4 * children;
    if (type == "BSMultiBoundNode") refs.push_back(end);
  }
  // Constraint: BSSubIndexTriShape segment data follows the BSTriShape data and is not parsed, so only its minimum size is checked.
  const bool sized = segmented ? bytes.size() >= end : bytes.size() == end + *tail;
  if (!sized) {
    throw parse_error(0, "block type " + std::string(type) + " does not match its reference layout");
  }
  return refs;
}

std::string_view type_of(const Model& model, std::int32_t block) {
  if (block < 0 || static_cast<std::size_t>(block) >= model.blocks.size()) return {};
  return write_detail::block_type(model, static_cast<std::uint32_t>(block));
}

std::uint32_t find_node(const NifFile& nif, std::string_view name) {
  for (const auto& node : nif.nodes()) {
    if (node.name == name) return node.block;
  }
  throw parse_error(0, "no node named '" + std::string(name) + "' in the NIF");
}

std::size_t collision_slot(const Bytes& bytes) { return k_av_collision_base + 4 * std::size_t{uint_at(bytes, 4)}; }

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

void add_extra_ref(Model& model, std::uint32_t block, std::uint32_t extra) {
  auto& bytes = model.blocks[block];
  const auto count = uint_at(bytes, 4);
  require_size(bytes, 8 + 4 * std::size_t{count});
  Bytes ref;
  append_int(ref, static_cast<std::int32_t>(extra));
  bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(8 + 4 * std::size_t{count}), ref.begin(), ref.end());
  write_int(bytes, 4, count + 1);
}

std::vector<std::uint32_t> holders_of(const Model& model, std::uint32_t target) {
  std::vector<std::uint32_t> holders;
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    for (const auto offset : ref_offsets(model, block)) {
      if (int_at(model.blocks[block], offset) == static_cast<std::int32_t>(target)) {
        holders.push_back(block);
        break;
      }
    }
  }
  return holders;
}

bool removable(const Model& model, std::int32_t target, const std::vector<std::uint32_t>& removing) {
  if (target < 0 || static_cast<std::size_t>(target) >= model.blocks.size()) return false;
  if (std::find(model.roots.begin(), model.roots.end(), target) != model.roots.end()) return false;
  const auto holders = holders_of(model, static_cast<std::uint32_t>(target));
  return std::all_of(holders.begin(), holders.end(), [&removing](std::uint32_t holder) {
    return std::find(removing.begin(), removing.end(), holder) != removing.end();
  });
}

void retire_object(const Model& model, std::int32_t object, std::vector<std::uint32_t>& removing) {
  if (!removable(model, object, removing)) return;
  removing.push_back(static_cast<std::uint32_t>(object));
  const auto system = int_at(model.blocks[static_cast<std::size_t>(object)], k_object_data_at);
  if (is_system(type_of(model, system)) && removable(model, system, removing)) {
    removing.push_back(static_cast<std::uint32_t>(system));
  }
}

void remove_blocks(Model& model, std::vector<std::uint32_t> removed) {
  if (removed.empty()) return;
  std::sort(removed.begin(), removed.end());
  removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
  const auto count = static_cast<std::int32_t>(model.blocks.size());
  const auto renumber = [&removed, count](std::int32_t value) {
    if (value < 0) return value;
    if (value >= count) throw parse_error(0, "reference lies outside the block table");
    const auto target = static_cast<std::uint32_t>(value);
    if (std::binary_search(removed.begin(), removed.end(), target)) {
      throw parse_error(0, "a block still references a removed collision block");
    }
    const auto below = std::lower_bound(removed.begin(), removed.end(), target) - removed.begin();
    return value - static_cast<std::int32_t>(below);
  };
  for (std::uint32_t block = 0; block < model.blocks.size(); ++block) {
    if (std::binary_search(removed.begin(), removed.end(), block)) continue;
    for (const auto offset : ref_offsets(model, block)) {
      write_int(model.blocks[block], offset, renumber(int_at(model.blocks[block], offset)));
    }
  }
  for (auto& root : model.roots) root = renumber(root);
  for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
    const auto index = static_cast<std::ptrdiff_t>(*it);
    model.blocks.erase(model.blocks.begin() + index);
    model.block_type_index.erase(model.block_type_index.begin() + index);
  }
}

std::vector<std::byte> finish(const Model& model) {
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

Bytes object_bytes(std::int32_t target, std::int32_t system, std::uint32_t body_id) {
  Bytes out;
  append_int(out, target);
  append_int(out, k_collision_owner_flags);
  append_int(out, system);
  append_int(out, body_id);
  return out;
}

Bytes system_bytes(std::span<const std::byte> packfile) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(packfile.size()));
  out.insert(out.end(), packfile.begin(), packfile.end());
  return out;
}

std::optional<std::uint32_t> find_block(const NifFile& nif, std::string_view type) {
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type == type) return static_cast<std::uint32_t>(block);
  }
  return std::nullopt;
}

}  // namespace

std::vector<std::byte> set_node_collision(const NifFile& nif, std::string_view node_name,
                                          std::span<const std::byte> packfile, std::uint32_t body_id) {
  const auto system = decode_hknp_system(read_hknp_packfile(packfile));
  if (body_id >= system.bodies.size()) {
    throw std::invalid_argument("body id " + std::to_string(body_id) + " is not a body of the packfile");
  }
  auto model = write_detail::load_model(nif);
  const auto node = find_node(nif, node_name);
  const auto slot = collision_slot(model.blocks[node]);
  const auto old_object = int_at(model.blocks[node], slot);
  if (old_object >= 0 && type_of(model, old_object) != k_collision_object) {
    throw parse_error(0, "node's collision reference is not a bhkNPCollisionObject");
  }

  const auto object = static_cast<std::uint32_t>(model.blocks.size());
  const auto target = static_cast<std::int32_t>(node);
  append_block(model, k_collision_object, object_bytes(target, static_cast<std::int32_t>(object + 1), body_id));
  append_block(model, k_physics_system, system_bytes(packfile));
  write_int(model.blocks[node], slot, static_cast<std::int32_t>(object));

  std::vector<std::uint32_t> removing;
  if (old_object >= 0) retire_object(model, old_object, removing);
  remove_blocks(model, removing);
  return finish(model);
}

std::vector<std::byte> remove_node_collision(const NifFile& nif, std::string_view node_name) {
  auto model = write_detail::load_model(nif);
  const auto node = find_node(nif, node_name);
  const auto slot = collision_slot(model.blocks[node]);
  const auto object = int_at(model.blocks[node], slot);
  if (object < 0) return finish(model);
  if (type_of(model, object) != k_collision_object) {
    throw parse_error(0, "node's collision reference is not a bhkNPCollisionObject");
  }
  write_int(model.blocks[node], slot, std::int32_t{-1});
  std::vector<std::uint32_t> removing;
  retire_object(model, object, removing);
  remove_blocks(model, removing);
  return finish(model);
}

std::vector<std::byte> copy_cloth_extra_data(const NifFile& nif, std::string_view node_name, const NifFile& donor) {
  const auto source = find_block(donor, k_cloth_extra);
  if (!source) throw parse_error(0, "donor NIF has no BSClothExtraData block");
  const auto& info = donor.blocks()[*source];
  const auto slice = std::span<const std::byte>(donor.bytes()).subspan(info.offset, info.size);
  Bytes block(slice.begin(), slice.end());
  auto cursor = detail::block_cursor(donor, *source);
  const auto name = cursor.read_i32();
  if (name >= 0 && static_cast<std::size_t>(name) >= donor.strings().size()) {
    throw parse_error(info.offset, "donor BSClothExtraData names a string outside the string table");
  }

  auto model = write_detail::load_model(nif);
  const auto node = find_node(nif, node_name);
  const auto name_index = name >= 0 ? intern_string(model, donor.strings()[static_cast<std::size_t>(name)])
                                    : std::int32_t{-1};
  write_int(block, 0, name_index);
  const auto cloth = append_block(model, k_cloth_extra, std::move(block));
  add_extra_ref(model, node, cloth);
  return finish(model);
}

std::vector<std::byte> set_havok_flag(const NifFile& nif, bool enabled) {
  if (nif.roots().empty()) throw parse_error(0, "NIF has no root block");
  const auto root = nif.roots().front();
  if (nif.node_for_block(root) < 0) {
    throw parse_error(nif.blocks()[root].offset, "root block is not a node, so its BSXFlags cannot be set");
  }
  auto model = write_detail::load_model(nif);
  std::optional<std::uint32_t> flags_block;
  const auto count = uint_at(model.blocks[root], 4);
  for (std::size_t i = 0; i < count; ++i) {
    const auto ref = int_at(model.blocks[root], 8 + 4 * i);
    if (type_of(model, ref) == k_bsx_flags) {
      flags_block = static_cast<std::uint32_t>(ref);
      break;
    }
  }
  if (flags_block) {
    auto& bytes = model.blocks[*flags_block];
    const auto flags = uint_at(bytes, 4);
    write_int(bytes, 4, enabled ? (flags | k_havok_bsx_bit) : (flags & ~k_havok_bsx_bit));
  } else if (enabled) {
    Bytes block;
    append_int(block, intern_string(model, std::string(k_bsx_name)));
    append_int(block, k_havok_bsx_bit);
    add_extra_ref(model, root, append_block(model, k_bsx_flags, std::move(block)));
  }
  return finish(model);
}

}  // namespace fo4::nif
