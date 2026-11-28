#include "fo4/hkx/skeleton_document.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::hkx {
namespace {

std::string lower_of(const std::string& text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (char ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

constexpr std::size_t kMaxBoneName = 255;
constexpr double kUnitRotationTolerance = 1e-3;

bool is_control(char ch) {
  return static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F;
}

// Requires a left/right word prefix: LArm pairs with RArm, L_Arm pairs with R_Arm, and nothing else pairs.
std::string opposite_name(const std::string& name) {
  for (std::size_t at = 0; at < name.size(); ++at) {
    const char ch = name[at];
    const bool starts_word = at == 0 || name[at - 1] == '_' || std::isupper(static_cast<unsigned char>(name[at - 1]));
    if (!starts_word) continue;
    const char swapped = ch == 'L' ? 'R' : ch == 'R' ? 'L' : ch;
    if (swapped == ch) continue;
    std::string opposite = name;
    opposite[at] = swapped;
    return opposite;
  }
  return {};
}

bool is_descendant(const std::vector<std::int16_t>& parents, std::size_t bone, std::size_t candidate) {
  std::int16_t at = parents[candidate];
  std::size_t steps = 0;
  // Constraint: the walk is bounded by the bone count, so a corrupted parent cycle cannot hang the reader.
  while (at >= 0 && steps <= parents.size()) {
    if (static_cast<std::size_t>(at) == bone) return true;
    at = parents[static_cast<std::size_t>(at)];
    ++steps;
  }
  return false;
}

void refresh_depth_and_children(HkxSkeletonDocument& document, const std::vector<std::int16_t>& parents) {
  for (HkxBoneInfo& bone : document.bones) bone.children.clear();
  for (std::size_t index = 0; index < document.bones.size(); ++index) {
    const std::int16_t parent = parents[index];
    if (parent >= 0 && static_cast<std::size_t>(parent) < document.bones.size()) {
      document.bones[static_cast<std::size_t>(parent)].children.push_back(index);
    }
  }
  for (std::size_t index = 0; index < document.bones.size(); ++index) {
    std::size_t depth = 0;
    std::int16_t at = parents[index];
    std::size_t steps = 0;
    while (at >= 0 && steps <= parents.size()) {
      ++depth;
      at = parents[static_cast<std::size_t>(at)];
      ++steps;
    }
    document.bones[index].depth = depth;
  }
  document.roots.clear();
  for (std::size_t index = 0; index < document.bones.size(); ++index) {
    if (parents[index] < 0) document.roots.push_back(document.bones[index].name);
  }
}

}  // namespace

HkxSkeletonDocument skeleton_document(const HkxSkeleton& skeleton) {
  HkxSkeletonDocument document;
  document.name = skeleton.name;
  document.has_reference_pose = skeleton.reference_pose.size() == skeleton.bone_names.size() &&
                                !skeleton.bone_names.empty();
  document.bones.reserve(skeleton.bone_names.size());
  for (std::size_t index = 0; index < skeleton.bone_names.size(); ++index) {
    HkxBoneInfo bone;
    bone.index = index;
    bone.name = skeleton.bone_names[index];
    bone.parent = index < skeleton.parent_indices.size() ? skeleton.parent_indices[index] : -1;
    document.bones.push_back(std::move(bone));
  }
  refresh_depth_and_children(document, skeleton.parent_indices);

  for (std::size_t index = 0; index < document.bones.size(); ++index) {
    const auto wanted = opposite_name(document.bones[index].name);
    if (wanted.empty()) continue;
    for (const HkxBoneInfo& other : document.bones) {
      if (other.index != index && lower_of(other.name) == lower_of(wanted)) {
        document.bones[index].opposite = other.name;
        break;
      }
    }
  }
  if (!skeleton.parent_indices.empty() && skeleton.parent_indices.size() != skeleton.bone_names.size()) {
    document.diagnostics.push_back("parent index count does not match the bone count");
  }
  if (!skeleton.reference_pose.empty() && skeleton.reference_pose.size() != skeleton.bone_names.size()) {
    document.diagnostics.push_back("reference pose count does not match the bone count");
  }
  return document;
}

HkxSkeletonEditReport apply_skeleton_edit(const HkxSkeleton& skeleton, const HkxSkeletonEdit& edit,
                                          HkxSkeleton& out) {
  HkxSkeletonEditReport report;
  if (edit.bone >= skeleton.bone_names.size()) {
    report.refusal = "bone index is out of range for this skeleton";
    return report;
  }
  HkxSkeleton next = skeleton;

  if (edit.kind == HkxSkeletonEditKind::Rename) {
    const std::string trimmed = edit.name;
    if (trimmed.empty()) {
      report.refusal = "a bone name cannot be empty";
      return report;
    }
    if (std::any_of(trimmed.begin(), trimmed.end(), is_control)) {
      report.refusal = "a bone name cannot hold control characters";
      return report;
    }
    if (trimmed.size() > kMaxBoneName) {
      report.refusal = "a bone name cannot be longer than 255 bytes";
      return report;
    }
    for (std::size_t index = 0; index < next.bone_names.size(); ++index) {
      if (index != edit.bone && lower_of(next.bone_names[index]) == lower_of(trimmed)) {
        report.refusal = "another bone is already named " + trimmed;
        return report;
      }
    }
    next.bone_names[edit.bone] = trimmed;
  } else if (edit.kind == HkxSkeletonEditKind::Reparent) {
    if (edit.parent < -1 || (edit.parent >= 0 && static_cast<std::size_t>(edit.parent) >= next.parent_indices.size())) {
      report.refusal = "parent index is out of range for this skeleton";
      return report;
    }
    if (edit.parent >= 0 && static_cast<std::size_t>(edit.parent) == edit.bone) {
      report.refusal = "a bone cannot parent itself";
      return report;
    }
    if (edit.parent >= 0 && is_descendant(next.parent_indices, edit.bone, static_cast<std::size_t>(edit.parent))) {
      report.refusal = "the chosen parent is a child of this bone";
      return report;
    }
    // Constraint: parents must stay before their children, because every pose walk assumes that order.
    if (edit.parent > 0 && static_cast<std::size_t>(edit.parent) > edit.bone) {
      report.refusal = "a parent must come before its child in the bone order";
      return report;
    }
    next.parent_indices[edit.bone] = edit.parent;
  } else if (edit.kind == HkxSkeletonEditKind::SetPose) {
    if (next.reference_pose.size() != next.bone_names.size()) {
      report.refusal = "this skeleton does not hold one reference pose per bone";
      return report;
    }
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(edit.pose.translation.begin(), edit.pose.translation.end(), finite) ||
        !std::all_of(edit.pose.rotation.begin(), edit.pose.rotation.end(), finite) ||
        !std::all_of(edit.pose.scale.begin(), edit.pose.scale.end(), finite)) {
      report.refusal = "a reference pose value must be a finite number";
      return report;
    }
    double squared = 0.0;
    for (const float lane : edit.pose.rotation) squared += static_cast<double>(lane) * lane;
    const double length = std::sqrt(squared);
    // Constraint: a stored rotation that is already off unit may be resent unchanged, so a translation edit never needs a renormalise.
    const bool resent = edit.pose.rotation == next.reference_pose[edit.bone].rotation;
    if (length == 0.0 || (std::abs(length - 1.0) > kUnitRotationTolerance && !resent)) {
      report.refusal = "a reference pose rotation must be a unit quaternion";
      return report;
    }
    next.reference_pose[edit.bone] = edit.pose;
  } else if (edit.kind == HkxSkeletonEditKind::SetLock) {
    if (next.lock_translation.size() != next.bone_names.size()) {
      report.refusal = "this skeleton does not hold one translation lock per bone";
      return report;
    }
    next.lock_translation[edit.bone] = edit.locked;
  } else {
    if (!next.parent_indices.empty()) {
      const auto child = std::find_if(next.parent_indices.begin(), next.parent_indices.end(),
                                      [&](std::int16_t parent) { return parent == static_cast<std::int16_t>(edit.bone); });
      if (child != next.parent_indices.end()) {
        report.refusal = "remove the children of this bone first";
        return report;
      }
    }
    next.bone_names.erase(next.bone_names.begin() + static_cast<std::ptrdiff_t>(edit.bone));
    if (edit.bone < next.lock_translation.size()) {
      next.lock_translation.erase(next.lock_translation.begin() + static_cast<std::ptrdiff_t>(edit.bone));
    }
    if (!next.parent_indices.empty()) {
      next.parent_indices.erase(next.parent_indices.begin() + static_cast<std::ptrdiff_t>(edit.bone));
      for (std::int16_t& parent : next.parent_indices) {
        if (parent > static_cast<std::int16_t>(edit.bone)) parent = static_cast<std::int16_t>(parent - 1);
      }
    }
    if (!next.reference_pose.empty() && edit.bone < next.reference_pose.size()) {
      next.reference_pose.erase(next.reference_pose.begin() + static_cast<std::ptrdiff_t>(edit.bone));
    }
  }

  out = std::move(next);
  report.changed = true;
  return report;
}

}  // namespace fo4::hkx
