#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_hkx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_hkx_plain_clip radicle_hkx_plain_clip;
typedef struct radicle_hkx_clip_roles radicle_hkx_clip_roles;

/* Invariant: root_motion_kind is 0 none, 1 default or 2 unsupported; the root_* values are zero unless it is 1. */
typedef struct radicle_hkx_plain_clip_info {
  float length;
  float rate;
  int32_t loop;
  size_t key_count;
  size_t track_count;
  size_t event_count;
  int32_t root_motion_kind;
  float root_net_translation[3];
  float root_net_yaw;
  float root_mean_planar_speed;
  float root_mean_turn_rate;
} radicle_hkx_plain_clip_info;

typedef struct radicle_hkx_clip_role_info {
  int32_t loop;
  float playback_speed;
} radicle_hkx_clip_role_info;

/* Invariant: every value is in engine axes and metres, so bone_count entries of 10 floats (translation, rotation x y z w, scale), released with radicle_free_floats. */
RADICLE_API radicle_status radicle_hkx_plain_skeleton_rest(const radicle_hkx* skeleton,
                                                           float** out_values,
                                                           size_t* out_count);

/* Requires a skeleton file and a clip file; rate is keys per second (0 is the clip's own rate, at most 240) and loop is stored as given. */
RADICLE_API radicle_status radicle_hkx_plain_clip_sample(const radicle_hkx* skeleton,
                                                         const radicle_hkx* clip,
                                                         float rate,
                                                         int32_t loop,
                                                         radicle_hkx_plain_clip** out_clip);
RADICLE_API void radicle_hkx_plain_clip_destroy(radicle_hkx_plain_clip* clip);
RADICLE_API radicle_status radicle_hkx_plain_clip_read_info(const radicle_hkx_plain_clip* clip,
                                                            radicle_hkx_plain_clip_info* out_info);
RADICLE_API radicle_status radicle_hkx_plain_clip_track_bone(const radicle_hkx_plain_clip* clip,
                                                             size_t track,
                                                             int32_t* out_bone);
/* Invariant: the three arrays hold key_count entries of 3, 4 (x y z w) and 3 floats, released with radicle_free_floats. */
RADICLE_API radicle_status radicle_hkx_plain_clip_track_translations(const radicle_hkx_plain_clip* clip,
                                                                     size_t track,
                                                                     float** out_values,
                                                                     size_t* out_count);
RADICLE_API radicle_status radicle_hkx_plain_clip_track_rotations(const radicle_hkx_plain_clip* clip,
                                                                  size_t track,
                                                                  float** out_values,
                                                                  size_t* out_count);
RADICLE_API radicle_status radicle_hkx_plain_clip_track_scales(const radicle_hkx_plain_clip* clip,
                                                               size_t track,
                                                               float** out_values,
                                                               size_t* out_count);
/* Invariant: events are ordered by time and the name is the annotation text verbatim. */
RADICLE_API radicle_status radicle_hkx_plain_clip_event_time(const radicle_hkx_plain_clip* clip,
                                                             size_t index,
                                                             float* out_time);
RADICLE_API radicle_status radicle_hkx_plain_clip_event_name(const radicle_hkx_plain_clip* clip,
                                                             size_t index,
                                                             uint8_t** out_bytes,
                                                             size_t* out_size);
/* Invariant: key_count entries of 4 floats (x y z, yaw about +Y), each since the previous key in its heading frame, entry 0 zero; NOT_FOUND unless root_motion_kind is 1. */
RADICLE_API radicle_status radicle_hkx_plain_clip_root_motion(const radicle_hkx_plain_clip* clip,
                                                              float** out_values,
                                                              size_t* out_count);

/* Requires a behaviour packfile; animation_root and directory may be NULL, and each role comes back at most once. */
RADICLE_API radicle_status radicle_hkx_clip_roles_read(const radicle_hkx* behavior,
                                                       const char* animation_root,
                                                       const char* directory,
                                                       radicle_hkx_clip_roles** out_roles);
RADICLE_API void radicle_hkx_clip_roles_destroy(radicle_hkx_clip_roles* roles);
RADICLE_API radicle_status radicle_hkx_clip_roles_count(const radicle_hkx_clip_roles* roles, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_clip_role_name(const radicle_hkx_clip_roles* roles,
                                                      size_t index,
                                                      uint8_t** out_bytes,
                                                      size_t* out_size);
/* Invariant: the entry is a lowercase `/` archive path ending .hkx with no drive and no `..`. */
RADICLE_API radicle_status radicle_hkx_clip_role_entry(const radicle_hkx_clip_roles* roles,
                                                       size_t index,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);
RADICLE_API radicle_status radicle_hkx_clip_role_read_info(const radicle_hkx_clip_roles* roles,
                                                           size_t index,
                                                           radicle_hkx_clip_role_info* out_info);

#ifdef __cplusplus
}
#endif
