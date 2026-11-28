#include "fo4/hkx/root_motion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fo4::hkx {
namespace {

constexpr float kRadiansToDegrees = 57.29577951308232f;

float length_of(const std::array<float, 3>& vector) {
  return std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
}

std::array<float, 4> conjugate(const std::array<float, 4>& quaternion) {
  return {-quaternion[0], -quaternion[1], -quaternion[2], quaternion[3]};
}

std::array<float, 4> product(const std::array<float, 4>& left, const std::array<float, 4>& right) {
  return {
      left[3] * right[0] + left[0] * right[3] + left[1] * right[2] - left[2] * right[1],
      left[3] * right[1] - left[0] * right[2] + left[1] * right[3] + left[2] * right[0],
      left[3] * right[2] + left[0] * right[1] - left[1] * right[0] + left[2] * right[3],
      left[3] * right[3] - left[0] * right[0] - left[1] * right[1] - left[2] * right[2],
  };
}

// Requires two unit quaternions; the twist about +Z is the heading a root bone turns through.
float yaw_between(const std::array<float, 4>& first, const std::array<float, 4>& last) {
  const auto relative = product(last, conjugate(first));
  const float degrees = 2.0f * std::atan2(relative[2], relative[3]) * kRadiansToDegrees;
  if (degrees > 180.0f) return degrees - 360.0f;
  if (degrees <= -180.0f) return degrees + 360.0f;
  return degrees;
}

}  // namespace

HkxStatus read_root_motion(const HkxAnimation& animation, const HkxAnimationBinding& binding,
                           std::int32_t bone, std::size_t sample_limit, HkxRootMotion& out) {
  out = HkxRootMotion{};
  out.bone = bone;
  out.duration = animation.duration;
  out.frame_duration = animation.frame_duration;

  std::optional<std::size_t> track;
  for (std::size_t index = 0; index < binding.transform_track_to_bone_indices.size(); ++index) {
    if (binding.transform_track_to_bone_indices[index] == bone) {
      track = index;
      break;
    }
  }
  if (!track || *track >= animation.tracks.size()) return HkxStatus::MissingObject;
  const HkxAnimationTrack& found = animation.tracks[*track];
  // Constraint: a track with no translation sample has no motion to report, so it stays a missing track.
  if (found.translations.empty()) return HkxStatus::MissingObject;

  out.has_track = true;
  out.track = *track;
  out.frame_count = found.translations.size();

  const auto& first = found.translations.front();
  const auto& last = found.translations.back();
  for (std::size_t axis = 0; axis < 3; ++axis) out.total[axis] = last[axis] - first[axis];
  out.loop_gap = length_of(out.total);
  out.loops = out.loop_gap <= kRootMotionLoopTolerance;
  if (animation.duration > 0.0f) {
    for (std::size_t axis = 0; axis < 3; ++axis) out.velocity[axis] = out.total[axis] / animation.duration;
  }

  float distance = 0.0f;
  const std::size_t shown = std::min(out.frame_count, sample_limit);
  out.samples.reserve(shown);
  for (std::size_t frame = 0; frame < out.frame_count; ++frame) {
    std::array<float, 3> delta{};
    if (frame > 0) {
      const auto& previous = found.translations[frame - 1];
      const auto& current = found.translations[frame];
      for (std::size_t axis = 0; axis < 3; ++axis) delta[axis] = current[axis] - previous[axis];
      distance += length_of(delta);
    }
    // Constraint: the sample list is one page of frames, because a long clip would otherwise fill the reply.
    if (frame < shown) {
      out.samples.push_back({frame, static_cast<float>(frame) * animation.frame_duration,
                             found.translations[frame], delta});
    }
  }
  out.distance = distance;

  if (out.frame_count >= 2 && found.rotations.size() == out.frame_count) {
    out.yaw_degrees = yaw_between(found.rotations.front(), found.rotations.back());
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
