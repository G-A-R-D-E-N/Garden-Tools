#include "fo4/nif/connect_points.hpp"

#include "connect_points_internal.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace connect_detail {
namespace {

// Invariant: a point is at least 40 bytes (two sized strings, a quaternion, a vector and a scale), so a larger count cannot fit.
constexpr std::size_t k_point_min_bytes = 40;
constexpr std::size_t k_child_min_bytes = 4;
constexpr std::size_t k_name_bytes = 4;

std::string read_sized(binary::Cursor& cursor) {
  const auto length = cursor.read_u32();
  const auto bytes = cursor.read_bytes(length);
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void read_floats(binary::Cursor& cursor, float* values, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) values[i] = cursor.read_f32();
}

void append_float(write_detail::Bytes& out, float value) {
  write_detail::append_int(out, std::bit_cast<std::uint32_t>(value));
}

void append_floats(write_detail::Bytes& out, const float* values, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) append_float(out, values[i]);
}

void check_count(const binary::Cursor& cursor, std::uint32_t count, std::size_t minimum) {
  if (count > cursor.remaining() / minimum) {
    throw parse_error(cursor.absolute_offset(), "connect point count is larger than the block");
  }
}

}  // namespace

std::span<const std::byte> block_bytes(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  return std::span<const std::byte>(nif.bytes()).subspan(info.offset, info.size);
}

void require_block(const NifFile& nif, std::uint32_t block, std::string_view type) {
  if (block >= nif.blocks().size()) {
    throw parse_error(0, "block " + std::to_string(block) + " is not in the file");
  }
  const auto& info = nif.blocks()[block];
  if (info.type != type) {
    throw parse_error(info.offset, "block " + std::to_string(block) + " is " + info.type + ", not " + std::string(type));
  }
}

ConnectParents parse_parents(binary::Cursor& cursor, const NifFile& nif) {
  ConnectParents value;
  value.name = detail::read_string_ref(cursor, nif);
  const auto count = cursor.read_u32();
  check_count(cursor, count, k_point_min_bytes);
  value.points.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    ConnectPoint point;
    point.parent = read_sized(cursor);
    point.name = read_sized(cursor);
    read_floats(cursor, point.rotation, 4);
    read_floats(cursor, point.translation, 3);
    point.scale = cursor.read_f32();
    value.points.push_back(std::move(point));
  }
  return value;
}

ConnectChildren parse_children(binary::Cursor& cursor, const NifFile& nif) {
  ConnectChildren value;
  value.name = detail::read_string_ref(cursor, nif);
  value.skinned = cursor.read_u8() != 0;
  const auto count = cursor.read_u32();
  check_count(cursor, count, k_child_min_bytes);
  value.names.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) value.names.push_back(read_sized(cursor));
  return value;
}

std::vector<std::byte> encode_parents(const NifFile& nif, std::uint32_t block, const ConnectParents& value) {
  const auto original = block_bytes(nif, block);
  binary::Cursor cursor(original);
  parse_parents(cursor, nif);
  write_detail::Bytes out(original.begin(), original.begin() + k_name_bytes);
  write_detail::append_int(out, static_cast<std::uint32_t>(value.points.size()));
  for (const auto& point : value.points) {
    write_detail::append_sized(out, point.parent);
    write_detail::append_sized(out, point.name);
    append_floats(out, point.rotation, 4);
    append_floats(out, point.translation, 3);
    append_float(out, point.scale);
  }
  out.insert(out.end(), original.begin() + static_cast<std::ptrdiff_t>(cursor.position()), original.end());
  return out;
}

std::vector<std::byte> encode_children(const NifFile& nif, std::uint32_t block, const ConnectChildren& value) {
  const auto original = block_bytes(nif, block);
  binary::Cursor cursor(original);
  parse_children(cursor, nif);
  write_detail::Bytes out(original.begin(), original.begin() + k_name_bytes);
  out.push_back(value.skinned ? std::byte{1} : std::byte{0});
  write_detail::append_int(out, static_cast<std::uint32_t>(value.names.size()));
  for (const auto& name : value.names) write_detail::append_sized(out, name);
  out.insert(out.end(), original.begin() + static_cast<std::ptrdiff_t>(cursor.position()), original.end());
  return out;
}

void require_parent_node(const NifFile& nif, std::uint32_t block, std::string_view parent) {
  if (parent.empty() || parent == k_workshop_connect_parent) return;
  for (const auto& node : nif.nodes()) {
    if (node.name == parent) return;
  }
  throw parse_error(nif.blocks().at(block).offset,
                    "connect point parent '" + std::string(parent) + "' is not a node in this NIF");
}

}  // namespace connect_detail

std::vector<std::uint32_t> connect_point_blocks(const NifFile& nif) {
  std::vector<std::uint32_t> blocks;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    const auto& type = nif.blocks()[block].type;
    if (type == k_connect_parents_type || type == k_connect_children_type) blocks.push_back(block);
  }
  return blocks;
}

ConnectParents read_connect_parents(const NifFile& nif, std::uint32_t block) {
  connect_detail::require_block(nif, block, k_connect_parents_type);
  auto cursor = detail::block_cursor(nif, block);
  return connect_detail::parse_parents(cursor, nif);
}

ConnectChildren read_connect_children(const NifFile& nif, std::uint32_t block) {
  connect_detail::require_block(nif, block, k_connect_children_type);
  auto cursor = detail::block_cursor(nif, block);
  return connect_detail::parse_children(cursor, nif);
}

std::array<float, 3> connect_point_world_position(const NifFile& nif, const ConnectPoint& point) {
  Transform parent;
  if (!point.parent.empty() && point.parent != k_workshop_connect_parent) {
    const auto& nodes = nif.nodes();
    const auto found = std::find_if(nodes.begin(), nodes.end(),
                                    [&](const Node& node) { return node.name == point.parent; });
    if (found != nodes.end()) parent = shape_to_global(nif, found->block);
  }
  std::array<float, 3> world{};
  for (std::size_t i = 0; i < 3; ++i) {
    float offset = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) offset += parent.r[i * 3 + k] * point.translation[k];
    world[i] = parent.s * offset + parent.t[i];
  }
  return world;
}

}  // namespace fo4::nif
