#include "fo4/hkx/channel_audit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr float kTranslationThreshold = 0.01f;
constexpr float kRotationDegrees = 0.5f;
constexpr float kScaleThreshold = 0.01f;
constexpr double kPi = 3.14159265358979323846;

float rotation_degrees(const std::array<float, 4>& rotation) {
  const double length = std::sqrt(static_cast<double>(rotation[0]) * rotation[0] + static_cast<double>(rotation[1]) * rotation[1] +
                                  static_cast<double>(rotation[2]) * rotation[2] + static_cast<double>(rotation[3]) * rotation[3]);
  if (!(length > 0.0)) return 0.0f;
  const double w = std::min(1.0, std::fabs(static_cast<double>(rotation[3])) / length);
  return static_cast<float>(2.0 * std::acos(w) * 180.0 / kPi);
}

}  // namespace

HkxStatus audit_channels(const HkxSkeleton& skeleton, const HkxAnimation& animation, const HkxAnimationBinding& binding,
                         HkxChannelAudit& out) {
  out = HkxChannelAudit{};
  const std::size_t bones = skeleton.bone_names.size();
  if (skeleton.reference_pose.size() != bones) return HkxStatus::BadSection;
  if (animation.kind == HkxAnimationKind::View) return HkxStatus::UnsupportedLayout;
  const std::size_t tracks = animation.tracks.size();
  if (binding.transform_track_to_bone_indices.size() != tracks) return HkxStatus::BadSection;
  std::vector<bool> mapped(bones, false);
  for (const std::int16_t bone : binding.transform_track_to_bone_indices) {
    if (bone < 0 || static_cast<std::size_t>(bone) >= bones || mapped[static_cast<std::size_t>(bone)]) {
      return HkxStatus::BadSection;
    }
    mapped[static_cast<std::size_t>(bone)] = true;
  }

  out.tracks = tracks;
  for (std::size_t at = 0; at < tracks; ++at) {
    const HkxAnimationTrack& track = animation.tracks[at];
    const auto bone = static_cast<std::size_t>(binding.transform_track_to_bone_indices[at]);
    const HkxQsTransform& rest = skeleton.reference_pose[bone];

    HkxChannelTrack row;
    row.track = at;
    row.bone = static_cast<std::int32_t>(bone);
    row.bone_name = skeleton.bone_names[bone];
    row.rest_translation_length = static_cast<float>(std::sqrt(static_cast<double>(rest.translation[0]) * rest.translation[0] +
                                                               static_cast<double>(rest.translation[1]) * rest.translation[1] +
                                                               static_cast<double>(rest.translation[2]) * rest.translation[2]));
    row.rest_rotation_degrees = rotation_degrees(rest.rotation);
    row.rest_scale_deviation = static_cast<float>(std::sqrt(
        static_cast<double>(rest.scale[0] - 1.0f) * (rest.scale[0] - 1.0f) + static_cast<double>(rest.scale[1] - 1.0f) * (rest.scale[1] - 1.0f) +
        static_cast<double>(rest.scale[2] - 1.0f) * (rest.scale[2] - 1.0f)));

    const bool translation_driven = track.translation_animated[0] || track.translation_animated[1] || track.translation_animated[2];
    const bool scale_driven = track.scale_animated[0] || track.scale_animated[1] || track.scale_animated[2];
    if (!translation_driven) {
      ++out.no_translation;
      if (row.rest_translation_length > kTranslationThreshold) {
        row.translation_undriven = true;
        ++out.offset_translation;
        out.max_translation_offset = std::max(out.max_translation_offset, row.rest_translation_length);
      }
    }
    if (!track.rotation_animated) {
      ++out.no_rotation;
      if (row.rest_rotation_degrees > kRotationDegrees) {
        row.rotation_undriven = true;
        ++out.offset_rotation;
        out.max_rotation_degrees = std::max(out.max_rotation_degrees, row.rest_rotation_degrees);
      }
    }
    if (!scale_driven) {
      ++out.no_scale;
      if (row.rest_scale_deviation > kScaleThreshold) {
        row.scale_undriven = true;
        ++out.offset_scale;
      }
    }
    if (row.translation_undriven || row.rotation_undriven || row.scale_undriven) out.offsets.push_back(std::move(row));
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
