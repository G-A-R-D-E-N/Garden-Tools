#include "fo4/c_api_hkx.h"

#include "c_api_hkx_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using fo4::hkx::HkxStatus;

constexpr CopyMessages k_string{"out_bytes and out_size are required", "failed to allocate an HKX string"};
constexpr CopyMessages k_values{"out_values and out_count are required", "failed to allocate HKX values"};

using radicle_hkx_detail::need;

template <size_t N>
std::vector<float> flatten(const std::vector<std::array<float, N>>& frames) {
  std::vector<float> values;
  values.reserve(frames.size() * N);
  for (const auto& frame : frames) values.insert(values.end(), frame.begin(), frame.end());
  return values;
}

template <typename Container>
const typename Container::value_type* pick(const Container& items,
                                           size_t index,
                                           const char* message,
                                           radicle_status& status) {
  if (index < items.size()) return &items[index];
  status = fail(RADICLE_STATUS_INVALID_ARGUMENT, message);
  return nullptr;
}

const fo4::hkx::HkxAnimationTrack* find_track(const radicle_hkx* hkx, size_t track, radicle_status& status) {
  const auto* animation = need(hkx, &radicle_hkx::animation, "animation", status);
  return animation ? pick(animation->tracks, track, "track index is out of range", status) : nullptr;
}

const fo4::hkx::HkxAnnotationTrack* find_annotation_track(const radicle_hkx* hkx,
                                                          size_t track,
                                                          radicle_status& status) {
  const auto* animation = need(hkx, &radicle_hkx::animation, "animation", status);
  return animation ? pick(animation->annotation_tracks, track, "annotation track index is out of range", status)
                   : nullptr;
}

const fo4::hkx::HkxAnimationAnnotation* find_annotation(const radicle_hkx* hkx,
                                                        size_t track,
                                                        size_t index,
                                                        radicle_status& status) {
  const auto* annotations = find_annotation_track(hkx, track, status);
  return annotations ? pick(annotations->annotations, index, "annotation index is out of range", status) : nullptr;
}

const fo4::hkx::HkxSkeleton* find_bone(const radicle_hkx* hkx, size_t index, radicle_status& status) {
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  return skeleton && pick(skeleton->bone_names, index, "bone index is out of range", status) ? skeleton : nullptr;
}

}  // namespace

extern "C" {

radicle_status radicle_hkx_read(const uint8_t* bytes, size_t size, radicle_hkx** out_hkx) {
  clear_error();
  if (out_hkx) *out_hkx = nullptr;
  if (!bytes || size == 0 || !out_hkx) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_hkx are required");
  }
  return guard_exceptions("unknown exception while reading an HKX packfile", [&] {
    fo4::hkx::HkxPackfile packfile;
    const auto status =
        fo4::hkx::read_packfile(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size), packfile);
    if (status != HkxStatus::Ok) {
      char message[96];
      std::snprintf(message, sizeof message, "the packfile could not be read, HkxStatus %d", static_cast<int>(status));
      return fail(RADICLE_STATUS_PARSE_ERROR, message);
    }
    auto handle = std::make_unique<radicle_hkx>();
    handle->skeleton.status = fo4::hkx::read_skeleton(packfile, handle->skeleton.value);
    handle->animation.status = fo4::hkx::read_animation(packfile, handle->animation.value);
    handle->binding.status = fo4::hkx::read_binding(packfile, handle->binding.value);
    handle->motion.status = fo4::hkx::read_extracted_motion(packfile, handle->motion.value);
    handle->clips.status = fo4::hkx::read_behavior_clips(packfile, handle->clips.value);
    handle->graph.status = fo4::hkx::read_behavior_model(packfile, handle->graph.value);
    *out_hkx = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_hkx_destroy(radicle_hkx* hkx) {
  clear_error();
  delete hkx;
}

radicle_status radicle_hkx_skeleton_name(const radicle_hkx* hkx, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  return copy_out(skeleton->name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_skeleton_bone_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = skeleton->bone_names.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_bone_name(const radicle_hkx* hkx,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = find_bone(hkx, index, status);
  if (!skeleton) return status;
  return copy_out(skeleton->bone_names[index], out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_skeleton_bone_parent(const radicle_hkx* hkx, size_t index, int16_t* out_parent) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = find_bone(hkx, index, status);
  if (!skeleton) return status;
  if (!out_parent) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_parent is required");
  *out_parent = skeleton->parent_indices[index];
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_bone_pose(const radicle_hkx* hkx,
                                              size_t index,
                                              radicle_hkx_qs_transform* out_pose) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = find_bone(hkx, index, status);
  if (!skeleton) return status;
  if (!out_pose) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_pose is required");
  const auto& pose = skeleton->reference_pose[index];
  std::memcpy(out_pose->translation, pose.translation.data(), sizeof out_pose->translation);
  std::memcpy(out_pose->rotation, pose.rotation.data(), sizeof out_pose->rotation);
  std::memcpy(out_pose->scale, pose.scale.data(), sizeof out_pose->scale);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_bone_lock_translation(const radicle_hkx* hkx,
                                                          size_t index,
                                                          int32_t* out_locked) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = find_bone(hkx, index, status);
  if (!skeleton) return status;
  if (!out_locked) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_locked is required");
  *out_locked = skeleton->lock_translation[index] ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_reference_floats(const radicle_hkx* hkx, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  return copy_out(skeleton->reference_floats, out_values, out_count, k_values);
}

radicle_status radicle_hkx_skeleton_float_slot_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = skeleton->float_slots.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_float_slot(const radicle_hkx* hkx,
                                               size_t index,
                                               int32_t* out_named,
                                               uint8_t** out_bytes,
                                               size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!pick(skeleton->float_slots, index, "float slot index is out of range", status)) return status;
  if (!out_named || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_named, out_bytes and out_size are required");
  }
  const auto& slot = skeleton->float_slots[index];
  *out_named = slot.named ? 1 : 0;
  if (!slot.named) {
    *out_bytes = nullptr;
    *out_size = 0;
    return RADICLE_STATUS_OK;
  }
  return copy_out(slot.name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_skeleton_local_frame_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = skeleton->local_frames.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_local_frame(const radicle_hkx* hkx,
                                                size_t index,
                                                int16_t* out_bone_index,
                                                int32_t* out_has_frame) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!pick(skeleton->local_frames, index, "local frame index is out of range", status)) return status;
  if (!out_bone_index || !out_has_frame) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bone_index and out_has_frame are required");
  }
  *out_bone_index = skeleton->local_frames[index].bone_index;
  *out_has_frame = skeleton->local_frames[index].has_frame ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_partition_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = skeleton->partitions.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_skeleton_partition(const radicle_hkx* hkx,
                                              size_t index,
                                              int32_t* out_named,
                                              uint8_t** out_bytes,
                                              size_t* out_size,
                                              int16_t* out_start_bone_index,
                                              int16_t* out_bone_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* skeleton = need(hkx, &radicle_hkx::skeleton, "skeleton", status);
  if (!skeleton) return status;
  if (!pick(skeleton->partitions, index, "partition index is out of range", status)) return status;
  if (!out_named || !out_bytes || !out_size || !out_start_bone_index || !out_bone_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "out_named, out_bytes, out_size, out_start_bone_index and out_bone_count are required");
  }
  const auto& partition = skeleton->partitions[index];
  *out_named = partition.named ? 1 : 0;
  *out_start_bone_index = partition.start_bone;
  *out_bone_count = partition.num_bones;
  if (!partition.named) {
    *out_bytes = nullptr;
    *out_size = 0;
    return RADICLE_STATUS_OK;
  }
  return copy_out(partition.name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_read_animation_header(const radicle_hkx* hkx, radicle_hkx_animation_header* out_header) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* animation = need(hkx, &radicle_hkx::animation, "animation", status);
  if (!animation) return status;
  if (!out_header) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_header is required");
  radicle_hkx_animation_header out{};
  out.kind = static_cast<int32_t>(animation->kind);
  out.type = animation->type;
  out.duration = animation->duration;
  out.frame_count = animation->frame_count;
  out.frame_duration = animation->frame_duration;
  out.float_track_count = animation->float_track_count;
  out.block_count = animation->blocks.count;
  out.block_max_frames_per_block = animation->blocks.max_frames_per_block;
  out.block_duration = animation->blocks.block_duration;
  out.track_count = animation->tracks.size();
  out.annotation_track_count = animation->annotation_tracks.size();
  *out_header = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_animation_class_name(const radicle_hkx* hkx, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* animation = need(hkx, &radicle_hkx::animation, "animation", status);
  if (!animation) return status;
  return copy_out(animation->class_name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_animation_track_translations(const radicle_hkx* hkx,
                                                        size_t track,
                                                        float** out_values,
                                                        size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(hkx, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying HKX translations",
                          [&] { return copy_out(flatten(found->translations), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_animation_track_rotations(const radicle_hkx* hkx,
                                                     size_t track,
                                                     float** out_values,
                                                     size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(hkx, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying HKX rotations",
                          [&] { return copy_out(flatten(found->rotations), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_animation_track_scales(const radicle_hkx* hkx,
                                                  size_t track,
                                                  float** out_values,
                                                  size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(hkx, track, status);
  if (!found) return status;
  return guard_exceptions("unknown exception while copying HKX scales",
                          [&] { return copy_out(flatten(found->scales), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_animation_track_flags(const radicle_hkx* hkx,
                                                 size_t track,
                                                 radicle_hkx_track_flags* out_flags) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_track(hkx, track, status);
  if (!found) return status;
  if (!out_flags) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_flags is required");
  radicle_hkx_track_flags out{};
  for (size_t axis = 0; axis < 3; ++axis) {
    out.translation_animated[axis] = found->translation_animated[axis] ? 1 : 0;
    out.scale_animated[axis] = found->scale_animated[axis] ? 1 : 0;
  }
  out.rotation_animated = found->rotation_animated ? 1 : 0;
  *out_flags = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_animation_annotation_track_name(const radicle_hkx* hkx,
                                                           size_t track,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_annotation_track(hkx, track, status);
  if (!found) return status;
  return copy_out(found->name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_animation_annotation_count(const radicle_hkx* hkx, size_t track, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_annotation_track(hkx, track, status);
  if (!found) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = found->annotations.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_animation_annotation_time(const radicle_hkx* hkx,
                                                     size_t track,
                                                     size_t index,
                                                     float* out_time) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_annotation(hkx, track, index, status);
  if (!found) return status;
  if (!out_time) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_time is required");
  *out_time = found->time;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_animation_annotation_text(const radicle_hkx* hkx,
                                                     size_t track,
                                                     size_t index,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* found = find_annotation(hkx, track, index, status);
  if (!found) return status;
  return copy_out(found->text, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_animation_float_tracks(const radicle_hkx* hkx, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* animation = need(hkx, &radicle_hkx::animation, "animation", status);
  if (!animation) return status;
  return copy_out(animation->float_tracks, out_values, out_count, k_values);
}

radicle_status radicle_hkx_read_motion_header(const radicle_hkx* hkx, radicle_hkx_motion_header* out_header) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* motion = need(hkx, &radicle_hkx::motion, "extracted motion", status);
  if (!motion) return status;
  if (!out_header) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_header is required");
  const auto summary = fo4::hkx::summarize_motion(*motion);
  radicle_hkx_motion_header out{};
  out.kind = static_cast<int32_t>(motion->kind);
  out.duration = motion->duration;
  std::memcpy(out.up, motion->up.data(), sizeof out.up);
  std::memcpy(out.forward, motion->forward.data(), sizeof out.forward);
  out.sample_count = motion->samples.size();
  std::memcpy(out.net_translation, summary.net_translation.data(), sizeof out.net_translation);
  out.net_turn_radians = summary.net_turn_radians;
  out.mean_planar_speed = summary.mean_planar_speed;
  out.mean_turn_rate = summary.mean_turn_rate;
  *out_header = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_motion_samples(const radicle_hkx* hkx, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* motion = need(hkx, &radicle_hkx::motion, "extracted motion", status);
  if (!motion) return status;
  return guard_exceptions("unknown exception while copying HKX motion samples",
                          [&] { return copy_out(flatten(motion->samples), out_values, out_count, k_values); });
}

radicle_status radicle_hkx_read_binding_header(const radicle_hkx* hkx, radicle_hkx_binding_header* out_header) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* binding = need(hkx, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;
  if (!out_header) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_header is required");
  radicle_hkx_binding_header out{};
  out.has_animation_offset = binding->animation_offset.has_value() ? 1 : 0;
  out.animation_offset = binding->animation_offset.value_or(0);
  out.blend_hint = binding->blend_hint;
  *out_header = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_binding_original_skeleton_name(const radicle_hkx* hkx,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* binding = need(hkx, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;
  return copy_out(binding->original_skeleton_name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_binding_transform_track_to_bone(const radicle_hkx* hkx,
                                                           int16_t** out_values,
                                                           size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* binding = need(hkx, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;
  return copy_out(binding->transform_track_to_bone_indices, out_values, out_count, k_values);
}

radicle_status radicle_hkx_binding_float_track_to_float_slot(const radicle_hkx* hkx,
                                                             int16_t** out_values,
                                                             size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* binding = need(hkx, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;
  return copy_out(binding->float_track_to_float_slot_indices, out_values, out_count, k_values);
}

radicle_status radicle_hkx_binding_partitions(const radicle_hkx* hkx, int16_t** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* binding = need(hkx, &radicle_hkx::binding, "animation binding", status);
  if (!binding) return status;
  return copy_out(binding->partition_indices, out_values, out_count, k_values);
}

void radicle_hkx_free_indices(int16_t* values) {
  clear_error();
  std::free(values);
}

}
