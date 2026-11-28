#include "fo4/hkx/plain.hpp"

#include "fo4/hkx/pose.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::size_t k_max_keys = std::size_t{1} << 16;
constexpr std::size_t k_max_samples = std::size_t{1} << 22;
constexpr double k_fallback_rate = 30.0;

std::array<double, 4> motion_at(const HkxExtractedMotion& motion, double time) {
  const std::size_t count = motion.samples.size();
  std::array<double, 4> out{};
  if (count == 1) {
    for (std::size_t c = 0; c < 4; ++c) out[c] = motion.samples[0][c];
    return out;
  }
  const double ratio = std::clamp(time / static_cast<double>(motion.duration), 0.0, 1.0);
  const double position = ratio * static_cast<double>(count - 1);
  const std::size_t lower = std::min(static_cast<std::size_t>(position), count - 2);
  const double fraction = position - static_cast<double>(lower);
  for (std::size_t c = 0; c < 4; ++c) {
    const double a = motion.samples[lower][c];
    out[c] = a + (static_cast<double>(motion.samples[lower + 1][c]) - a) * fraction;
  }
  return out;
}

bool all_finite(const std::vector<std::array<float, 4>>& values) {
  return std::all_of(values.begin(), values.end(), [](const std::array<float, 4>& value) {
    return std::all_of(value.begin(), value.end(), [](float component) { return std::isfinite(component); });
  });
}

bool motion_usable(const HkxExtractedMotion& motion) {
  if (motion.samples.empty() || !all_finite(motion.samples)) return false;
  if (motion.samples.size() > 1 && !(std::isfinite(motion.duration) && motion.duration > 0.0f)) return false;
  return std::fabs(motion.up[0]) <= k_plain_up_tolerance && std::fabs(motion.up[1]) <= k_plain_up_tolerance &&
         std::fabs(motion.up[2] - 1.0f) <= k_plain_up_tolerance;
}

HkxPlainRootMotion plain_root_motion(const HkxExtractedMotion* motion, std::size_t key_count, double rate) {
  HkxPlainRootMotion out;
  if (motion == nullptr || motion->kind == HkxMotionKind::None) return out;
  out.kind = HkxMotionKind::Unsupported;
  if (motion->kind != HkxMotionKind::Default || !motion_usable(*motion)) return out;

  const double scale = 1.0 / static_cast<double>(k_plain_units_per_metre);
  std::vector<std::array<float, 4>> deltas;
  deltas.reserve(key_count);
  deltas.push_back({0.0f, 0.0f, 0.0f, 0.0f});
  std::array<double, 4> previous = motion_at(*motion, 0.0);
  for (std::size_t key = 1; key < key_count; ++key) {
    const std::array<double, 4> current = motion_at(*motion, static_cast<double>(key) / rate);
    const double dx = current[0] - previous[0];
    const double dy = current[1] - previous[1];
    const double dz = current[2] - previous[2];
    const double cosine = std::cos(previous[3]);
    const double sine = std::sin(previous[3]);
    const double local_x = cosine * dx + sine * dy;
    const double local_y = -sine * dx + cosine * dy;
    deltas.push_back({static_cast<float>(local_x * scale), static_cast<float>(dz * scale),
                      static_cast<float>(-local_y * scale), static_cast<float>(current[3] - previous[3])});
    previous = current;
  }
  if (!all_finite(deltas)) return out;

  const HkxMotionSummary summary = summarize_motion(*motion);
  const float reported[] = {summary.net_translation[0], summary.net_translation[1], summary.net_translation[2],
                            summary.net_turn_radians, summary.mean_planar_speed, summary.mean_turn_rate};
  if (!std::all_of(std::begin(reported), std::end(reported), [](float value) { return std::isfinite(value); })) return out;
  const float inverse = 1.0f / k_plain_units_per_metre;
  out.kind = HkxMotionKind::Default;
  out.deltas = std::move(deltas);
  out.net_translation = {summary.net_translation[0] * inverse, summary.net_translation[2] * inverse,
                         -summary.net_translation[1] * inverse};
  out.net_yaw = summary.net_turn_radians;
  out.mean_planar_speed = summary.mean_planar_speed * inverse;
  out.mean_turn_rate = summary.mean_turn_rate;
  return out;
}

}  // namespace

HkxQsTransform to_engine_axes(const HkxQsTransform& transform) {
  const float inverse = 1.0f / k_plain_units_per_metre;
  HkxQsTransform out;
  out.translation = {transform.translation[0] * inverse, transform.translation[2] * inverse,
                     -transform.translation[1] * inverse};
  out.rotation = {transform.rotation[0], transform.rotation[2], -transform.rotation[1], transform.rotation[3]};
  out.scale = {transform.scale[0], transform.scale[2], transform.scale[1]};
  return out;
}

std::vector<HkxQsTransform> plain_rest_pose(const HkxSkeleton& skeleton) {
  std::vector<HkxQsTransform> out;
  out.reserve(skeleton.reference_pose.size());
  for (const HkxQsTransform& transform : skeleton.reference_pose) out.push_back(to_engine_axes(transform));
  return out;
}

HkxStatus sample_plain_clip(const HkxSkeleton& skeleton, const HkxAnimation& animation,
                            const HkxAnimationBinding& binding, const HkxExtractedMotion* motion, float rate, bool loop,
                            HkxPlainClip& out) {
  out = HkxPlainClip{};
  if (!std::isfinite(rate) || rate < 0.0f || rate > k_plain_max_rate) return HkxStatus::BadSection;
  if (!std::isfinite(animation.duration) || animation.duration < 0.0f) return HkxStatus::BadSection;

  HkxPlainClip clip;
  clip.length = animation.duration;
  clip.loop = loop;
  double key_rate = rate;
  if (rate == 0.0f) {
    if (animation.frame_count >= 2 && animation.duration > 0.0f) {
      clip.key_count = animation.frame_count;
      key_rate = static_cast<double>(animation.frame_count - 1) / static_cast<double>(animation.duration);
    } else {
      clip.key_count = 1;
      key_rate = animation.frame_duration > 0.0f ? 1.0 / static_cast<double>(animation.frame_duration) : k_fallback_rate;
    }
  } else {
    const double span = static_cast<double>(animation.duration) * key_rate;
    if (span > static_cast<double>(k_max_keys)) return HkxStatus::BadSection;
    clip.key_count = static_cast<std::size_t>(std::llround(span)) + 1;
  }
  if (clip.key_count > k_max_keys || !std::isfinite(key_rate) || !(key_rate > 0.0)) return HkxStatus::BadSection;
  if (binding.transform_track_to_bone_indices.size() > k_max_samples / clip.key_count) return HkxStatus::BadSection;
  clip.rate = static_cast<float>(key_rate);

  std::vector<HkxQsTransform> pose;
  for (std::size_t key = 0; key < clip.key_count; ++key) {
    const auto time = static_cast<float>(static_cast<double>(key) / key_rate);
    if (const auto status = sample_pose(skeleton, animation, binding, time, pose); status != HkxStatus::Ok) {
      return status;
    }
    if (key == 0) {
      for (const std::int16_t bone : binding.transform_track_to_bone_indices) {
        HkxPlainTrack track;
        track.bone = bone;
        track.translations.reserve(clip.key_count);
        track.rotations.reserve(clip.key_count);
        track.scales.reserve(clip.key_count);
        clip.tracks.push_back(std::move(track));
      }
    }
    for (HkxPlainTrack& track : clip.tracks) {
      const HkxQsTransform engine = to_engine_axes(pose[static_cast<std::size_t>(track.bone)]);
      track.translations.push_back(engine.translation);
      track.rotations.push_back(engine.rotation);
      track.scales.push_back(engine.scale);
    }
  }

  for (const HkxAnnotationTrack& annotations : animation.annotation_tracks) {
    for (const HkxAnimationAnnotation& annotation : annotations.annotations) {
      if (!std::isfinite(annotation.time)) return HkxStatus::BadSection;
      clip.events.push_back({annotation.time, annotation.text});
    }
  }
  std::stable_sort(clip.events.begin(), clip.events.end(),
                   [](const HkxPlainEvent& a, const HkxPlainEvent& b) { return a.time < b.time; });

  clip.root_motion = plain_root_motion(motion, clip.key_count, key_rate);
  out = std::move(clip);
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
