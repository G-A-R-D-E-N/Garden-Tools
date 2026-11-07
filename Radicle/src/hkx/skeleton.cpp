#include "fo4/hkx/skeleton.hpp"

#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"

#include <optional>
#include <string>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::size_t kQsTransformSize = 48;
constexpr std::size_t kQsRotationOffset = 16;
constexpr std::size_t kQsScaleOffset = 32;
constexpr std::size_t kFloatSize = 4;
constexpr std::size_t kParentIndexSize = 2;

using detail::Elements;
using detail::elements_at;
using detail::in_section;
using detail::local_target;
using detail::read_string;
using detail::value_at;

HkxStatus check_skeleton(const HkxSkeleton& skeleton) {
  const std::size_t count = skeleton.bone_names.size();
  if (skeleton.parent_indices.size() != count || skeleton.reference_pose.size() != count) {
    return HkxStatus::BadSection;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const std::int16_t parent = skeleton.parent_indices[i];
    if (parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= i)) return HkxStatus::BadSection;
  }
  return HkxStatus::Ok;
}

HkxStatus decode_skeleton(const HkxPackfile& packfile, const HkxObjectView& object, HkxSkeleton& out) {
  const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
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
  for (std::size_t i = 0; i < bones.count; ++i) {
    const std::size_t element = bones.first + i * bone->size;
    const auto target = local_target(section, element + *bone_name);
    if (!target) return HkxStatus::FixupOutOfRange;
    std::string text;
    if (const auto status = read_string(section, *target, text); status != HkxStatus::Ok) return status;
    out.bone_names.push_back(std::move(text));
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
  return check_skeleton(out);
}

}  // namespace

HkxStatus read_skeleton(const HkxPackfile& packfile, HkxSkeleton& out) {
  out = HkxSkeleton{};
  bool found = false;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkaSkeleton") continue;
    HkxSkeleton candidate;
    if (const auto status = decode_skeleton(packfile, object, candidate); status != HkxStatus::Ok) return status;
    if (candidate.bone_names.empty()) continue;
    if (!found || candidate.bone_names.size() > out.bone_names.size()) {
      out = std::move(candidate);
      found = true;
    }
  }
  return found ? HkxStatus::Ok : HkxStatus::MissingObject;
}

}  // namespace fo4::hkx
