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

/* out_pcm is malloc'd and the caller must release it with radicle_free_bytes. */
RADICLE_API radicle_status radicle_audio_decode_xwma(const uint8_t* bytes,
                                                     size_t size,
                                                     uint8_t** out_pcm,
                                                     size_t* out_pcm_size,
                                                     uint32_t* out_sample_rate,
                                                     uint16_t* out_channels,
                                                     size_t* out_bad_packets);

/* Invariant: out_weights is frame-major, frame_count * (phoneme_count + modifier_count) floats, released with radicle_free_floats. */
RADICLE_API radicle_status radicle_audio_decode_lip(const uint8_t* bytes,
                                                    size_t size,
                                                    uint32_t* out_frame_count,
                                                    int32_t* out_preroll_frames,
                                                    uint32_t* out_phoneme_count,
                                                    uint32_t* out_modifier_count,
                                                    float** out_weights,
                                                    size_t* out_weight_count);

/* Invariant: slots 0 to 42 are the LIP phonemes in file order, slot 40 is SIL; the name is static NUL-terminated text that is never freed. */
RADICLE_API radicle_status radicle_lip_phoneme_name(uint32_t slot, const char** out_name);

/* Constraint: a slot maps to head .tri morphs through the RACE phoneme weight table and an authored viseme recipe; slots with an all-zero PHWT row answer a count of 0. */
RADICLE_API radicle_status radicle_lip_slot_morph_count(uint32_t slot, size_t* out_count);

/* Invariant: the morph name is static NUL-terminated text, the weight is in [0, 1], and index is below radicle_lip_slot_morph_count. */
RADICLE_API radicle_status radicle_lip_slot_morph(uint32_t slot,
                                                  size_t index,
                                                  const char** out_morph,
                                                  float* out_weight);

/* Requires radicle_free_bytes on out_path; NULL or empty language, variant and extension take the defaults. */
RADICLE_API radicle_status radicle_audio_format_voice_path(const char* plugin,
                                                           const char* voice_type,
                                                           uint32_t info_form_id,
                                                           uint32_t response_index,
                                                           const char* language,
                                                           const char* variant,
                                                           const char* extension,
                                                           uint8_t** out_path,
                                                           size_t* out_size);

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
