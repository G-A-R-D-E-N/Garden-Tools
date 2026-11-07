#include "fo4/hkx/pose.hpp"

#include "spline_eval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::normalize_quat;

struct FrameSample {
  std::size_t lower{0};
  std::size_t upper{0};
  float fraction{0.0f};
};

FrameSample frame_at(const HkxAnimation& animation, float time) {
  if (animation.frame_count <= 1 || !(animation.duration > 0.0f)) return {};
  const float ratio = std::clamp(time, 0.0f, animation.duration) / animation.duration;
  const float position = ratio * static_cast<float>(animation.frame_count - 1);
  const std::size_t lower = std::min(static_cast<std::size_t>(position), animation.frame_count - 2);
  return {lower, lower + 1, position - static_cast<float>(lower)};
}

template <std::size_t N>
std::array<float, N> blend(const std::array<float, N>& a, const std::array<float, N>& b, float t) {
  std::array<float, N> out{};
  for (std::size_t c = 0; c < N; ++c) out[c] = a[c] + (b[c] - a[c]) * t;
  return out;
}

std::array<float, 4> interpolate_rotation(const std::array<float, 4>& a, std::array<float, 4> b, float t) {
  const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0f) {
    for (float& value : b) value = -value;
  }
  return normalize_quat(blend(a, b, t));
}

std::array<float, 3> rotate_vector(const std::array<float, 4>& q, const std::array<float, 3>& v) {
  const float x = q[0];
  const float y = q[1];
  const float z = q[2];
  const float w = q[3];
  const float tx = 2.0f * (y * v[2] - z * v[1]);
  const float ty = 2.0f * (z * v[0] - x * v[2]);
  const float tz = 2.0f * (x * v[1] - y * v[0]);
  return {v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx)};
}

HkxQsTransform compose(const HkxQsTransform& parent, const HkxQsTransform& child) {
  const auto& p = parent.rotation;
  const auto& r = child.rotation;
  const std::array<float, 4> rotation = normalize_quat({
      p[3] * r[0] + p[0] * r[3] + p[1] * r[2] - p[2] * r[1],
      p[3] * r[1] - p[0] * r[2] + p[1] * r[3] + p[2] * r[0],
      p[3] * r[2] + p[0] * r[1] - p[1] * r[0] + p[2] * r[3],
      p[3] * r[3] - p[0] * r[0] - p[1] * r[1] - p[2] * r[2],
  });
  const std::array<float, 3> moved = rotate_vector(p, child.translation);
  HkxQsTransform out{};
  out.rotation = rotation;
  for (std::size_t c = 0; c < 3; ++c) {
    out.translation[c] = parent.translation[c] + moved[c];
    out.scale[c] = parent.scale[c] * child.scale[c];
  }
  return out;
}

}  // namespace

HkxStatus sample_pose(const HkxSkeleton& skeleton, const HkxAnimation& animation, const HkxAnimationBinding& binding,
                      float time, std::vector<HkxQsTransform>& out) {
  const std::size_t bones = skeleton.bone_names.size();
  if (skeleton.reference_pose.size() != bones || !std::isfinite(time)) return HkxStatus::BadSection;
  if (animation.kind == HkxAnimationKind::View) return HkxStatus::UnsupportedLayout;
  const std::size_t tracks = animation.tracks.size();
  if (binding.transform_track_to_bone_indices.size() != tracks) return HkxStatus::BadSection;
  if (animation.frame_count == 0 && tracks != 0) return HkxStatus::BadSection;
  for (std::size_t track = 0; track < tracks; ++track) {
    const HkxAnimationTrack& sampled = animation.tracks[track];
    if (sampled.translations.size() != animation.frame_count || sampled.rotations.size() != animation.frame_count ||
        sampled.scales.size() != animation.frame_count) {
      return HkxStatus::BadSection;
    }
    const std::int16_t bone = binding.transform_track_to_bone_indices[track];
    if (bone < 0 || static_cast<std::size_t>(bone) >= bones) return HkxStatus::BadSection;
  }

  std::vector<HkxQsTransform> pose = skeleton.reference_pose;
  const FrameSample frame = frame_at(animation, time);
  for (std::size_t track = 0; track < tracks; ++track) {
    const HkxAnimationTrack& sampled = animation.tracks[track];
    HkxQsTransform& into = pose[static_cast<std::size_t>(binding.transform_track_to_bone_indices[track])];
    into.translation = blend(sampled.translations[frame.lower], sampled.translations[frame.upper], frame.fraction);
    into.rotation =
        interpolate_rotation(sampled.rotations[frame.lower], sampled.rotations[frame.upper], frame.fraction);
    into.scale = blend(sampled.scales[frame.lower], sampled.scales[frame.upper], frame.fraction);
  }
  out = std::move(pose);
  return HkxStatus::Ok;
}

HkxStatus model_space_pose(const HkxSkeleton& skeleton, std::span<const HkxQsTransform> local,
                           std::vector<HkxQsTransform>& out) {
  const std::size_t bones = skeleton.bone_names.size();
  if (skeleton.parent_indices.size() != bones || local.size() != bones) return HkxStatus::BadSection;
  std::vector<HkxQsTransform> pose(bones);
  for (std::size_t bone = 0; bone < bones; ++bone) {
    const std::int16_t parent = skeleton.parent_indices[bone];
    if (parent == -1) {
      pose[bone] = local[bone];
      continue;
    }
    if (parent < 0 || static_cast<std::size_t>(parent) >= bone) return HkxStatus::BadSection;
    pose[bone] = compose(pose[static_cast<std::size_t>(parent)], local[bone]);
  }
  out = std::move(pose);
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
