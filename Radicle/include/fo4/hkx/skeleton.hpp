#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

struct HkxQsTransform {
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{};
  std::array<float, 3> scale{};
};

// Requires the index to stay stable: an unnamed slot keeps its place so a binding's slot index still addresses it.
struct HkxSkeletonFloatSlot {
  std::string name;
  bool named{false};
};

// Invariant: has_frame records whether localFrame resolved, so a null frame is reported rather than dropped.
struct HkxSkeletonLocalFrame {
  std::int16_t bone_index{0};
  bool has_frame{false};
  std::size_t frame_offset{0};
};

struct HkxSkeletonPartition {
  std::string name;
  bool named{false};
  std::int16_t start_bone{0};
  std::int16_t num_bones{0};
};

struct HkxSkeleton {
  std::string name;
  std::vector<std::string> bone_names;
  std::vector<std::int16_t> parent_indices;
  std::vector<HkxQsTransform> reference_pose;
  // Invariant: one entry per bone, so a caller may index this by bone exactly like bone_names.
  std::vector<bool> lock_translation;
  std::vector<float> reference_floats;
  std::vector<HkxSkeletonFloatSlot> float_slots;
  std::vector<HkxSkeletonLocalFrame> local_frames;
  std::vector<HkxSkeletonPartition> partitions;
};

HkxStatus read_skeleton(const HkxPackfile& packfile, HkxSkeleton& out);

// Constraint: the object must be an hkaSkeleton view of this packfile; it is decoded alone, ignoring the animation binding.
HkxStatus read_skeleton_object(const HkxPackfile& packfile, const HkxObjectView& object, HkxSkeleton& out);

}  // namespace fo4::hkx
