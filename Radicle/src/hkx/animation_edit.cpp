#include "fo4/hkx/animation_edit.hpp"

#include "write_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr float kMinRetimeScale = 0.01f;
constexpr float kMaxRetimeScale = 100.0f;
constexpr float kMaxMotionTravel = 1000000.0f;
constexpr float kMinAxisLengthSquared = 1e-6f;
constexpr std::size_t kMaxSamples = std::size_t{1} << 20;

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;
using Sample = std::array<float, 4>;

float step_of(const HkxAnimation& animation) {
  return animation.frame_count > 1 ? animation.duration / static_cast<float>(animation.frame_count - 1) : 0.0f;
}

bool finite(const Vec3& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

bool finite(const Quat& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]) && std::isfinite(value[3]);
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
  return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

Quat slerp(const Quat& a, Quat b, float t) {
  double dot = 0.0;
  for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(a[i]) * b[i];
  if (dot < 0.0) {
    for (float& value : b) value = -value;
    dot = -dot;
  }
  double wa = 1.0 - t;
  double wb = t;
  if (dot < 0.9995) {
    const double angle = std::acos(std::min(1.0, dot));
    const double sine = std::sin(angle);
    wa = std::sin((1.0 - t) * angle) / sine;
    wb = std::sin(t * angle) / sine;
  }
  Quat out{};
  for (std::size_t i = 0; i < 4; ++i) out[i] = static_cast<float>(wa * a[i] + wb * b[i]);
  const auto unit = detail::normalised_quaternion(out);
  return unit ? *unit : Quat{0.0f, 0.0f, 0.0f, 1.0f};
}

float rotation_gap(const Quat& a, const Quat& b) {
  const auto ua = detail::normalised_quaternion(a).value_or(Quat{0.0f, 0.0f, 0.0f, 1.0f});
  const auto ub = detail::normalised_quaternion(b).value_or(Quat{0.0f, 0.0f, 0.0f, 1.0f});
  double dot = 0.0;
  for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(ua[i]) * ub[i];
  return static_cast<float>(2.0 * std::acos(std::min(1.0, std::fabs(dot))));
}

float vector_gap(const Vec3& a, const Vec3& b) {
  const double x = static_cast<double>(a[0]) - b[0];
  const double y = static_cast<double>(a[1]) - b[1];
  const double z = static_cast<double>(a[2]) - b[2];
  return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

Vec3 between(const std::vector<Vec3>& frames, float at) {
  if (frames.size() == 1) return frames[0];
  const float where = std::clamp(at, 0.0f, 1.0f) * static_cast<float>(frames.size() - 1);
  const std::size_t first = std::min(static_cast<std::size_t>(where), frames.size() - 2);
  return lerp(frames[first], frames[first + 1], where - static_cast<float>(first));
}

Quat turned(const std::vector<Quat>& frames, float at) {
  if (frames.size() == 1) return frames[0];
  const float where = std::clamp(at, 0.0f, 1.0f) * static_cast<float>(frames.size() - 1);
  const std::size_t first = std::min(static_cast<std::size_t>(where), frames.size() - 2);
  return slerp(frames[first], frames[first + 1], where - static_cast<float>(first));
}

// Requires a clip from open_clip_edit or one built to the same shape: every track holds one sample per frame.
HkxEditStatus ready(const HkxClipEdit& clip, std::size_t least_frames) {
  const HkxAnimation& animation = clip.animation;
  if (animation.kind == HkxAnimationKind::View || animation.tracks.empty() || animation.frame_count < least_frames ||
      animation.float_track_count != 0 || !animation.float_tracks.empty()) {
    return HkxEditStatus::UnsupportedClip;
  }
  if (!std::isfinite(animation.duration) || (animation.frame_count > 1 && !(animation.duration > 0.0f))) {
    return HkxEditStatus::UnsupportedClip;
  }
  for (const HkxAnimationTrack& track : animation.tracks) {
    if (track.translations.size() != animation.frame_count || track.rotations.size() != animation.frame_count ||
        track.scales.size() != animation.frame_count) {
      return HkxEditStatus::UnsupportedClip;
    }
  }
  return HkxEditStatus::Ok;
}

bool has_samples(const HkxExtractedMotion& motion) {
  return motion.kind == HkxMotionKind::Default && !motion.samples.empty();
}

// Constraint: root motion that is not decodable or not finite must be refused, never kept or zeroed.
HkxEditStatus motion_ready(const HkxExtractedMotion& motion) {
  if (motion.kind == HkxMotionKind::Unsupported) return HkxEditStatus::UnsupportedMotion;
  if (motion.kind == HkxMotionKind::None) return HkxEditStatus::Ok;
  for (const Sample& sample : motion.samples) {
    if (!finite(sample)) return HkxEditStatus::UnsupportedMotion;
    if (std::fabs(sample[0]) > kMaxMotionTravel || std::fabs(sample[1]) > kMaxMotionTravel ||
        std::fabs(sample[2]) > kMaxMotionTravel) {
      return HkxEditStatus::UnsupportedMotion;
    }
  }
  return HkxEditStatus::Ok;
}

Sample motion_at(const HkxExtractedMotion& motion, float fraction) {
  if (motion.samples.size() == 1) return motion.samples[0];
  const float where = std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(motion.samples.size() - 1);
  const std::size_t first = std::min(static_cast<std::size_t>(where), motion.samples.size() - 1);
  const std::size_t second = std::min(first + 1, motion.samples.size() - 1);
  const float t = where - static_cast<float>(first);
  Sample out{};
  for (std::size_t i = 0; i < 4; ++i) out[i] = motion.samples[first][i] + (motion.samples[second][i] - motion.samples[first][i]) * t;
  return out;
}

// Invariant: the first sample becomes the origin, so a trimmed clip starts at rest facing the same way.
std::vector<Sample> rebased(const std::vector<Sample>& taken, const std::array<float, 3>& up) {
  std::vector<Sample> out;
  if (taken.empty()) return out;
  std::array<double, 3> axis{up[0], up[1], up[2]};
  const double length_squared = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
  if (length_squared > kMinAxisLengthSquared) {
    const double length = std::sqrt(length_squared);
    for (double& value : axis) value /= length;
  } else {
    axis = {0.0, 0.0, 1.0};
  }
  const Sample first = taken[0];
  const double angle = -static_cast<double>(first[3]);
  const double c = std::cos(angle);
  const double s = std::sin(angle);
  for (const Sample& sample : taken) {
    const std::array<double, 3> v{static_cast<double>(sample[0]) - first[0], static_cast<double>(sample[1]) - first[1],
                                  static_cast<double>(sample[2]) - first[2]};
    const double dot = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
    const std::array<double, 3> cross{axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2],
                                      axis[0] * v[1] - axis[1] * v[0]};
    Sample turned_sample{};
    for (std::size_t i = 0; i < 3; ++i) {
      turned_sample[i] = static_cast<float>(v[i] * c + cross[i] * s + axis[i] * dot * (1.0 - c));
    }
    turned_sample[3] = sample[3] - first[3];
    out.push_back(turned_sample);
  }
  return out;
}

std::size_t nearest_frame(float time, float step) {
  if (!(step > 0.0f) || !(time > 0.0f)) return 0;
  return static_cast<std::size_t>(std::floor(time / step + 0.5f));
}

void erase_frames(HkxAnimation& animation, std::size_t first, std::size_t count) {
  for (HkxAnimationTrack& track : animation.tracks) {
    const auto at = static_cast<std::ptrdiff_t>(first);
    const auto to = static_cast<std::ptrdiff_t>(first + count);
    track.translations.erase(track.translations.begin() + at, track.translations.begin() + to);
    track.rotations.erase(track.rotations.begin() + at, track.rotations.begin() + to);
    track.scales.erase(track.scales.begin() + at, track.scales.begin() + to);
  }
}

bool text_ok(std::string_view text) { return text.find('\0') == std::string_view::npos; }

// Invariant: a track already in time order keeps that order; an unordered one gets the new event at its end.
void place_annotation(HkxAnnotationTrack& track, HkxAnimationAnnotation event) {
  const bool sorted = std::is_sorted(track.annotations.begin(), track.annotations.end(),
                                     [](const HkxAnimationAnnotation& a, const HkxAnimationAnnotation& b) {
                                       return a.time < b.time;
                                     });
  if (!sorted) {
    track.annotations.push_back(std::move(event));
    return;
  }
  const auto at = std::upper_bound(track.annotations.begin(), track.annotations.end(), event.time,
                                   [](float time, const HkxAnimationAnnotation& other) { return time < other.time; });
  track.annotations.insert(at, std::move(event));
}

bool time_ok(const HkxAnimation& animation, float time) {
  return std::isfinite(time) && time >= 0.0f && time <= animation.duration;
}

}  // namespace

const char* hkx_edit_status_name(HkxEditStatus status) {
  switch (status) {
    case HkxEditStatus::Ok: return "Ok";
    case HkxEditStatus::BadPackfile: return "BadPackfile";
    case HkxEditStatus::UnsupportedClip: return "UnsupportedClip";
    case HkxEditStatus::UnsupportedMotion: return "UnsupportedMotion";
    case HkxEditStatus::BindingMismatch: return "BindingMismatch";
    case HkxEditStatus::BadRange: return "BadRange";
    case HkxEditStatus::BadValue: return "BadValue";
    case HkxEditStatus::NotRebuildable: return "NotRebuildable";
    case HkxEditStatus::BudgetExceeded: return "BudgetExceeded";
    case HkxEditStatus::WriteFailed: return "WriteFailed";
  }
  return "Unknown";
}

HkxEditStatus trim_clip(HkxClipEdit& clip, std::size_t first_frame, std::size_t last_frame, HkxEditReport* report) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (const auto status = motion_ready(clip.motion); status != HkxEditStatus::Ok) return status;
  const std::size_t frames = clip.animation.frame_count;
  if (first_frame > last_frame || last_frame >= frames || last_frame - first_frame < 1) return HkxEditStatus::BadRange;

  HkxClipEdit work = clip;
  HkxAnimation& animation = work.animation;
  const std::size_t kept = last_frame - first_frame + 1;
  const float step = step_of(clip.animation);
  const float from = static_cast<float>(first_frame) * step;
  const float to = static_cast<float>(last_frame) * step;
  const float duration = static_cast<float>(kept - 1) * step;

  erase_frames(animation, last_frame + 1, frames - last_frame - 1);
  erase_frames(animation, 0, first_frame);
  animation.frame_count = kept;
  animation.duration = duration;
  animation.frame_duration = step;

  std::size_t dropped = 0;
  const float slack = step > 0.0f ? step / 2.0f : 1e-4f;
  for (HkxAnnotationTrack& track : animation.annotation_tracks) {
    std::vector<HkxAnimationAnnotation> inside;
    for (HkxAnimationAnnotation& note : track.annotations) {
      if (note.time < from - slack || note.time > to + slack) {
        ++dropped;
        continue;
      }
      note.time = std::clamp(note.time - from, 0.0f, duration);
      inside.push_back(std::move(note));
    }
    track.annotations = std::move(inside);
  }

  if (has_samples(clip.motion)) {
    const HkxExtractedMotion& motion = clip.motion;
    const float last = static_cast<float>(frames - 1);
    std::vector<Sample> taken;
    if (motion.samples.size() == frames) {
      taken.assign(motion.samples.begin() + static_cast<std::ptrdiff_t>(first_frame),
                   motion.samples.begin() + static_cast<std::ptrdiff_t>(last_frame + 1));
    } else if (motion.samples.size() == 2) {
      taken.push_back(motion_at(motion, static_cast<float>(first_frame) / last));
      taken.push_back(motion_at(motion, static_cast<float>(last_frame) / last));
    } else {
      for (std::size_t f = 0; f < kept; ++f) taken.push_back(motion_at(motion, static_cast<float>(first_frame + f) / last));
    }
    work.motion.samples = rebased(taken, motion.up);
  }
  if (work.motion.kind == HkxMotionKind::Default) work.motion.duration = duration;

  if (report != nullptr) report->annotations_dropped = dropped;
  clip = std::move(work);
  return HkxEditStatus::Ok;
}

HkxEditStatus retime_clip(HkxClipEdit& clip, float scale, bool keep_frame_rate, const HkxRetimeBudget& budget,
                          HkxEditReport* report) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (!std::isfinite(scale) || scale < kMinRetimeScale || scale > kMaxRetimeScale) return HkxEditStatus::BadValue;
  if (const auto status = motion_ready(clip.motion); status != HkxEditStatus::Ok) return status;

  const HkxAnimation& source = clip.animation;
  const std::size_t was_frames = source.frame_count;
  const float step = step_of(source);
  const float was = static_cast<float>(was_frames - 1) * step;
  const std::size_t frames =
      keep_frame_rate
          ? std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(static_cast<double>(was_frames - 1) * scale)) + 1)
          : was_frames;
  if (frames > kMaxSamples / source.tracks.size()) return HkxEditStatus::BadRange;
  const float per_frame = keep_frame_rate ? step : step * scale;
  const float duration = static_cast<float>(frames - 1) * per_frame;
  const float happened = was > 0.0f ? duration / was : scale;
  const bool resampled = frames != was_frames;
  if (!std::isfinite(duration) || !(duration > 0.0f)) return HkxEditStatus::BadValue;

  HkxClipEdit work = clip;
  HkxAnimation& made = work.animation;
  float position_error = 0.0f;
  float rotation_error = 0.0f;
  float scale_error = 0.0f;
  if (resampled) {
    for (std::size_t t = 0; t < source.tracks.size(); ++t) {
      const HkxAnimationTrack& old_track = source.tracks[t];
      HkxAnimationTrack& track = made.tracks[t];
      track.translations.clear();
      track.rotations.clear();
      track.scales.clear();
      for (std::size_t f = 0; f < frames; ++f) {
        const float at = static_cast<float>(f) / static_cast<float>(frames - 1);
        track.translations.push_back(between(old_track.translations, at));
        track.rotations.push_back(turned(old_track.rotations, at));
        track.scales.push_back(between(old_track.scales, at));
      }
      for (std::size_t f = 0; f < was_frames; ++f) {
        const float at = static_cast<float>(f) / static_cast<float>(was_frames - 1);
        position_error = std::max(position_error, vector_gap(between(track.translations, at), old_track.translations[f]));
        rotation_error = std::max(rotation_error, rotation_gap(turned(track.rotations, at), old_track.rotations[f]));
        scale_error = std::max(scale_error, vector_gap(between(track.scales, at), old_track.scales[f]));
      }
    }
  }
  if (position_error > budget.position || rotation_error > budget.rotation || scale_error > budget.scale) {
    return HkxEditStatus::BudgetExceeded;
  }

  made.frame_count = frames;
  made.duration = duration;
  made.frame_duration = per_frame;
  for (HkxAnnotationTrack& track : made.annotation_tracks) {
    for (HkxAnimationAnnotation& note : track.annotations) note.time = std::clamp(note.time * happened, 0.0f, duration);
  }
  if (work.motion.kind == HkxMotionKind::Default) {
    work.motion.duration = duration;
    if (resampled && has_samples(clip.motion) && clip.motion.samples.size() == was_frames) {
      work.motion.samples.clear();
      for (std::size_t f = 0; f < frames; ++f) {
        work.motion.samples.push_back(motion_at(clip.motion, static_cast<float>(f) / static_cast<float>(frames - 1)));
      }
    }
  }

  if (report != nullptr) {
    report->resampled = resampled;
    report->position_error = position_error;
    report->rotation_error = rotation_error;
    report->scale_error = scale_error;
  }
  clip = std::move(work);
  return HkxEditStatus::Ok;
}

HkxEditStatus loop_clip(HkxClipEdit& clip, std::size_t blend_frames, HkxEditReport* report) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (blend_frames < 1 || blend_frames > clip.animation.frame_count - 1) return HkxEditStatus::BadRange;

  HkxClipEdit work = clip;
  const std::size_t first = clip.animation.frame_count - blend_frames;
  float worst_position = 0.0f;
  float worst_turn = 0.0f;
  for (HkxAnimationTrack& track : work.animation.tracks) {
    const Vec3 anchor_position = track.translations[0];
    const Quat anchor_rotation = track.rotations[0];
    const Vec3 anchor_scale = track.scales[0];
    for (std::size_t k = 0; k < blend_frames; ++k) {
      const std::size_t index = first + k;
      const float at = static_cast<float>(k + 1) / static_cast<float>(blend_frames);
      const Vec3 moved = lerp(track.translations[index], anchor_position, at);
      worst_position = std::max(worst_position, vector_gap(moved, track.translations[index]));
      track.translations[index] = moved;
      const Quat eased = slerp(track.rotations[index], anchor_rotation, at);
      worst_turn = std::max(worst_turn, rotation_gap(eased, track.rotations[index]));
      track.rotations[index] = eased;
      track.scales[index] = lerp(track.scales[index], anchor_scale, at);
    }
  }
  if (report != nullptr) {
    report->position_error = worst_position;
    report->rotation_error = worst_turn;
  }
  clip = std::move(work);
  return HkxEditStatus::Ok;
}

HkxEditStatus set_key(HkxClipEdit& clip, std::size_t track, std::size_t frame, const HkxKey& key) {
  if (const auto status = ready(clip, 1); status != HkxEditStatus::Ok) return status;
  if (track >= clip.animation.tracks.size() || frame >= clip.animation.frame_count) return HkxEditStatus::BadRange;
  if (!key.translation && !key.rotation && !key.scale) return HkxEditStatus::BadValue;
  if (key.translation && !finite(*key.translation)) return HkxEditStatus::BadValue;
  if (key.scale && !finite(*key.scale)) return HkxEditStatus::BadValue;
  std::optional<Quat> rotation;
  if (key.rotation) {
    const Quat& q = *key.rotation;
    if (!finite(q)) return HkxEditStatus::BadValue;
    const double length_squared = static_cast<double>(q[0]) * q[0] + static_cast<double>(q[1]) * q[1] +
                                  static_cast<double>(q[2]) * q[2] + static_cast<double>(q[3]) * q[3];
    if (!(length_squared > 1e-12)) return HkxEditStatus::BadValue;
    rotation = detail::normalised_quaternion(q);
    if (!rotation) return HkxEditStatus::BadValue;
  }
  HkxAnimationTrack& target = clip.animation.tracks[track];
  if (key.translation) target.translations[frame] = *key.translation;
  if (rotation) target.rotations[frame] = *rotation;
  if (key.scale) target.scales[frame] = *key.scale;
  return HkxEditStatus::Ok;
}

HkxEditStatus copy_pose(HkxClipEdit& clip, std::size_t from_frame, std::size_t to_frame) {
  if (const auto status = ready(clip, 1); status != HkxEditStatus::Ok) return status;
  const std::size_t frames = clip.animation.frame_count;
  if (from_frame >= frames || to_frame >= frames || from_frame == to_frame) return HkxEditStatus::BadRange;
  for (HkxAnimationTrack& track : clip.animation.tracks) {
    track.translations[to_frame] = track.translations[from_frame];
    track.rotations[to_frame] = track.rotations[from_frame];
    track.scales[to_frame] = track.scales[from_frame];
  }
  return HkxEditStatus::Ok;
}

HkxEditStatus insert_frames(HkxClipEdit& clip, std::size_t at, std::size_t count) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (const auto status = motion_ready(clip.motion); status != HkxEditStatus::Ok) return status;
  if (has_samples(clip.motion)) return HkxEditStatus::NotRebuildable;
  const std::size_t frames = clip.animation.frame_count;
  if (at > frames || count < 1 || count > kMaxSamples) return HkxEditStatus::BadRange;
  if (frames + count > kMaxSamples / clip.animation.tracks.size()) return HkxEditStatus::BadRange;

  HkxClipEdit work = clip;
  HkxAnimation& animation = work.animation;
  const float step = step_of(clip.animation);
  const std::size_t held = at == 0 ? 0 : at - 1;
  for (HkxAnimationTrack& track : animation.tracks) {
    const auto where = static_cast<std::ptrdiff_t>(at);
    const auto n = static_cast<std::ptrdiff_t>(count);
    const Vec3 translation = track.translations[held];
    const Quat rotation = track.rotations[held];
    const Vec3 scale = track.scales[held];
    track.translations.insert(track.translations.begin() + where, static_cast<std::size_t>(n), translation);
    track.rotations.insert(track.rotations.begin() + where, static_cast<std::size_t>(n), rotation);
    track.scales.insert(track.scales.begin() + where, static_cast<std::size_t>(n), scale);
  }
  animation.frame_count = frames + count;
  animation.duration = static_cast<float>(frames + count - 1) * step;
  animation.frame_duration = step;
  const float shift = static_cast<float>(count) * step;
  for (HkxAnnotationTrack& track : animation.annotation_tracks) {
    for (HkxAnimationAnnotation& note : track.annotations) {
      if (nearest_frame(note.time, step) >= at) note.time = std::min(note.time + shift, animation.duration);
    }
  }
  clip = std::move(work);
  return HkxEditStatus::Ok;
}

HkxEditStatus delete_frames(HkxClipEdit& clip, std::size_t first, std::size_t count, HkxEditReport* report) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (const auto status = motion_ready(clip.motion); status != HkxEditStatus::Ok) return status;
  if (has_samples(clip.motion)) return HkxEditStatus::NotRebuildable;
  const std::size_t frames = clip.animation.frame_count;
  if (count < 1 || first >= frames || count > frames - first || frames - count < 2) return HkxEditStatus::BadRange;

  HkxClipEdit work = clip;
  HkxAnimation& animation = work.animation;
  const float step = step_of(clip.animation);
  erase_frames(animation, first, count);
  animation.frame_count = frames - count;
  animation.duration = static_cast<float>(frames - count - 1) * step;
  animation.frame_duration = step;
  const float shift = static_cast<float>(count) * step;
  std::size_t dropped = 0;
  for (HkxAnnotationTrack& track : animation.annotation_tracks) {
    std::vector<HkxAnimationAnnotation> kept;
    for (HkxAnimationAnnotation& note : track.annotations) {
      const std::size_t frame = nearest_frame(note.time, step);
      if (frame >= first && frame < first + count) {
        ++dropped;
        continue;
      }
      if (frame >= first + count) note.time = std::clamp(note.time - shift, 0.0f, animation.duration);
      kept.push_back(std::move(note));
    }
    track.annotations = std::move(kept);
  }
  if (report != nullptr) report->annotations_dropped = dropped;
  clip = std::move(work);
  return HkxEditStatus::Ok;
}

HkxEditStatus move_frames(HkxClipEdit& clip, std::size_t first, std::size_t count, std::size_t to) {
  if (const auto status = ready(clip, 2); status != HkxEditStatus::Ok) return status;
  if (const auto status = motion_ready(clip.motion); status != HkxEditStatus::Ok) return status;
  if (has_samples(clip.motion)) return HkxEditStatus::NotRebuildable;
  const std::size_t frames = clip.animation.frame_count;
  if (count < 1 || first >= frames || count > frames - first || to >= frames || count > frames - to || to == first) {
    return HkxEditStatus::BadRange;
  }
  const auto rotate_block = [&](auto& values) {
    const auto base = values.begin();
    if (to > first) {
      std::rotate(base + static_cast<std::ptrdiff_t>(first), base + static_cast<std::ptrdiff_t>(first + count),
                  base + static_cast<std::ptrdiff_t>(to + count));
    } else {
      std::rotate(base + static_cast<std::ptrdiff_t>(to), base + static_cast<std::ptrdiff_t>(first),
                  base + static_cast<std::ptrdiff_t>(first + count));
    }
  };
  for (HkxAnimationTrack& track : clip.animation.tracks) {
    rotate_block(track.translations);
    rotate_block(track.rotations);
    rotate_block(track.scales);
  }
  return HkxEditStatus::Ok;
}

HkxEditStatus add_annotation(HkxClipEdit& clip, std::string_view track_name, float time, std::string_view text) {
  if (!time_ok(clip.animation, time) || !text_ok(text) || !text_ok(track_name)) return HkxEditStatus::BadValue;
  auto& tracks = clip.animation.annotation_tracks;
  auto found = std::find_if(tracks.begin(), tracks.end(),
                            [&](const HkxAnnotationTrack& track) { return track.name == track_name; });
  if (found == tracks.end()) {
    tracks.push_back(HkxAnnotationTrack{std::string(track_name), {}});
    found = tracks.end() - 1;
  }
  place_annotation(*found, HkxAnimationAnnotation{time, std::string(text)});
  return HkxEditStatus::Ok;
}

HkxEditStatus edit_annotation(HkxClipEdit& clip, std::size_t track, std::size_t index, float time,
                              std::string_view text) {
  auto& tracks = clip.animation.annotation_tracks;
  if (track >= tracks.size() || index >= tracks[track].annotations.size()) return HkxEditStatus::BadRange;
  if (!time_ok(clip.animation, time) || !text_ok(text)) return HkxEditStatus::BadValue;
  auto& events = tracks[track].annotations;
  events.erase(events.begin() + static_cast<std::ptrdiff_t>(index));
  place_annotation(tracks[track], HkxAnimationAnnotation{time, std::string(text)});
  return HkxEditStatus::Ok;
}

HkxEditStatus remove_annotation(HkxClipEdit& clip, std::size_t track, std::size_t index) {
  auto& tracks = clip.animation.annotation_tracks;
  if (track >= tracks.size() || index >= tracks[track].annotations.size()) return HkxEditStatus::BadRange;
  auto& events = tracks[track].annotations;
  events.erase(events.begin() + static_cast<std::ptrdiff_t>(index));
  return HkxEditStatus::Ok;
}

}  // namespace fo4::hkx
