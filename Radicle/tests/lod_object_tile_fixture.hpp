#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::test::lodtile {

// Invariant: measured vanilla order is root, then per group node, shape and shader, the shared texture set once, alpha, bound and AABB.
inline constexpr std::int32_t k_block_root = 0;
inline constexpr std::int32_t k_block_node = 1;
inline constexpr std::int32_t k_block_shape = 2;
inline constexpr std::int32_t k_block_texture_set = 4;


inline std::size_t count_type(const fo4::nif::NifFile& nif, std::string_view type) {
  std::size_t count = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type == type) ++count;
  }
  return count;
}

inline std::uint64_t read_u64(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t offset) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[info.offset + offset + i]) << (8 * i);
  return value;
}

inline std::uint32_t read_u32(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t offset) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[info.offset + offset + i]) << (8 * i);
  return value;
}

inline std::int32_t read_i32(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32(nif, block, offset));
}

inline std::string hex_of(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t offset, std::size_t count) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::string out;
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < count; ++i) {
    const auto value = bytes[info.offset + offset + i];
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
  }
  return out;
}

inline std::vector<float> block_floats(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t count) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::vector<float> out;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t bits = 0;
    for (std::size_t k = 0; k < 4; ++k) {
      bits |= static_cast<std::uint32_t>(bytes[info.offset + i * 4 + k]) << (8 * k);
    }
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    out.push_back(value);
  }
  return out;
}

// Invariant: every reference is read from the blocks themselves, because the vanilla order interleaves the texture set and alpha blocks.
struct GroupBlocks {
  std::uint32_t node{0};
  std::uint32_t shape{0};
  std::size_t shift{0};
  std::uint32_t shader{0};
  std::uint32_t bound{0};
  std::uint32_t aabb{0};
};

inline std::vector<GroupBlocks> group_blocks(const fo4::nif::NifFile& nif) {
  std::vector<GroupBlocks> groups;
  if (nif.roots().empty()) return groups;
  const auto root = nif.roots().front();
  const auto children = read_u32(nif, root, 72);
  for (std::uint32_t index = 0; index < children; ++index) {
    const auto node = static_cast<std::uint32_t>(read_i32(nif, root, 76 + 4 * static_cast<std::size_t>(index)));
    GroupBlocks group;
    group.node = node;
    group.shape = static_cast<std::uint32_t>(read_i32(nif, node, 76));
    group.shift = 4 * static_cast<std::size_t>(read_u32(nif, group.shape, 4));
    group.shader = static_cast<std::uint32_t>(read_i32(nif, group.shape, 92 + group.shift));
    const auto node_children = read_u32(nif, node, 72);
    group.bound = static_cast<std::uint32_t>(read_i32(nif, node, 76 + 4 * node_children));
    group.aabb = static_cast<std::uint32_t>(read_i32(nif, group.bound, 0));
    groups.push_back(group);
  }
  return groups;
}

inline void expect_group_references(const fo4::nif::NifFile& nif) {
  const auto groups = group_blocks(nif);
  ASSERT_EQ(groups.size(), count_type(nif, "BSMultiBoundNode"));
  ASSERT_EQ(groups.size(), count_type(nif, "BSSubIndexTriShape"));
  for (const auto& group : groups) {
    EXPECT_EQ(nif.blocks().at(group.node).type, "BSMultiBoundNode");
    EXPECT_EQ(nif.blocks().at(group.shape).type, "BSSubIndexTriShape");
    EXPECT_EQ(nif.blocks().at(group.shader).type, "BSLightingShaderProperty");
    EXPECT_EQ(nif.blocks().at(group.bound).type, "BSMultiBound");
    EXPECT_EQ(nif.blocks().at(group.aabb).type, "BSMultiBoundAABB");
    EXPECT_EQ(read_u32(nif, group.node, 72), 1u);
    EXPECT_EQ(read_i32(nif, group.node, 76), static_cast<std::int32_t>(group.shape));
    EXPECT_EQ(read_i32(nif, group.node, 80), static_cast<std::int32_t>(group.bound));
    // Invariant: measured BSMultiBoundNode ends with a cull mode of 1 after its bound reference.
    EXPECT_EQ(read_u32(nif, group.node, 84), 1u);
    EXPECT_EQ(read_i32(nif, group.shader, 40), k_block_texture_set);
    const auto alpha_ref = read_i32(nif, group.shape, 96 + group.shift);
    if (alpha_ref >= 0) {
      EXPECT_EQ(nif.blocks().at(static_cast<std::size_t>(alpha_ref)).type, "NiAlphaProperty");
    }
  }
}

inline fo4::nif::NifFile synthetic_mesh() {
  const std::string obj =
      "v 0 0 0\nv 128 0 0\nv 0 128 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
  fo4::nif::ImportOptions options;
  return fo4::nif::read_nif(fo4::nif::import_obj(obj, options));
}

}  // namespace fo4::test::lodtile
