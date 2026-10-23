#include "fo4/hkx/skeleton.hpp"

#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <cstring>
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

struct Elements {
  std::size_t first{0};
  std::size_t count{0};
};

template <typename T>
T value_at(std::span<const std::byte> bytes, std::size_t at) {
  T value{};
  std::memcpy(&value, bytes.data() + at, sizeof value);
  return value;
}

bool in_section(const HkxSection& section, std::size_t at, std::size_t width) {
  return at <= section.data.size() && width <= section.data.size() - at;
}

std::optional<std::size_t> local_target(const HkxSection& section, std::size_t source) {
  const auto found = std::lower_bound(section.local_fixups.begin(), section.local_fixups.end(), source,
                                      [](const HkxLocalFixup& fixup, std::size_t value) { return fixup.source < value; });
  if (found == section.local_fixups.end() || found->source != source) return std::nullopt;
  return found->destination;
}

HkxStatus read_string(const HkxSection& section, std::size_t at, std::string& out) {
  const auto text = cstring_at(section.data, at);
  if (!text) return HkxStatus::FixupOutOfRange;
  out.assign(text->begin(), text->end());
  return HkxStatus::Ok;
}

HkxStatus elements_at(const HkxSection& section, std::size_t field, std::size_t pointer_size,
                      std::size_t element_size, Elements& out) {
  if (!in_section(section, field, pointer_size + 4)) return HkxStatus::FixupOutOfRange;
  const auto count = value_at<std::int32_t>(section.data, field + pointer_size);
  if (count < 0) return HkxStatus::BadSection;
  out.count = static_cast<std::size_t>(count);
  if (out.count == 0) return HkxStatus::Ok;
  const auto target = local_target(section, field);
  if (!target || !in_section(section, *target, out.count * element_size)) return HkxStatus::FixupOutOfRange;
  out.first = *target;
  return HkxStatus::Ok;
}

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
