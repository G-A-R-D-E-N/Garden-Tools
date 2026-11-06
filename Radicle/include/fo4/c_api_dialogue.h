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
  int32_t has_reset_hours;
  double reset_hours;
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

typedef struct radicle_dialogue_branch_spec {
  const char* editor_id;
  uint32_t category;
  uint32_t flags;
} radicle_dialogue_branch_spec;

typedef struct radicle_dialogue_topic_spec {
  const char* editor_id;
  const char* name;
  float priority;
  int32_t has_branch;
  radicle_form_key branch;
  int32_t has_keyword;
  radicle_form_key keyword;
  uint8_t topic_flags;
  uint8_t category;
  uint16_t subtype;
  const char* subtype_name;
} radicle_dialogue_topic_spec;

typedef struct radicle_dialogue_response_spec {
  const char* text;
  const char* script_notes;
  int32_t has_emotion;
  radicle_form_key emotion;
  int32_t has_sound;
  radicle_form_key sound;
  uint8_t number;
  uint16_t interrupt_percent;
  int32_t camera_target_alias;
  int32_t camera_location_alias;
} radicle_dialogue_response_spec;

typedef struct radicle_dialogue_info_spec {
  uint16_t flags;
  uint16_t reset_time_encoded;
  const radicle_dialogue_response_spec* responses;
  size_t response_count;
  const radicle_dialogue_condition* conditions;
  size_t condition_count;
} radicle_dialogue_info_spec;

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

typedef enum radicle_scene_part {
  RADICLE_SCENE_PART_PHASE = 0,
  RADICLE_SCENE_PART_ACTOR = 1,
  RADICLE_SCENE_PART_ACTION = 2
} radicle_scene_part;

RADICLE_API radicle_status radicle_dialogue_topic_get_info(const radicle_env* env, const radicle_record* record, radicle_dialogue_topic_info* out_info);
RADICLE_API radicle_status radicle_dialogue_info_decode(const radicle_env* env, const radicle_record* record, const radicle_dialogue_info** out_info);
RADICLE_API void radicle_dialogue_info_free(const radicle_dialogue_info* info);
RADICLE_API radicle_status radicle_scene_decode(const radicle_env* env, const radicle_record* record, const radicle_scene_info** out_scene);
RADICLE_API void radicle_scene_free(const radicle_scene_info* scene);
RADICLE_API const char* radicle_scene_action_type_name(uint16_t type);
RADICLE_API radicle_status radicle_plugin_set_scene_scalar(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const char* path,
                                                           size_t group_index,
                                                           const char* value);
RADICLE_API radicle_status radicle_plugin_set_scene_formlink(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             const char* path,
                                                             const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_set_scene_action_formlink(radicle_plugin* plugin,
                                                                    radicle_form_key key,
                                                                    size_t index,
                                                                    const char* path,
                                                                    const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_scene_group_count(const radicle_plugin* plugin,
                                                            radicle_form_key key,
                                                            radicle_scene_part part,
                                                            size_t* out_count);
RADICLE_API radicle_status radicle_plugin_insert_scene_phase(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             size_t index,
                                                             const char* name);
RADICLE_API radicle_status radicle_plugin_remove_scene_phase(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             size_t index);
RADICLE_API radicle_status radicle_plugin_set_scene_phase_name(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               size_t index,
                                                               const char* name);
RADICLE_API radicle_status radicle_plugin_insert_scene_actor(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             size_t index);
RADICLE_API radicle_status radicle_plugin_remove_scene_actor(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             size_t index);
RADICLE_API radicle_status radicle_plugin_insert_scene_action(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              size_t index,
                                                              uint16_t type);
RADICLE_API radicle_status radicle_plugin_remove_scene_action(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              size_t index);
RADICLE_API radicle_status radicle_plugin_set_scene_action_name(radicle_plugin* plugin,
                                                                radicle_form_key key,
                                                                size_t index,
                                                                const char* name);
RADICLE_API radicle_status radicle_plugin_create_dialogue_branch(radicle_plugin* plugin,
                                                                 radicle_form_key quest,
                                                                 const radicle_dialogue_branch_spec* spec,
                                                                 radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_create_dialogue_topic(radicle_plugin* plugin,
                                                                radicle_form_key quest,
                                                                const radicle_dialogue_topic_spec* spec,
                                                                radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_set_dialogue_branch_start(radicle_plugin* plugin,
                                                                    radicle_form_key branch,
                                                                    radicle_form_key topic);
RADICLE_API radicle_status radicle_plugin_create_dialogue_info(radicle_plugin* plugin,
                                                               radicle_form_key topic,
                                                               const radicle_dialogue_info_spec* spec,
                                                               radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_update_dialogue_info(radicle_plugin* plugin,
                                                               radicle_form_key info,
                                                               const radicle_dialogue_info_spec* spec);
RADICLE_API radicle_status radicle_plugin_remove_dialogue_info(radicle_plugin* plugin,
                                                               radicle_form_key info);

#ifdef __cplusplus
}
#endif
