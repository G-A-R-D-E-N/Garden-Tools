#include "fo4/hkx/animation.hpp"

#include "hkx_read_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::elements_at;
using detail::Elements;
using detail::in_section;
using detail::local_target;
using detail::read_string;
using detail::value_at;

constexpr std::size_t kInt16Size = 2;

// Requires the global fixup form too: the animation lives in the same section, behind no local fixup.
std::optional<std::size_t> pointer_target(const HkxPackfile& packfile, const HkxSection& section, std::size_t source) {
  if (const auto local = local_target(section, source)) return local;
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.source != source) continue;
    if (fixup.section != packfile.data_section) return std::nullopt;
    if (!in_section(section, fixup.destination, 1)) return std::nullopt;
    return static_cast<std::size_t>(fixup.destination);
  }
  return std::nullopt;
}

HkxStatus read_track_map(const HkxSection& section, std::size_t field, std::size_t pointer_size,
                         std::vector<std::int16_t>& out) {
  Elements elements;
  if (const auto status = elements_at(section, field, pointer_size, kInt16Size, elements); status != HkxStatus::Ok) {
    return status;
  }
  out.clear();
  out.reserve(elements.count);
  for (std::size_t i = 0; i < elements.count; ++i) {
    out.push_back(value_at<std::int16_t>(section.data, elements.first + i * kInt16Size));
  }
  return HkxStatus::Ok;
}

HkxStatus decode_binding(const HkxPackfile& packfile, const HkxObjectView& object, const HkxClassLayout& layout,
                         HkxAnimationBinding& out) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const auto name_at = find_member_offset(layout, "originalSkeletonName");
  const auto animation_at = find_member_offset(layout, "animation");
  const auto transforms_at = find_member_offset(layout, "transformTrackToBoneIndices");
  const auto floats_at = find_member_offset(layout, "floatTrackToFloatSlotIndices");
  const auto partitions_at = find_member_offset(layout, "partitionIndices");
  const auto blend_at = find_member_offset(layout, "blendHint");
  if (!name_at || !transforms_at || !floats_at || !partitions_at || !blend_at) return HkxStatus::UnsupportedLayout;

  const std::size_t name_field = object.offset + static_cast<std::size_t>(*name_at);
  if (!in_section(section, name_field, packfile.pointer_size)) return HkxStatus::FixupOutOfRange;
  if (const auto target = local_target(section, name_field)) {
    if (const auto status = read_string(section, *target, out.original_skeleton_name); status != HkxStatus::Ok) {
      return status;
    }
  }

  if (const auto animation_field = animation_at ? std::optional<std::size_t>(object.offset + *animation_at)
                                                : std::nullopt;
      animation_field && in_section(section, *animation_field, packfile.pointer_size)) {
    out.animation_offset = pointer_target(packfile, section, *animation_field);
  }

  const std::size_t blend_field = object.offset + static_cast<std::size_t>(*blend_at);
  if (!in_section(section, blend_field, 1)) return HkxStatus::FixupOutOfRange;
  out.blend_hint = value_at<std::int8_t>(section.data, blend_field);

  if (const auto status = read_track_map(section, object.offset + *transforms_at, packfile.pointer_size,
                                        out.transform_track_to_bone_indices);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_track_map(section, object.offset + *floats_at, packfile.pointer_size,
                                        out.float_track_to_float_slot_indices);
      status != HkxStatus::Ok) {
    return status;
  }
  return read_track_map(section, object.offset + *partitions_at, packfile.pointer_size, out.partition_indices);
}

}  // namespace

HkxStatus read_binding(const HkxPackfile& packfile, HkxAnimationBinding& out) {
  out = HkxAnimationBinding{};
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkaAnimationBinding") continue;
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (layout == nullptr) return HkxStatus::UnsupportedLayout;
    return decode_binding(packfile, object, *layout, out);
  }
  return HkxStatus::MissingObject;
}

}  // namespace fo4::hkx
