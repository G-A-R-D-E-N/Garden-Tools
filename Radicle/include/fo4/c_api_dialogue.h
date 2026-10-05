#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_dialogue_text {
  radicle_name_kind kind;
  const char* text;
  uint32_t string_id;
} radicle_dialogue_text;

typedef struct radicle_dialogue_topic_info {
  int32_t has_data;
  uint8_t flags;
  uint8_t category;
  uint16_t subtype;
  int32_t has_priority;
  float priority;
  int32_t has_info_count;
  uint32_t info_count;
  int32_t has_subtype_name;
  uint8_t subtype_name[4];
  radicle_form_key branch;
  radicle_form_key quest;
  radicle_form_key keyword;
} radicle_dialogue_topic_info;

typedef struct radicle_dialogue_condition {
  uint8_t flags_byte;
  uint32_t comparison_raw;
  uint16_t function;
  uint16_t unknown2;
  int32_t param1;
  int32_t param2;
  uint32_t run_on;
  uint32_t reference;
  int32_t unknown3;
  const char* parameter1_string;
  const char* parameter2_string;
} radicle_dialogue_condition;

typedef struct radicle_dialogue_response {
  radicle_form_key emotion;
  radicle_form_key sound;
  uint8_t number;
  uint8_t unknown;
  uint16_t interrupt_percent;
  int32_t camera_target_alias;
  int32_t camera_location_alias;
  radicle_dialogue_text text;
  radicle_form_key speaker_idle;
  radicle_form_key listener_idle;
} radicle_dialogue_response;

typedef struct radicle_dialogue_info {
  int32_t has_data;
  uint16_t flags;
  uint16_t reset_time_encoded;
  radicle_form_key previous_topic;
  radicle_form_key previous_info;
  radicle_form_key shared_info;
  radicle_form_key info_group;
  radicle_form_key speaker;
  radicle_form_key start_scene;
  radicle_dialogue_text prompt;
  const radicle_dialogue_response* responses;
  size_t response_count;
  const radicle_dialogue_condition* conditions;
  size_t condition_count;
  void* internal;
} radicle_dialogue_info;

typedef struct radicle_scene_phase {
  const char* name;
  int32_t has_editor_width;
  uint32_t editor_width;
  int32_t has_flags;
  uint16_t flags;
  int32_t has_quest_stages;
  int16_t start_quest_stage;
  int16_t completion_quest_stage;
  const radicle_dialogue_condition* start_conditions;
  size_t start_condition_count;
  const radicle_dialogue_condition* completion_conditions;
  size_t completion_condition_count;
} radicle_scene_phase;

typedef struct radicle_scene_actor {
  int32_t alias;
  uint32_t flags;
  uint32_t behavior_flags;
} radicle_scene_actor;

typedef struct radicle_scene_payload {
  char signature[5];
  const uint8_t* bytes;
  size_t size;
} radicle_scene_payload;

typedef struct radicle_scene_action {
  uint16_t type;
  const char* name;
  int32_t has_actor_alias;
  int32_t actor_alias;
  int32_t has_index;
  uint32_t index;
  int32_t has_flags;
  uint32_t flags;
  int32_t has_start_phase;
  uint32_t start_phase;
  int32_t has_end_phase;
  uint32_t end_phase;
  const radicle_scene_payload* payload;
  size_t payload_count;
} radicle_scene_action;

typedef struct radicle_scene_info {
  int32_t has_flags;
  uint32_t flags;
  radicle_form_key parent_quest;
  const radicle_scene_phase* phases;
  size_t phase_count;
  const radicle_scene_actor* actors;
  size_t actor_count;
  const radicle_scene_action* actions;
  size_t action_count;
  const radicle_dialogue_condition* conditions;
  size_t condition_count;
  void* internal;
} radicle_scene_info;

RADICLE_API radicle_status radicle_dialogue_topic_get_info(const radicle_env* env, const radicle_record* record, radicle_dialogue_topic_info* out_info);
RADICLE_API radicle_status radicle_dialogue_info_decode(const radicle_env* env, const radicle_record* record, const radicle_dialogue_info** out_info);
RADICLE_API void radicle_dialogue_info_free(const radicle_dialogue_info* info);
RADICLE_API radicle_status radicle_scene_decode(const radicle_env* env, const radicle_record* record, const radicle_scene_info** out_scene);
RADICLE_API void radicle_scene_free(const radicle_scene_info* scene);

#ifdef __cplusplus
}
#endif
