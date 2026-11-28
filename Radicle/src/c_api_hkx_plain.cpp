#include "fo4/c_api_hkx_plain.h"

#include "c_api_hkx_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/hkx/clip_roles.hpp"
#include "fo4/hkx/plain.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct radicle_hkx_plain_clip {
  fo4::hkx::HkxPlainClip value;
};

struct radicle_hkx_clip_roles {
  std::vector<fo4::hkx::HkxClipRole> value;
};

namespace {

using namespace radicle_c_detail;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::HkxStatus;
using radicle_hkx_detail::need;

constexpr CopyMessages k_string{"out_bytes and out_size are required", "failed to allocate an HKX string"};
constexpr CopyMessages k_values{"out_values and out_count are required", "failed to allocate HKX values"};

template <size_t N>
std::vector<float> flatten(const std::vector<std::array<float, N>>& items) {
  std::vector<float> values;
  values.reserve(items.size() * N);
  for (const auto& item : items) values.insert(values.end(), item.begin(), item.end());
  return values;
}

std::string text_or_empty(const char* text) { return text ? std::string(text) : std::string(); }

const fo4::hkx::HkxPlainTrack* find_track(const radicle_hkx_plain_clip* clip, size_t track, radicle_status& status) {
  if (!clip) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "clip is required");
    return nullptr;
  }
  if (track >= clip->value.tracks.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "track index is out of range");
    return nullptr;
  }
  return &clip->value.tracks[track];
}

const fo4::hkx::HkxPlainEvent* find_event(const radicle_hkx_plain_clip* clip, size_t index, radicle_status& status) {
  if (!clip) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "clip is required");
    return nullptr;
  }
  if (index >= clip->value.events.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "event index is out of range");
    return nullptr;
  }
  return &clip->value.events[index];
}

const fo4::hkx::HkxClipRole* find_role(const radicle_hkx_clip_roles* roles, size_t index, radicle_status& status) {
  if (!roles) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "roles is required");
    return nullptr;
  }
  if (index >= roles->value.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "role index is out of range");
    return nullptr;
  }
  return &roles->value[index];
}

}  // namespace

extern "C" {

radicle_status radicle_hkx_plain_skeleton_rest(const radicle_hkx* skeleton, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = need(skeleton, &radicle_hkx::skeleton, "skeleton", status);
  if (!found) return status;
  return guard_exceptions("unknown exception while converting the HKX rest pose", [&] {
    std::vector<float> values;
    for (const auto& bone : fo4::hkx::plain_rest_pose(*found)) {
      values.insert(values.end(), bone.translation.begin(), bone.translation.end());
      values.insert(values.end(), bone.rotation.begin(), bone.rotation.end());
      values.insert(values.end(), bone.scale.begin(), bone.scale.end());
    }
    return copy_out(values, out_values, out_count, k_values);
  });
}

radicle_status radicle_hkx_plain_clip_sample(const radicle_hkx* skeleton,
                                             const radicle_hkx* clip,
                                             float rate,
                                             int32_t loop,
                                             radicle_hkx_plain_clip** out_clip) {
  clear_error();
  if (out_clip) *out_clip = nullptr;
  if (!out_clip) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_clip is required");
  if (!std::isfinite(rate) || rate < 0.0f || rate > fo4::hkx::k_plain_max_rate) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "rate must be 0 or finite and at most 240");
  }
  if (loop != 0 && loop != 1) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "loop must be 0 or 1");
  radicle_status status = RADICLE_STATUS_OK;
  const auto* rig = need(skeleton, &radicle_hkx::skeleton, "skeleton", status);
  if (!rig) return status;
  const auto* animation = need(clip, &radicle_hkx::animation, "animation", status);
  if (!animation) return status;
  const auto* binding = need(clip, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;

  fo4::hkx::HkxExtractedMotion unsupported;
  unsupported.kind = HkxMotionKind::Unsupported;
  const fo4::hkx::HkxExtractedMotion* motion = nullptr;
  if (clip->motion.status == HkxStatus::Ok) {
    motion = &clip->motion.value;
  } else if (clip->motion.status != HkxStatus::MissingObject) {
    motion = &unsupported;
  }

  return guard_exceptions("unknown exception while sampling an HKX clip", [&] {
    auto handle = std::make_unique<radicle_hkx_plain_clip>();
    const auto sampled = fo4::hkx::sample_plain_clip(*rig, *animation, *binding, motion, rate, loop == 1, handle->value);
    if (sampled != HkxStatus::Ok) return radicle_hkx_detail::part_status(sampled, "sampled clip");
    *out_clip = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_hkx_plain_clip_destroy(radicle_hkx_plain_clip* clip) {
  clear_error();
  delete clip;
}

radicle_status radicle_hkx_plain_clip_read_info(const radicle_hkx_plain_clip* clip,
                                                radicle_hkx_plain_clip_info* out_info) {
  clear_error();
  if (!clip) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "clip is required");
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  const auto& value = clip->value;
  radicle_hkx_plain_clip_info out{};
  out.length = value.length;
  out.rate = value.rate;
  out.loop = value.loop ? 1 : 0;
  out.key_count = value.key_count;
  out.track_count = value.tracks.size();
  out.event_count = value.events.size();
  out.root_motion_kind = static_cast<int32_t>(value.root_motion.kind);
  if (value.root_motion.kind == HkxMotionKind::Default) {
    for (size_t axis = 0; axis < 3; ++axis) out.root_net_translation[axis] = value.root_motion.net_translation[axis];
    out.root_net_yaw = value.root_motion.net_yaw;
    out.root_mean_planar_speed = value.root_motion.mean_planar_speed;
    out.root_mean_turn_rate = value.root_motion.mean_turn_rate;
  }
  *out_info = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_plain_clip_track_bone(const radicle_hkx_plain_clip* clip, size_t track, int32_t* out_bone) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(clip, track, status);
  if (!found) return status;
  if (!out_bone) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bone is required");
  *out_bone = found->bone;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_plain_clip_track_translations(const radicle_hkx_plain_clip* clip,
                                                         size_t track,
                                                         float** out_values,
                                                         size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(clip, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying plain translations",
                          [&] { return copy_out(flatten(found->translations), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_plain_clip_track_rotations(const radicle_hkx_plain_clip* clip,
                                                      size_t track,
                                                      float** out_values,
                                                      size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(clip, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying plain rotations",
                          [&] { return copy_out(flatten(found->rotations), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_plain_clip_track_scales(const radicle_hkx_plain_clip* clip,
                                                   size_t track,
                                                   float** out_values,
                                                   size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(clip, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying plain scales",
                          [&] { return copy_out(flatten(found->scales), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_plain_clip_event_time(const radicle_hkx_plain_clip* clip, size_t index, float* out_time) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_event(clip, index, status);
  if (!found) return status;
  if (!out_time) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_time is required");
  *out_time = found->time;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_plain_clip_event_name(const radicle_hkx_plain_clip* clip,
                                                 size_t index,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_event(clip, index, status);
  if (!found) return status;
  return copy_out(found->name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_plain_clip_root_motion(const radicle_hkx_plain_clip* clip,
                                                  float** out_values,
                                                  size_t* out_count) {
  clear_error();
  if (!clip) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "clip is required");
  if (clip->value.root_motion.kind != HkxMotionKind::Default) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the clip has no root motion");
  }
  return guard_exceptions("unknown exception while copying plain root motion", [&] {
    return copy_out(flatten(clip->value.root_motion.deltas), out_values, out_count, k_values);
  });
}

radicle_status radicle_hkx_clip_roles_read(const radicle_hkx* behavior,
                                           const char* animation_root,
                                           const char* directory,
                                           radicle_hkx_clip_roles** out_roles) {
  clear_error();
  if (out_roles) *out_roles = nullptr;
  if (!out_roles) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_roles is required");
  radicle_status status = RADICLE_STATUS_OK;
  const auto* clips = need(behavior, &radicle_hkx::clips, "behavior clip generator", status);
  if (!clips) return status;
  return guard_exceptions("unknown exception while resolving HKX clip roles", [&] {
    auto handle = std::make_unique<radicle_hkx_clip_roles>();
    handle->value = fo4::hkx::resolve_clip_roles(*clips, text_or_empty(animation_root), text_or_empty(directory));
    *out_roles = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_hkx_clip_roles_destroy(radicle_hkx_clip_roles* roles) {
  clear_error();
  delete roles;
}

radicle_status radicle_hkx_clip_roles_count(const radicle_hkx_clip_roles* roles, size_t* out_count) {
  clear_error();
  if (!roles) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "roles is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = roles->value.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_clip_role_name(const radicle_hkx_clip_roles* roles,
                                          size_t index,
                                          uint8_t** out_bytes,
                                          size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_role(roles, index, status);
  if (!found) return status;
  return copy_out(found->role, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_clip_role_entry(const radicle_hkx_clip_roles* roles,
                                           size_t index,
                                           uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_role(roles, index, status);
  if (!found) return status;
  return copy_out(found->entry, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_clip_role_read_info(const radicle_hkx_clip_roles* roles,
                                               size_t index,
                                               radicle_hkx_clip_role_info* out_info) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_role(roles, index, status);
  if (!found) return status;
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  radicle_hkx_clip_role_info out{};
  out.loop = found->loop ? 1 : 0;
  out.playback_speed = found->playback_speed;
  *out_info = out;
  return RADICLE_STATUS_OK;
}

}
