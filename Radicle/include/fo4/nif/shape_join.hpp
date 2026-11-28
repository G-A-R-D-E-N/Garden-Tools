#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Constraint: join merges shapes of one file into one (Outfit Studio "Merge Into").
// Invariant: the first block keeps its node, material, shader and skin blocks.
// Invariant: merged donors are removed with remove_block.
// Constraint: all shapes must be BSTriShape with byte-identical vertex layouts.
// Constraint: global transforms must be bitwise-identical (bake first).
// Constraint: all unskinned, or all skinned through BSSkin::Instance with mergeable bone lists.
// Constraint: BSDismemberSkinInstance shapes are refused until partitions merge.
struct JoinRequest {
  // Invariant: blocks[0] is the surviving target; at least two blocks.
  std::vector<std::uint32_t> blocks;
};

struct JoinReport {
  std::uint32_t target{0};
  std::vector<std::uint32_t> removed;
  std::size_t vertices_before{0};
  std::size_t vertices_after{0};
  std::size_t triangles_before{0};
  std::size_t triangles_after{0};
};

std::vector<std::byte> join_shapes(const NifFile& nif, const JoinRequest& request, JoinReport* report = nullptr);

// Constraint: separate splits one shape into several by triangle partition (Outfit Studio "Separate").
// Constraint: every triangle must appear in exactly one group.
// Invariant: copies share the skin, shader and texture blocks.
// Constraint: the shape's parent node must be a plain node.
struct SeparateRequest {
  std::uint32_t block{0};
  // Invariant: at least two non-empty groups forming an exact partition of the triangle list.
  std::vector<std::vector<std::uint32_t>> groups;
};

struct SeparateReport {
  // Invariant: blocks[0] is the original block, the rest are the appended copies in group order.
  std::vector<std::uint32_t> blocks;
};

std::vector<std::byte> separate_shape(const NifFile& nif, const SeparateRequest& request,
                                      SeparateReport* report = nullptr);

// Invariant: separate_by_bone assigns each triangle to its dominant bone.
// Constraint: triangles with no positive bone weight are refused with the triangle index.
std::vector<std::byte> separate_by_bone(const NifFile& nif, std::uint32_t block, SeparateReport* report = nullptr);

}  // namespace fo4::nif
