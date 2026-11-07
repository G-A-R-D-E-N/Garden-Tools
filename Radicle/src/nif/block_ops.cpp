#include "fo4/nif/block_ops.hpp"

#include "fo4/error.hpp"
#include "nif_internal.hpp"
#include "write_edit_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_ref_bytes = 4;


std::size_t child_count_at(const NifFile& nif, std::uint32_t node) {
  auto cursor = detail::block_cursor(nif, node);
  detail::read_av_header(cursor, nif);
  cursor.read_i32();
  return cursor.absolute_offset();
}

std::size_t child_count_local(const NifFile& nif, std::uint32_t node) {
  return child_count_at(nif, node) - nif.blocks()[node].offset;
}

std::size_t disk_child_count(const NifFile& nif, std::uint32_t node) {
  const auto count_at = child_count_at(nif, node);
  const auto& info = nif.blocks()[node];
  if (count_at + k_ref_bytes > info.offset + info.size) {
    throw parse_error(info.offset, "node block is too short for its child count");
  }
  return static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(nif.bytes(), count_at));
}

std::size_t child_array_end(const NifFile& nif, std::uint32_t node) {
  const auto count = disk_child_count(nif, node);
  const auto count_at = child_count_at(nif, node);
  const auto& info = nif.blocks()[node];
  if (count > (info.offset + info.size - count_at - k_ref_bytes) / k_ref_bytes) {
    throw parse_error(info.offset, "node child list runs past the end of its block");
  }
  return count_at + k_ref_bytes + count * k_ref_bytes;
}

void require_node_without_tail(const NifFile& nif, std::uint32_t node) {
  if (node >= nif.blocks().size()) throw parse_error(0, "block " + std::to_string(node) + " is not in the file");
  const auto& info = nif.blocks()[node];
  if (nif.node_for_block(node) < 0) throw parse_error(info.offset, "block " + std::to_string(node) + " is not a node");
  if (child_array_end(nif, node) != info.offset + info.size) {
    throw parse_error(info.offset, "node " + std::to_string(node) + " has data after its child list");
  }
}

std::vector<std::int32_t> disk_children(const NifFile& nif, std::uint32_t node) {
  const auto count = disk_child_count(nif, node);
  const auto array_at = child_count_at(nif, node) + k_ref_bytes;
  std::vector<std::int32_t> slots;
  slots.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    slots.push_back(write_detail::read_int<std::int32_t>(nif.bytes(), array_at + i * k_ref_bytes));
  }
  return slots;
}

std::string block_name(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  if (nif.node_for_block(block) < 0 && !edit_detail::is_shape_type(info.type)) return {};
  if (info.size < k_ref_bytes) return {};
  auto cursor = detail::block_cursor(nif, block);
  const auto index = cursor.read_i32();
  if (index < 0 || static_cast<std::size_t>(index) >= nif.strings().size()) return {};
  return nif.strings()[static_cast<std::size_t>(index)];
}

bool is_root(const NifFile& nif, std::uint32_t block) {
  return std::find(nif.roots().begin(), nif.roots().end(), block) != nif.roots().end();
}

bool is_ancestor(const NifFile& nif, std::uint32_t ancestor, std::uint32_t node) {
  auto current = nif.parent(node);
  while (current >= 0) {
    if (static_cast<std::uint32_t>(current) == ancestor) return true;
    current = nif.parent(static_cast<std::uint32_t>(current));
  }
  return false;
}

std::vector<std::byte> serialized(const write_detail::Model& model) {
  auto out = write_detail::serialize(model);
  read_nif(out);
  return out;
}

}  // namespace

std::vector<std::byte> sort_children_by_name(const NifFile& nif, std::uint32_t node) {
  require_node_without_tail(nif, node);
  const auto slots = disk_children(nif, node);
  std::vector<std::pair<std::string, std::uint32_t>> ordered;
  for (const auto slot : slots) {
    if (slot < 0) continue;
    if (static_cast<std::size_t>(slot) >= nif.blocks().size()) {
      throw parse_error(nif.blocks()[node].offset, "node child reference is out of range");
    }
    ordered.emplace_back(block_name(nif, static_cast<std::uint32_t>(slot)), static_cast<std::uint32_t>(slot));
  }
  if (ordered.size() < 2) {
    throw parse_error(nif.blocks()[node].offset, "node " + std::to_string(node) + " has fewer than two children");
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    if (left.first != right.first) return left.first < right.first;
    return left.second < right.second;
  });
  auto model = write_detail::load_model(nif);
  const auto array_at = child_count_local(nif, node) + k_ref_bytes;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const auto value = i < ordered.size() ? static_cast<std::int32_t>(ordered[i].second) : -1;
    write_detail::write_int(model.blocks[node], array_at + i * k_ref_bytes, value);
  }
  return serialized(model);
}

std::vector<std::byte> append_child(const NifFile& nif, std::uint32_t node, std::uint32_t child) {
  require_node_without_tail(nif, node);
  if (child >= nif.blocks().size()) throw parse_error(0, "block " + std::to_string(child) + " is not in the file");
  const auto& child_info = nif.blocks()[child];
  if (child == node) throw parse_error(nif.blocks()[node].offset, "a node cannot be its own child");
  if (!edit_detail::is_shape_type(child_info.type) && nif.node_for_block(child) < 0) {
    throw parse_error(child_info.offset, "block " + std::to_string(child) + " is not a node or a shape");
  }
  if (nif.parent(child) >= 0) {
    throw parse_error(child_info.offset, "block " + std::to_string(child) + " already has a parent");
  }
  if (is_root(nif, child)) {
    throw parse_error(child_info.offset, "block " + std::to_string(child) + " is already a file root");
  }
  if (is_ancestor(nif, child, node)) {
    throw parse_error(child_info.offset, "appending block " + std::to_string(child) + " would create a cycle");
  }
  auto model = write_detail::load_model(nif);
  const auto count_local = child_count_local(nif, node);
  const auto count = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(model.blocks[node], count_local));
  auto grown = model.blocks[node];
  grown.resize(grown.size() + k_ref_bytes);
  write_detail::write_int(grown, grown.size() - k_ref_bytes, static_cast<std::int32_t>(child));
  write_detail::write_int(grown, count_local, static_cast<std::uint32_t>(count + 1));
  model.blocks[node] = std::move(grown);
  return serialized(model);
}

std::vector<std::byte> duplicate_block(const NifFile& nif, std::uint32_t block) {
  if (block >= nif.blocks().size()) throw parse_error(0, "block " + std::to_string(block) + " is not in the file");
  const auto& info = nif.blocks()[block];
  const auto node_index = nif.node_for_block(block);
  if (node_index >= 0 && !nif.children(block).empty()) {
    throw parse_error(info.offset, "node " + std::to_string(block) + " has children, so a copy would share them");
  }
  if (node_index < 0 && !edit_detail::is_shape_type(info.type)) {
    throw parse_error(info.offset, "block " + std::to_string(block) + " is not a node or a shape");
  }
  auto model = write_detail::load_model(nif);
  model.blocks.push_back(model.blocks[block]);
  model.block_type_index.push_back(model.block_type_index[block]);
  return serialized(model);
}

}  // namespace fo4::nif
