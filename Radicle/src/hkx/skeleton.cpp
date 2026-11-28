#include "fo4/hkx/skeleton.hpp"

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t kQsTransformSize = 48;
constexpr std::size_t kQsRotationOffset = 16;
constexpr std::size_t kQsScaleOffset = 32;
constexpr std::size_t kFloatSize = 4;
constexpr std::size_t kParentIndexSize = 2;
constexpr std::size_t kIndexSize = 2;
constexpr std::size_t kLockSize = 1;

using detail::Elements;
using detail::elements_at;
using detail::in_section;
using detail::local_target;
using detail::read_string;
using detail::value_at;

HkxStatus check_skeleton(const HkxSkeleton& skeleton) {
  const std::size_t count = skeleton.bone_names.size();
  if (skeleton.parent_indices.size() != count || skeleton.reference_pose.size() != count ||
      skeleton.lock_translation.size() != count) {
    return HkxStatus::BadSection;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const std::int16_t parent = skeleton.parent_indices[i];
    if (parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= i)) return HkxStatus::BadSection;
  }
  for (const HkxQsTransform& transform : skeleton.reference_pose) {
    if (!detail::transform_is_finite(transform)) return HkxStatus::BadSection;
  }
  return HkxStatus::Ok;
}

HkxStatus decode_skeleton(const HkxPackfile& packfile, const HkxObjectView& object, HkxSkeleton& out) {
  const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
  if (!detail::holds_layout(object, *layout)) return HkxStatus::Truncated;
  const HkxSection& section = packfile.sections[packfile.data_section];
  const std::size_t pointer = packfile.pointer_size;
  const auto field = [&](std::string_view member) -> std::optional<std::size_t> {
    const auto offset = find_member_offset(*layout, member);
    if (!offset) return std::nullopt;
    return object.offset + static_cast<std::size_t>(*offset);
  };

  const auto name_at = field("name");
  const auto parents_at = field("parentIndices");
  const auto bones_at = field("bones");
  const auto pose_at = field("referencePose");
  const HkxClassLayout* bone = find_layout("hkaBone");
  const auto bone_name = bone == nullptr ? std::nullopt : find_member_offset(*bone, "name");
  if (!name_at || !parents_at || !bones_at || !pose_at || !bone_name) {
    return HkxStatus::UnsupportedLayout;
  }

  if (const auto target = local_target(section, *name_at)) {
    if (const auto status = read_string(section, *target, out.name); status != HkxStatus::Ok) return status;
  }

  Elements parents;
  if (const auto status = elements_at(section, *parents_at, pointer, kParentIndexSize, parents);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < parents.count; ++i) {
    out.parent_indices.push_back(value_at<std::int16_t>(section.data, parents.first + i * kParentIndexSize));
  }

  Elements bones;
  if (const auto status = elements_at(section, *bones_at, pointer, bone->size, bones); status != HkxStatus::Ok) {
    return status;
  }
  const auto bone_lock = find_member_offset(*bone, "lockTranslation");
  for (std::size_t i = 0; i < bones.count; ++i) {
    const std::size_t element = bones.first + i * bone->size;
    const auto target = local_target(section, element + *bone_name);
    if (!target) return HkxStatus::FixupOutOfRange;
    std::string text;
    if (const auto status = read_string(section, *target, text); status != HkxStatus::Ok) return status;
    out.bone_names.push_back(std::move(text));
    // Constraint: the lock flag is a byte beside the name, so a layout without it still yields one entry per bone.
    bool locked = false;
    if (bone_lock) {
      const std::size_t lock_at = element + *bone_lock;
      locked = in_section(section, lock_at, kLockSize) && value_at<std::uint8_t>(section.data, lock_at) != 0;
    }
    out.lock_translation.push_back(locked);
  }

  Elements pose;
  if (const auto status = elements_at(section, *pose_at, pointer, kQsTransformSize, pose);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < pose.count; ++i) {
    const std::size_t at = pose.first + i * kQsTransformSize;
    HkxQsTransform transform;
    for (std::size_t k = 0; k < 3; ++k) {
      transform.translation[k] = value_at<float>(section.data, at + k * kFloatSize);
      transform.scale[k] = value_at<float>(section.data, at + kQsScaleOffset + k * kFloatSize);
    }
    for (std::size_t k = 0; k < 4; ++k) {
      transform.rotation[k] = value_at<float>(section.data, at + kQsRotationOffset + k * kFloatSize);
    }
    out.reference_pose.push_back(transform);
  }

  if (const auto floats_at = field("referenceFloats")) {
    Elements floats;
    if (const auto status = elements_at(section, *floats_at, pointer, kFloatSize, floats); status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t i = 0; i < floats.count; ++i) {
      out.reference_floats.push_back(value_at<float>(section.data, floats.first + i * kFloatSize));
    }
  }

  if (const auto slots_at = field("floatSlots")) {
    Elements slots;
    if (const auto status = elements_at(section, *slots_at, pointer, pointer, slots); status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t i = 0; i < slots.count; ++i) {
      HkxSkeletonFloatSlot slot;
      const auto target = local_target(section, slots.first + i * pointer);
      if (target) {
        if (const auto status = read_string(section, *target, slot.name); status != HkxStatus::Ok) return status;
        slot.named = true;
      }
      out.float_slots.push_back(std::move(slot));
    }
  }

  const HkxClassLayout* frame_layout = find_layout("hkaSkeletonLocalFrameOnBone");
  if (const auto frames_at = field("localFrames"); frames_at && frame_layout != nullptr) {
    const auto frame_bone = find_member_offset(*frame_layout, "boneIndex");
    const auto frame_ref = find_member_offset(*frame_layout, "localFrame");
    Elements frames;
    if (const auto status = elements_at(section, *frames_at, pointer, frame_layout->size, frames);
        status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t i = 0; i < frames.count; ++i) {
      const std::size_t element = frames.first + i * frame_layout->size;
      HkxSkeletonLocalFrame frame;
      if (frame_bone && in_section(section, element + *frame_bone, kIndexSize)) {
        frame.bone_index = value_at<std::int16_t>(section.data, element + *frame_bone);
      }
      if (frame_ref) {
        if (const auto target = local_target(section, element + *frame_ref)) {
          frame.has_frame = true;
          frame.frame_offset = *target;
        }
      }
      out.local_frames.push_back(frame);
    }
  }

  const HkxClassLayout* partition_layout = find_layout("hkaSkeletonPartition");
  if (const auto parts_at = field("partitions"); parts_at && partition_layout != nullptr) {
    const auto part_name = find_member_offset(*partition_layout, "name");
    const auto part_start = find_member_offset(*partition_layout, "startBoneIndex");
    const auto part_count = find_member_offset(*partition_layout, "numBones");
    Elements parts;
    if (const auto status = elements_at(section, *parts_at, pointer, partition_layout->size, parts);
        status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t i = 0; i < parts.count; ++i) {
      const std::size_t element = parts.first + i * partition_layout->size;
      HkxSkeletonPartition partition;
      if (part_name) {
        if (const auto target = local_target(section, element + *part_name)) {
          if (const auto status = read_string(section, *target, partition.name); status != HkxStatus::Ok) {
            return status;
          }
          partition.named = true;
        }
      }
      if (part_start && in_section(section, element + *part_start, kIndexSize)) {
        partition.start_bone = value_at<std::int16_t>(section.data, element + *part_start);
      }
      if (part_count && in_section(section, element + *part_count, kIndexSize)) {
        partition.num_bones = value_at<std::int16_t>(section.data, element + *part_count);
      }
      out.partitions.push_back(partition);
    }
  }
  return check_skeleton(out);
}

}  // namespace

HkxStatus read_skeleton_object(const HkxPackfile& packfile, const HkxObjectView& object, HkxSkeleton& out) {
  out = HkxSkeleton{};
  if (object.class_name != "hkaSkeleton") return HkxStatus::MissingObject;
  return decode_skeleton(packfile, object, out);
}

HkxStatus read_skeleton(const HkxPackfile& packfile, HkxSkeleton& out) {
  out = HkxSkeleton{};
  std::vector<HkxSkeleton> rigs;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkaSkeleton") continue;
    HkxSkeleton candidate;
    if (const auto status = decode_skeleton(packfile, object, candidate); status != HkxStatus::Ok) return status;
    if (!candidate.bone_names.empty()) rigs.push_back(std::move(candidate));
  }
  if (rigs.empty()) return HkxStatus::MissingObject;

  HkxAnimationBinding binding;
  const HkxStatus bound = read_binding(packfile, binding);
  if (bound != HkxStatus::Ok && bound != HkxStatus::MissingObject) return bound;
  if (bound == HkxStatus::Ok && !binding.original_skeleton_name.empty()) {
    HkxSkeleton* named = nullptr;
    for (HkxSkeleton& rig : rigs) {
      if (rig.name != binding.original_skeleton_name) continue;
      if (named != nullptr) return HkxStatus::UnsupportedLayout;
      named = &rig;
    }
    if (named == nullptr) return HkxStatus::UnsupportedLayout;
    out = std::move(*named);
    return HkxStatus::Ok;
  }

  // Requires a unique largest rig when no binding names one: equal bone counts cannot be told apart.
  std::size_t most = 0;
  for (const HkxSkeleton& rig : rigs) most = std::max(most, rig.bone_names.size());
  std::size_t chosen = 0;
  std::size_t at_most = 0;
  for (std::size_t i = 0; i < rigs.size(); ++i) {
    if (rigs[i].bone_names.size() != most) continue;
    chosen = i;
    ++at_most;
  }
  if (at_most > 1) return HkxStatus::UnsupportedLayout;
  out = std::move(rigs[chosen]);
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
