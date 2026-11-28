#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Invariant: merge_nif_into appends every donor block, remapping block, string and type references.
// Invariant: donor roots are parented under the base node.
// Constraint: only reader-pinned reference layouts are merged.
// Constraint: Havok collision is refused with the block type.
std::vector<std::byte> merge_nif_into(const NifFile& base, const NifFile& donor, std::uint32_t parent);

// Invariant: bake_shape_transforms applies each shape's accumulated transform into its vertices.
// Invariant: refused shapes (skinned, half precision, mirrored) are reported, never half-baked.
struct BakeReport {
  std::vector<std::uint32_t> baked;
  // Invariant: each entry reads "block N: reason".
  std::vector<std::string> skipped;
};

std::vector<std::byte> bake_shape_transforms(const NifFile& nif, const std::vector<std::uint32_t>& blocks,
                                             BakeReport* report = nullptr);

// Invariant: the pose library stores named bone-local transforms in BoneData space as JSON.
// Invariant: applying writes the rotation, translation and scale of every named bone the skin holds.
// Invariant: unknown pose bones and unposed skin bones are reported, never invented.
struct PoseBone {
  std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
  float scale{1.0f};
};

struct Pose {
  std::string name;
  std::map<std::string, PoseBone> bones;
};

struct PoseApplyReport {
  std::uint32_t applied{0};
  std::vector<std::string> unknown_pose_bones;
  std::vector<std::string> unposed_bones;
};

std::string write_pose_library(const std::vector<Pose>& poses);
std::vector<Pose> read_pose_library(std::string_view text);
std::vector<std::byte> apply_pose_to_skin(const NifFile& nif, std::uint32_t instance_block, const Pose& pose,
                                          PoseApplyReport* report = nullptr);

}  // namespace fo4::nif
