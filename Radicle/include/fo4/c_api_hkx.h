#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_nif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_hkx radicle_hkx;

typedef struct radicle_hkx_qs_transform {
  float translation[3];
  float rotation[4];
  float scale[3];
} radicle_hkx_qs_transform;

/* Invariant: kind is 0 interleaved, 1 spline, 2 view, 3 lossless, 4 predictive or 5 quantized; a view clip is an undecoded class with no tracks. */
typedef struct radicle_hkx_animation_header {
  int32_t kind;
  int32_t type;
  float duration;
  size_t frame_count;
  float frame_duration;
  int32_t float_track_count;
  size_t block_count;
  size_t block_max_frames_per_block;
  float block_duration;
  size_t track_count;
  size_t annotation_track_count;
} radicle_hkx_animation_header;

typedef struct radicle_hkx_track_flags {
  uint8_t translation_animated[3];
  uint8_t rotation_animated;
  uint8_t scale_animated[3];
} radicle_hkx_track_flags;

typedef struct radicle_hkx_binding_header {
  int32_t has_animation_offset;
  size_t animation_offset;
  int32_t blend_hint;
} radicle_hkx_binding_header;

/* Invariant: kind is 0 none, 1 default or 2 unsupported; net_* and mean_* are zero unless kind is 1. */
typedef struct radicle_hkx_motion_header {
  int32_t kind;
  float duration;
  float up[3];
  float forward[3];
  size_t sample_count;
  float net_translation[3];
  float net_turn_radians;
  float mean_planar_speed;
  float mean_turn_rate;
} radicle_hkx_motion_header;

/* Requires a packfile: the handle decodes it once and each read below answers RADICLE_STATUS_NOT_FOUND for a part it lacks. */
RADICLE_API radicle_status radicle_hkx_read(const uint8_t* bytes, size_t size, radicle_hkx** out_hkx);
RADICLE_API void radicle_hkx_destroy(radicle_hkx* hkx);

RADICLE_API radicle_status radicle_hkx_skeleton_name(const radicle_hkx* hkx, uint8_t** out_bytes, size_t* out_size);
RADICLE_API radicle_status radicle_hkx_skeleton_bone_count(const radicle_hkx* hkx, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_skeleton_bone_name(const radicle_hkx* hkx,
                                                          size_t index,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_hkx_skeleton_bone_parent(const radicle_hkx* hkx, size_t index, int16_t* out_parent);
RADICLE_API radicle_status radicle_hkx_skeleton_bone_pose(const radicle_hkx* hkx,
                                                          size_t index,
                                                          radicle_hkx_qs_transform* out_pose);
RADICLE_API radicle_status radicle_hkx_skeleton_bone_lock_translation(const radicle_hkx* hkx,
                                                                     size_t index,
                                                                     int32_t* out_locked);
RADICLE_API radicle_status radicle_hkx_skeleton_reference_floats(const radicle_hkx* hkx,
                                                                float** out_values,
                                                                size_t* out_count);
RADICLE_API radicle_status radicle_hkx_skeleton_float_slot_count(const radicle_hkx* hkx, size_t* out_count);
/* Invariant: an unnamed slot answers out_named 0 with no bytes, so a slot keeps its index either way. */
RADICLE_API radicle_status radicle_hkx_skeleton_float_slot(const radicle_hkx* hkx,
                                                           size_t index,
                                                           int32_t* out_named,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);
RADICLE_API radicle_status radicle_hkx_skeleton_local_frame_count(const radicle_hkx* hkx, size_t* out_count);
/* Invariant: out_has_frame is 0 when the localFrame pointer is null, so a frame-less bone index is still reported. */
RADICLE_API radicle_status radicle_hkx_skeleton_local_frame(const radicle_hkx* hkx,
                                                           size_t index,
                                                           int16_t* out_bone_index,
                                                           int32_t* out_has_frame);
RADICLE_API radicle_status radicle_hkx_skeleton_partition_count(const radicle_hkx* hkx, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_skeleton_partition(const radicle_hkx* hkx,
                                                         size_t index,
                                                         int32_t* out_named,
                                                         uint8_t** out_bytes,
                                                         size_t* out_size,
                                                         int16_t* out_start_bone_index,
                                                         int16_t* out_bone_count);

RADICLE_API radicle_status radicle_hkx_read_animation_header(const radicle_hkx* hkx,
                                                             radicle_hkx_animation_header* out_header);
RADICLE_API radicle_status radicle_hkx_animation_class_name(const radicle_hkx* hkx,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);
/* Invariant: the three arrays hold frame_count entries of 3, 4 (x y z w) and 3 floats, released with radicle_free_floats. */
RADICLE_API radicle_status radicle_hkx_animation_track_translations(const radicle_hkx* hkx,
                                                                    size_t track,
                                                                    float** out_values,
                                                                    size_t* out_count);
RADICLE_API radicle_status radicle_hkx_animation_track_rotations(const radicle_hkx* hkx,
                                                                 size_t track,
                                                                 float** out_values,
                                                                 size_t* out_count);
RADICLE_API radicle_status radicle_hkx_animation_track_scales(const radicle_hkx* hkx,
                                                              size_t track,
                                                              float** out_values,
                                                              size_t* out_count);
RADICLE_API radicle_status radicle_hkx_animation_track_flags(const radicle_hkx* hkx,
                                                             size_t track,
                                                             radicle_hkx_track_flags* out_flags);
RADICLE_API radicle_status radicle_hkx_animation_annotation_track_name(const radicle_hkx* hkx,
                                                                       size_t track,
                                                                       uint8_t** out_bytes,
                                                                       size_t* out_size);
RADICLE_API radicle_status radicle_hkx_animation_annotation_count(const radicle_hkx* hkx,
                                                                  size_t track,
                                                                  size_t* out_count);
RADICLE_API radicle_status radicle_hkx_animation_annotation_time(const radicle_hkx* hkx,
                                                                 size_t track,
                                                                 size_t index,
                                                                 float* out_time);
RADICLE_API radicle_status radicle_hkx_animation_annotation_text(const radicle_hkx* hkx,
                                                                 size_t track,
                                                                 size_t index,
                                                                 uint8_t** out_bytes,
                                                                 size_t* out_size);
RADICLE_API radicle_status radicle_hkx_animation_float_tracks(const radicle_hkx* hkx,
                                                              float** out_values,
                                                              size_t* out_count);

RADICLE_API radicle_status radicle_hkx_read_motion_header(const radicle_hkx* hkx,
                                                          radicle_hkx_motion_header* out_header);
/* Invariant: the array holds sample_count entries of 4 floats (x y z w; w is yaw in radians about up), released with radicle_free_floats. */
RADICLE_API radicle_status radicle_hkx_motion_samples(const radicle_hkx* hkx, float** out_values, size_t* out_count);

RADICLE_API radicle_status radicle_hkx_read_binding_header(const radicle_hkx* hkx,
                                                           radicle_hkx_binding_header* out_header);
RADICLE_API radicle_status radicle_hkx_binding_original_skeleton_name(const radicle_hkx* hkx,
                                                                      uint8_t** out_bytes,
                                                                      size_t* out_size);
RADICLE_API radicle_status radicle_hkx_binding_transform_track_to_bone(const radicle_hkx* hkx,
                                                                       int16_t** out_values,
                                                                       size_t* out_count);
RADICLE_API radicle_status radicle_hkx_binding_float_track_to_float_slot(const radicle_hkx* hkx,
                                                                         int16_t** out_values,
                                                                         size_t* out_count);
RADICLE_API radicle_status radicle_hkx_binding_partitions(const radicle_hkx* hkx,
                                                          int16_t** out_values,
                                                          size_t* out_count);

RADICLE_API void radicle_hkx_free_indices(int16_t* values);

typedef struct radicle_hkx_scalar_list radicle_hkx_scalar_list;

/* Invariant: kind is 0 bool, 1 int8, 2 uint8, 3 int16, 4 uint16, 5 int32, 6 uint32 or 7 real; offset is relative to the object's start and size is the member's width in bytes. */
typedef struct radicle_hkx_scalar_info {
  int32_t kind;
  int32_t is_enum;
  int32_t is_flags;
  uint32_t offset;
  uint32_t size;
} radicle_hkx_scalar_info;

/* Requires a packfile and an index into its object list (a behaviour node's object index). Lists the object's fixed-size scalar members (bool, int8 to uint32, real, enum and flags of those) that no fixup covers; strings, pointers, structs and arrays are absent. A class without a layout is RADICLE_STATUS_INVALID_ARGUMENT and an object index past the list is RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_hkx_scalar_list_read(const uint8_t* bytes,
                                                        size_t size,
                                                        size_t object_index,
                                                        radicle_hkx_scalar_list** out_list);
RADICLE_API void radicle_hkx_scalar_list_destroy(radicle_hkx_scalar_list* list);
RADICLE_API radicle_status radicle_hkx_scalar_list_count(const radicle_hkx_scalar_list* list, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_scalar_list_read_info(const radicle_hkx_scalar_list* list,
                                                             size_t index,
                                                             radicle_hkx_scalar_info* out_info);
/* Invariant: which is 0 member name, 1 layout type text, 2 value or 3 enum value name (empty when the table names none); the value is plain text (true, false, an integer or a real) that radicle_hkx_set_scalar accepts back. Bytes are released with radicle_free_bytes. */
RADICLE_API radicle_status radicle_hkx_scalar_list_text(const radicle_hkx_scalar_list* list,
                                                        size_t index,
                                                        int32_t which,
                                                        uint8_t** out_bytes,
                                                        size_t* out_size);

/* Requires a scalar member of the list above. A bool takes true, false, 1 or 0; an integer or flags member a decimal integer; an enum a member name or a decimal integer the enum names; a real finite decimal text. The packfile is patched in place: the result has the source's size and differs only inside the member's bytes (checked before it is returned), so writing the current value returns identical bytes. Unknown member is RADICLE_STATUS_NOT_FOUND; a non-scalar member, a bad or out-of-range value is RADICLE_STATUS_INVALID_ARGUMENT with the reason in radicle_last_error. The result is not validated as a behaviour graph and has not been loaded by the game. Released with radicle_free_bytes. */
RADICLE_API radicle_status radicle_hkx_set_scalar(const uint8_t* bytes,
                                                  size_t size,
                                                  size_t object_index,
                                                  const char* member,
                                                  const char* value,
                                                  uint8_t** out_bytes,
                                                  size_t* out_size);

#ifdef __cplusplus
}
#endif
