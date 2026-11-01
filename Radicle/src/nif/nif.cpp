#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace fo4::nif {
namespace {

constexpr std::size_t k_max_file_size = std::size_t{512} * 1024 * 1024;
constexpr std::size_t k_max_header_line = 1024;
constexpr std::size_t k_max_blocks = 1'000'000;
constexpr std::size_t k_max_strings = 100'000;
constexpr std::uint32_t k_max_string_length = 1024 * 1024;
constexpr std::uint32_t k_version_20_2_0_7 = 0x14020007U;
constexpr std::uint32_t k_user_version = 12;
constexpr std::uint32_t k_bs_version = 130;
constexpr std::string_view k_banner = "Gamebryo File Format";
constexpr std::array<std::string_view, 15> k_node_types{
    "NiNode",        "BSFadeNode",     "BSMultiBoundNode", "BSOrderedNode",  "BSLeafAnimNode",
    "BSTreeNode",    "NiBillboardNode", "NiSwitchNode",    "NiLODNode",      "BSValueNode",
    "BSRangeNode",   "BSDebrisNode",   "BSBlastNode",      "BSDamageStage",  "NiSortAdjustNode"};

bool is_node_type(std::string_view type) {
  return std::find(k_node_types.begin(), k_node_types.end(), type) != k_node_types.end();
}

std::string read_sized_string(binary::Cursor& cursor) {
  const auto length = cursor.read_u32();
  if (length > k_max_string_length) {
    throw parse_error(cursor.absolute_offset(), "string exceeds 1 MiB");
  }
  const auto text = cursor.read_bytes(length);
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::vector<std::uint32_t> read_children(binary::Cursor& cursor, std::size_t block_count) {
  const auto count = cursor.read_u32();
  if (static_cast<std::size_t>(count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "child list runs past the end of its block");
  }
  std::vector<std::uint32_t> children;
  children.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto child = cursor.read_i32();
    if (child < 0) continue;
    if (static_cast<std::size_t>(child) >= block_count) {
      throw parse_error(cursor.absolute_offset(), "child reference is out of range");
    }
    children.push_back(static_cast<std::uint32_t>(child));
  }
  return children;
}

Transform compose(const Transform& outer, const Transform& inner) {
  Transform out;
  for (std::size_t i = 0; i < 3; ++i) {
    float offset = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) offset += outer.r[i * 3 + k] * inner.t[k];
    out.t[i] = outer.s * offset + outer.t[i];
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      float value = 0.0f;
      for (std::size_t k = 0; k < 3; ++k) value += outer.r[i * 3 + k] * inner.r[k * 3 + j];
      out.r[i * 3 + j] = value;
    }
  }
  out.s = outer.s * inner.s;
  return out;
}

bool is_root(const NifFile& nif, std::uint32_t block) {
  return std::find(nif.roots().begin(), nif.roots().end(), block) != nif.roots().end();
}

}  // namespace

namespace detail {

binary::Cursor block_cursor(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  return binary::Cursor(std::span<const std::byte>(nif.bytes()).subspan(info.offset, info.size), info.offset);
}

std::string read_string_ref(binary::Cursor& cursor, const NifFile& nif) {
  const auto index = cursor.read_i32();
  if (index == -1) return {};
  if (index < 0 || static_cast<std::size_t>(index) >= nif.strings().size()) {
    throw parse_error(cursor.absolute_offset(), "string index is out of range");
  }
  return nif.strings()[static_cast<std::size_t>(index)];
}

NetPrefix read_net_prefix(binary::Cursor& cursor, const NifFile& nif) {
  NetPrefix prefix;
  prefix.name_at = cursor.position();
  prefix.name = read_string_ref(cursor, nif);
  const auto extra_count = cursor.read_u32();
  if (static_cast<std::size_t>(extra_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "extra-data list runs past the end of its block");
  }
  cursor.read_bytes(static_cast<std::size_t>(extra_count) * 4);
  cursor.read_i32();
  prefix.end = cursor.position();
  return prefix;
}

AvHeader read_av_header(binary::Cursor& cursor, const NifFile& nif) {
  const auto name = cursor.read_i32();
  const auto extra_count = cursor.read_u32();
  if (static_cast<std::size_t>(extra_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "extra-data list runs past the end of its block");
  }
  cursor.read_bytes(static_cast<std::size_t>(extra_count) * 4);
  cursor.read_i32();
  cursor.read_u32();

  AvHeader header;
  if (name != -1) {
    if (name < 0 || static_cast<std::size_t>(name) >= nif.strings().size()) {
      throw parse_error(cursor.absolute_offset(), "string index is out of range");
    }
    header.name = nif.strings()[static_cast<std::size_t>(name)];
  }
  for (float& value : header.transform.t) value = cursor.read_f32();
  for (float& value : header.transform.r) value = cursor.read_f32();
  header.transform.s = cursor.read_f32();
  return header;
}

std::vector<std::string> read_texture_set(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  if (info.type != "BSShaderTextureSet") return {};
  if (info.size < 4) {
    throw parse_error(info.offset, "texture set is shorter than its slot count");
  }
  auto cursor = block_cursor(nif, block);
  const auto count = cursor.read_u32();
  if (count > 4096) {
    throw parse_error(cursor.absolute_offset(), "texture slot count exceeds 4,096");
  }
  if (static_cast<std::size_t>(count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "texture slot list runs past the end of its block");
  }
  std::vector<std::string> textures;
  textures.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto length = cursor.read_u32();
    if (length > cursor.remaining()) {
      throw parse_error(cursor.absolute_offset(), "texture path runs past the end of its block");
    }
    const auto text = cursor.read_bytes(length);
    textures.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
  }
  return textures;
}

}  // namespace detail

NifFile read_nif(std::span<const std::byte> bytes) {
  if (bytes.size() > k_max_file_size) throw parse_error(0, "NIF file is larger than 512 MiB");
  NifFile nif;
  nif.bytes_.assign(bytes.begin(), bytes.end());
  const std::span<const std::byte> all{nif.bytes_};

  const auto search = all.first(std::min(all.size(), k_max_header_line + 1));
  const auto newline = std::find(search.begin(), search.end(), std::byte{'\n'});
  if (newline == search.end()) throw parse_error(0, "NIF header line is missing or longer than 1024 bytes");
  const auto header_length = static_cast<std::size_t>(newline - search.begin());
  const std::string_view banner(reinterpret_cast<const char*>(all.data()), header_length);
  if (!banner.starts_with(k_banner)) throw parse_error(0, "file does not start with a Gamebryo header");

  binary::Cursor cursor(all.subspan(header_length + 1), header_length + 1);
  nif.version_ = cursor.read_u32();
  if (nif.version_ != k_version_20_2_0_7) {
    throw parse_error(cursor.absolute_offset(), "only NIF version 20.2.0.7 is read");
  }
  if (cursor.read_u8() != 1) throw parse_error(cursor.absolute_offset(), "only little-endian NIF is read");
  nif.user_version_ = cursor.read_u32();
  if (nif.user_version_ != k_user_version) throw parse_error(cursor.absolute_offset(), "only user version 12 is read");
  const auto block_count = cursor.read_u32();
  nif.bs_version_ = cursor.read_u32();
  if (nif.bs_version_ != k_bs_version) throw parse_error(cursor.absolute_offset(), "only BS version 130 is read");
  if (block_count > k_max_blocks) throw parse_error(cursor.absolute_offset(), "block count exceeds 1,000,000");
  const auto blocks_count = static_cast<std::size_t>(block_count);

  for (std::size_t i = 0; i < 4; ++i) cursor.read_bytes(cursor.read_u8());

  const auto type_count = static_cast<std::size_t>(cursor.read_u16());
  if (type_count > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "block type table runs past the end of the file");
  }
  std::vector<std::string> types;
  types.reserve(type_count);
  for (std::size_t i = 0; i < type_count; ++i) types.push_back(read_sized_string(cursor));

  if (blocks_count > cursor.remaining() / 6) {
    throw parse_error(cursor.absolute_offset(), "block table runs past the end of the file");
  }
  std::vector<std::size_t> type_indices;
  type_indices.reserve(blocks_count);
  for (std::size_t i = 0; i < blocks_count; ++i) {
    const auto index = static_cast<std::size_t>(cursor.read_u16());
    if (index >= types.size()) throw parse_error(cursor.absolute_offset(), "block type index is out of range");
    type_indices.push_back(index);
  }
  std::vector<std::size_t> sizes;
  sizes.reserve(blocks_count);
  for (std::size_t i = 0; i < blocks_count; ++i) sizes.push_back(cursor.read_u32());

  const auto string_count = cursor.read_u32();
  if (string_count > k_max_strings) throw parse_error(cursor.absolute_offset(), "string count exceeds 100,000");
  const auto max_string_length = cursor.read_u32();
  if (static_cast<std::size_t>(string_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "string table runs past the end of the file");
  }
  nif.strings_.reserve(string_count);
  for (std::size_t i = 0; i < string_count; ++i) {
    auto text = read_sized_string(cursor);
    if (max_string_length != 0 && text.size() > max_string_length) {
      throw parse_error(cursor.absolute_offset(), "string exceeds the declared maximum length");
    }
    nif.strings_.push_back(std::move(text));
  }

  const auto group_count = cursor.read_u32();
  if (static_cast<std::size_t>(group_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "group table runs past the end of the file");
  }
  cursor.read_bytes(static_cast<std::size_t>(group_count) * 4);

  std::size_t offset = cursor.absolute_offset();
  nif.blocks_.reserve(blocks_count);
  for (std::size_t i = 0; i < blocks_count; ++i) {
    const auto size = sizes[i];
    if (size > all.size() - offset) throw parse_error(offset, "block extends past the end of the file");
    nif.blocks_.push_back(BlockInfo{types[type_indices[i]], offset, size});
    offset += size;
  }

  binary::Cursor footer(all.subspan(offset), offset);
  const auto root_count = static_cast<std::size_t>(footer.read_u32());
  if (root_count > footer.remaining() / 4) {
    throw parse_error(footer.absolute_offset(), "root table runs past the end of the file");
  }
  nif.roots_.reserve(root_count);
  for (std::size_t i = 0; i < root_count; ++i) {
    const auto root = footer.read_i32();
    if (root < 0 || static_cast<std::size_t>(root) >= blocks_count) {
      throw parse_error(footer.absolute_offset(), "root reference is out of range");
    }
    nif.roots_.push_back(static_cast<std::uint32_t>(root));
  }
  if (footer.remaining() != 0) throw parse_error(footer.absolute_offset(), "trailing bytes after the root table");

  nif.children_.resize(blocks_count);
  nif.parents_.assign(blocks_count, -1);
  nif.node_of_block_.assign(blocks_count, -1);
  for (std::size_t i = 0; i < blocks_count; ++i) {
    const auto& type = nif.blocks_[i].type;
    if (!is_node_type(type)) continue;
    auto block = detail::block_cursor(nif, static_cast<std::uint32_t>(i));
    const auto header = detail::read_av_header(block, nif);
    block.read_i32();
    nif.children_[i] = read_children(block, blocks_count);
    for (const auto child : nif.children_[i]) {
      if (nif.parents_[child] < 0) nif.parents_[child] = static_cast<std::int32_t>(i);
    }
    Node node;
    node.block = static_cast<std::uint32_t>(i);
    node.type = type;
    node.name = header.name;
    node.children = nif.children_[i];
    node.local = header.transform;
    if (type == "BSValueNode" && block.remaining() >= 4) node.value = block.read_i32();
    nif.node_of_block_[i] = static_cast<std::int32_t>(nif.nodes_.size());
    nif.nodes_.push_back(std::move(node));
  }
  std::vector<std::uint8_t> walk_state(blocks_count, 0);
  std::vector<std::size_t> path;
  for (std::size_t start = 0; start < blocks_count; ++start) {
    path.clear();
    std::size_t current = start;
    while (walk_state[current] == 0) {
      walk_state[current] = 1;
      path.push_back(current);
      const auto parent = nif.parents_[current];
      if (parent < 0) break;
      current = static_cast<std::size_t>(parent);
      if (walk_state[current] == 1) {
        throw parse_error(nif.blocks_[start].offset, "node parent chain contains a cycle");
      }
    }
    for (const auto visited : path) walk_state[visited] = 2;
  }
  for (auto& node : nif.nodes_) {
    const auto parent = nif.parents_[node.block];
    if (parent >= 0) node.parent = nif.node_of_block_[static_cast<std::size_t>(parent)];
  }
  return nif;
}

Transform shape_to_global(const NifFile& nif, std::uint32_t block) {
  Transform world;
  std::uint32_t current = block;
  for (std::size_t steps = 0; steps <= nif.blocks().size(); ++steps) {
    if (!is_root(nif, current)) {
      auto cursor = detail::block_cursor(nif, current);
      world = compose(detail::read_av_header(cursor, nif).transform, world);
    }
    const auto parent = nif.parent(current);
    if (parent < 0) return world;
    current = static_cast<std::uint32_t>(parent);
  }
  throw parse_error(nif.blocks().at(block).offset, "parent chain contains a cycle");
}

std::int32_t shape_parent_node(const NifFile& nif, std::uint32_t block) {
  std::int32_t current = nif.parent(block);
  for (std::size_t steps = 0; current >= 0; ++steps) {
    if (steps > nif.blocks().size()) throw parse_error(nif.blocks().at(block).offset, "parent chain contains a cycle");
    const auto index = nif.node_for_block(static_cast<std::uint32_t>(current));
    if (index >= 0) return index;
    current = nif.parent(static_cast<std::uint32_t>(current));
  }
  return -1;
}

std::int32_t billboard_mode(const NifFile& nif, std::uint32_t block) {
  std::int32_t current = nif.parent(block);
  for (std::size_t steps = 0; current >= 0; ++steps) {
    if (steps > nif.blocks().size()) throw parse_error(nif.blocks().at(block).offset, "parent chain contains a cycle");
    const auto index = static_cast<std::uint32_t>(current);
    if (nif.blocks()[index].type == "NiBillboardNode") {
      auto cursor = detail::block_cursor(nif, index);
      detail::read_av_header(cursor, nif);
      cursor.read_i32();
      read_children(cursor, nif.blocks().size());
      return static_cast<std::int32_t>(cursor.read_u16());
    }
    current = nif.parent(index);
  }
  return -1;
}

}  // namespace fo4::nif
