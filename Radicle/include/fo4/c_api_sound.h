#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_sound_playback {
  int8_t frequency_shift;
  int8_t frequency_variance;
  uint8_t priority;
  uint8_t db_variance;
  uint16_t attenuation_hundredths;
} radicle_sound_playback;

typedef struct radicle_sound_file {
  const char* path;
} radicle_sound_file;

typedef struct radicle_sound_rate_file {
  uint32_t rpm;
  const char* path;
} radicle_sound_rate_file;

typedef struct radicle_sound_descriptor {
  int32_t has_kind;
  uint32_t kind;
  radicle_form_key category;
  radicle_form_key alternate;
  radicle_form_key output_model;
  radicle_form_key base_descriptor;
  const radicle_form_key* descriptors;
  size_t descriptor_count;
  const radicle_sound_file* files;
  size_t file_count;
  int32_t has_controls;
  uint8_t controls[4];
  int32_t has_playback;
  radicle_sound_playback playback;
  const radicle_sound_rate_file* rate_files;
  size_t rate_file_count;
  void* internal;
} radicle_sound_descriptor;

typedef struct radicle_sound_marker_info {
  radicle_form_key descriptor;
  int32_t has_repeat;
  float min_time;
  float max_time;
  int32_t has_stackable;
  uint8_t stackable;
} radicle_sound_marker_info;

typedef struct radicle_music_type {
  int32_t has_flags;
  uint32_t flags;
  int32_t has_priority;
  uint16_t priority;
  uint16_t ducking_hundredths;
  int32_t has_fade_duration;
  float fade_duration;
  const radicle_form_key* tracks;
  size_t track_count;
  void* internal;
} radicle_music_type;

typedef struct radicle_footstep_info {
  radicle_form_key impact_set;
  const char* tag;
} radicle_footstep_info;

typedef struct radicle_footstep_set {
  uint32_t counts[5];
  const radicle_form_key* steps;
  size_t step_count;
  void* internal;
} radicle_footstep_set;

typedef struct radicle_sound_file_list {
  const radicle_sound_file* files;
  size_t file_count;
  void* internal;
} radicle_sound_file_list;

RADICLE_API radicle_status radicle_sound_descriptor_decode(const radicle_env* env, const radicle_record* record,
                                                           const radicle_sound_descriptor** out_descriptor);
RADICLE_API void radicle_sound_descriptor_free(const radicle_sound_descriptor* descriptor);
RADICLE_API radicle_status radicle_sound_marker_get_info(const radicle_env* env, const radicle_record* record,
                                                        radicle_sound_marker_info* out_info);
RADICLE_API radicle_status radicle_music_type_decode(const radicle_env* env, const radicle_record* record,
                                                     const radicle_music_type** out_music);
RADICLE_API void radicle_music_type_free(const radicle_music_type* music);
RADICLE_API radicle_status radicle_footstep_get_info(const radicle_env* env, const radicle_record* record,
                                                   radicle_footstep_info* out_info);
RADICLE_API radicle_status radicle_footstep_set_decode(const radicle_env* env, const radicle_record* record,
                                                      const radicle_footstep_set** out_set);
RADICLE_API void radicle_footstep_set_free(const radicle_footstep_set* set);
RADICLE_API radicle_status radicle_sound_resolve_files(const radicle_env* env, const radicle_record* record,
                                                      const radicle_sound_file_list** out_list);
RADICLE_API void radicle_sound_file_list_free(const radicle_sound_file_list* list);
RADICLE_API radicle_status radicle_sound_select_rate_file(const radicle_sound_rate_file* rate_files,
                                                          size_t rate_file_count, uint32_t rpm,
                                                          int32_t* out_has_index, size_t* out_index);
RADICLE_API radicle_status radicle_sound_select_standard_file(size_t count, size_t draw_index, int32_t has_previous,
                                                              size_t previous_index, size_t* out_index);
RADICLE_API radicle_status radicle_sound_frequency_factor(int8_t frequency_shift, double* out_factor);
RADICLE_API radicle_status radicle_sound_is_looping(uint8_t loop_control, int32_t* out_looping);
RADICLE_API radicle_status radicle_sound_sidechain_send(uint8_t sidechain_control, double* out_send);
RADICLE_API radicle_status radicle_sound_small_rumble_send(uint8_t rumble_control, double* out_send);
RADICLE_API radicle_status radicle_sound_big_rumble_send(uint8_t rumble_control, double* out_send);

RADICLE_API radicle_status radicle_plugin_add_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                                         const char* file, size_t* out_file_index);
RADICLE_API radicle_status radicle_plugin_remove_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                                            size_t file_index);
RADICLE_API radicle_status radicle_plugin_set_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                                         size_t file_index, const char* file);
RADICLE_API radicle_status radicle_plugin_add_sound_base_link(radicle_plugin* plugin, radicle_form_key key,
                                                              radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_remove_sound_base_link(radicle_plugin* plugin, radicle_form_key key);
RADICLE_API radicle_status radicle_plugin_create_sound_record(radicle_plugin* plugin, const char* signature,
                                                              const char* editor_id_utf8,
                                                              radicle_form_key* out_key);

#ifdef __cplusplus
}
#endif
