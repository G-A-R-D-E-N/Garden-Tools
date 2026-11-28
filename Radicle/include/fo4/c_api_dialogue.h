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

typedef struct radicle_scene_spec {
  const char* editor_id;
  uint32_t flags;
  uint32_t index;
} radicle_scene_spec;

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

/* Constraint: one repeated LCEP/INTT/SSPN/CITC/CTDA entry inside a type-4 Start Scene action. */
typedef struct radicle_scene_start_entry {
  radicle_form_key scene;
  uint16_t phase_index;
  int32_t has_phase_name;
  const char* phase_name;
  const radicle_dialogue_condition* conditions;
  size_t condition_count;
} radicle_scene_start_entry;

typedef struct radicle_scene_start_entries {
  const radicle_scene_start_entry* entries;
  size_t count;
  void* internal;
} radicle_scene_start_entries;

/* has_phase_name 0 writes no SSPN; phase_name text is UTF-8 and must be representable in Windows-1252. */
typedef struct radicle_scene_start_entry_spec {
  radicle_form_key scene;
  uint16_t phase_index;
  int32_t has_phase_name;
  const char* phase_name;
  const radicle_dialogue_condition* conditions;
  size_t condition_count;
} radicle_scene_start_entry_spec;

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
// Constraint: release the returned package list and its borrowed names with radicle_scene_package_list_free.
typedef struct radicle_scene_package_list {
  const radicle_form_key* packages;
  size_t count;
  void* internal;
} radicle_scene_package_list;
RADICLE_API radicle_status radicle_plugin_read_scene_action_packages(const radicle_plugin* plugin, radicle_form_key key,
    size_t action, const radicle_scene_package_list** out_list);
RADICLE_API void radicle_scene_package_list_free(const radicle_scene_package_list* list);
RADICLE_API radicle_status radicle_plugin_set_scene_action_packages(radicle_plugin* plugin, radicle_form_key key,
    size_t action, const radicle_form_key* packages, size_t count);
// Constraint: the UTF-8 name is length-delimited and must be released with radicle_free_bytes.
RADICLE_API radicle_status radicle_plugin_read_scene_action_start_phase(const radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index, uint8_t* has_index, uint16_t* phase_index, uint8_t* has_name,
    uint8_t** name, size_t* name_size);
RADICLE_API radicle_status radicle_plugin_set_scene_action_start_phase_index(radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index, uint8_t has_value, uint32_t value);
RADICLE_API radicle_status radicle_plugin_set_scene_action_start_phase_name(radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index, const char* value);
RADICLE_API radicle_status radicle_plugin_insert_scene_action_start_scene(radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index, const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_set_scene_action_start_scene(radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index, const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_remove_scene_action_start_scene(radicle_plugin* plugin, radicle_form_key key,
    size_t action, size_t index);
RADICLE_API radicle_status radicle_plugin_set_scene_action_headtracking(radicle_plugin* plugin,
                                                                          radicle_form_key key,
                                                                          size_t index,
                                                                          const int32_t* aliases,
                                                                          size_t count);
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
RADICLE_API radicle_status radicle_plugin_scene_start_scenes(const radicle_plugin* plugin,
                                                             radicle_form_key scene,
                                                             size_t action_index,
                                                             const radicle_scene_start_entries** out_entries);
RADICLE_API void radicle_scene_start_entries_free(const radicle_scene_start_entries* entries);
RADICLE_API radicle_status radicle_plugin_insert_scene_start_scene(radicle_plugin* plugin,
                                                                  radicle_form_key scene,
                                                                  size_t action_index,
                                                                  size_t index,
                                                                  const radicle_scene_start_entry_spec* spec);
RADICLE_API radicle_status radicle_plugin_update_scene_start_scene(radicle_plugin* plugin,
                                                                  radicle_form_key scene,
                                                                  size_t action_index,
                                                                  size_t index,
                                                                  const radicle_scene_start_entry_spec* spec);
RADICLE_API radicle_status radicle_plugin_remove_scene_start_scene(radicle_plugin* plugin,
                                                                  radicle_form_key scene,
                                                                  size_t action_index,
                                                                  size_t index);
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
RADICLE_API radicle_status radicle_plugin_read_scene_action_camera(const radicle_plugin* plugin, radicle_form_key key,
    size_t action, uint8_t* has_value, float* fov, float* rate);
RADICLE_API radicle_status radicle_plugin_set_scene_action_camera(radicle_plugin* plugin, radicle_form_key key,
    size_t action, uint8_t has_value, float fov, float rate);
RADICLE_API radicle_status radicle_plugin_create_scene(radicle_plugin* plugin,
                                                       radicle_form_key quest,
                                                       const radicle_scene_spec* spec,
                                                       radicle_form_key* out_key);
/* Invariant: slot meaning follows the stored action type, never the subrecord signature alone. */
typedef enum radicle_scene_link_slot {
  RADICLE_SCENE_LINK_TOPIC = 0,
  RADICLE_SCENE_LINK_SOUND = 1,
  RADICLE_SCENE_LINK_DIALOGUE_SUBTYPE = 2,
  RADICLE_SCENE_LINK_ANIM_ARCH_TYPE = 3,
  RADICLE_SCENE_LINK_AUDIO_OUTPUT_OVERRIDE = 4,
  RADICLE_SCENE_LINK_PLAYER_POSITIVE_RESPONSE = 5,
  RADICLE_SCENE_LINK_PLAYER_NEGATIVE_RESPONSE = 6,
  RADICLE_SCENE_LINK_PLAYER_NEUTRAL_RESPONSE = 7,
  RADICLE_SCENE_LINK_PLAYER_QUESTION_RESPONSE = 8,
  RADICLE_SCENE_LINK_PLAYER_POSITIVE_SUBTYPE = 9,
  RADICLE_SCENE_LINK_PLAYER_NEGATIVE_SUBTYPE = 10,
  RADICLE_SCENE_LINK_PLAYER_NEUTRAL_SUBTYPE = 11,
  RADICLE_SCENE_LINK_PLAYER_QUESTION_SUBTYPE = 12,
  RADICLE_SCENE_LINK_NPC_POSITIVE_RESPONSE = 13,
  RADICLE_SCENE_LINK_NPC_NEGATIVE_RESPONSE = 14,
  RADICLE_SCENE_LINK_NPC_NEUTRAL_RESPONSE = 15,
  RADICLE_SCENE_LINK_NPC_QUESTION_RESPONSE = 16,
  RADICLE_SCENE_LINK_NPC_POSITIVE_SUBTYPE = 17,
  RADICLE_SCENE_LINK_NPC_NEGATIVE_SUBTYPE = 18,
  RADICLE_SCENE_LINK_NPC_NEUTRAL_SUBTYPE = 19,
  RADICLE_SCENE_LINK_NPC_QUESTION_SUBTYPE = 20
} radicle_scene_link_slot;

/* Invariant: a link with a null plugin is an absent field or a null stored link. */
typedef struct radicle_scene_target_link {
  radicle_scene_link_slot slot;
  radicle_form_key key;
} radicle_scene_target_link;

/* Invariant: one row per scene action, in action order. max_seconds is Looping Max for types 0 and 6 and Timer Max for type 2; min_seconds is Looping Min for type 0 and Timer Min for type 2. */
/* Invariant: malformed is 1 when a known action type holds a wrong size, a repeated single field or an unlisted signature; every field that did decode is still reported. An unknown type reports no fields and malformed 0. */
typedef struct radicle_scene_action_target {
  uint16_t type;
  int32_t malformed;
  int32_t end_scene_greeting;
  int32_t has_max_seconds;
  float max_seconds;
  int32_t has_min_seconds;
  float min_seconds;
  int32_t has_camera;
  float camera_fov;
  float camera_rate;
  int32_t has_emotion_type;
  uint32_t emotion_type;
  int32_t has_emotion_value;
  uint32_t emotion_value;
  int32_t has_quest_stage;
  int16_t quest_stage;
  int32_t has_dialogue_target_actor;
  int32_t dialogue_target_actor;
  const radicle_scene_target_link* links;
  size_t link_count;
  const int32_t* head_tracking;
  size_t head_tracking_count;
  const radicle_form_key* packages;
  size_t package_count;
  const radicle_scene_start_entry* start_entries;
  size_t start_entry_count;
} radicle_scene_action_target;

/* Invariant: quest stages are the scene-level SCQS pair; -1 means no stage. */
typedef struct radicle_scene_targets {
  int32_t has_quest_stages;
  int16_t quest_begin_stage;
  int16_t quest_end_stage;
  const radicle_scene_action_target* actions;
  size_t action_count;
  void* internal;
} radicle_scene_targets;

typedef enum radicle_info_fragment_state {
  RADICLE_INFO_FRAGMENT_NO_VMAD = 0,
  RADICLE_INFO_FRAGMENT_NO_SECTION = 1,
  RADICLE_INFO_FRAGMENT_UNREADABLE = 2,
  RADICLE_INFO_FRAGMENT_PRESENT = 3
} radicle_info_fragment_state;

/* Invariant: kind is 1 for OnBegin and 2 for OnEnd. */
typedef struct radicle_info_fragment_row {
  uint8_t kind;
  uint8_t unknown;
  const char* script;
  const char* function;
} radicle_info_fragment_row;

/* Invariant: begin_stage and end_stage are raw u16 with 0xFFFF unset. fragment_failure is null unless the state is UNREADABLE; a fragment problem never fails the decode. */
typedef struct radicle_info_result {
  int32_t has_stages;
  uint16_t begin_stage;
  uint16_t end_stage;
  int32_t has_start_scene_phase;
  const char* start_scene_phase;
  radicle_info_fragment_state fragment_state;
  const char* fragment_failure;
  int32_t fragment_version;
  uint8_t fragment_flags;
  const radicle_info_fragment_row* rows;
  size_t row_count;
  void* internal;
} radicle_info_result;

typedef enum radicle_term_fragment_state {
  RADICLE_TERM_FRAGMENT_NO_VMAD = 0,
  RADICLE_TERM_FRAGMENT_NO_SECTION = 1,
  RADICLE_TERM_FRAGMENT_UNREADABLE = 2,
  RADICLE_TERM_FRAGMENT_PRESENT = 3
} radicle_term_fragment_state;

/* Invariant: index is the stored 32-bit Fragment Index; whether it equals a menu item id is UNVERIFIED, so the caller joins. */
typedef struct radicle_term_fragment_row {
  uint32_t index;
  uint8_t unknown;
  const char* script;
  const char* function;
} radicle_term_fragment_row;

/* Invariant: rows are in stored order and exist only when state is PRESENT; failure is null unless the state is UNREADABLE; a fragment problem never fails the decode. */
typedef struct radicle_term_fragments {
  radicle_term_fragment_state state;
  const char* failure;
  int32_t version;
  const char* script_name;
  const radicle_term_fragment_row* rows;
  size_t row_count;
  void* internal;
} radicle_term_fragments;

RADICLE_API radicle_status radicle_scene_targets_decode(const radicle_env* env, const radicle_record* record, const radicle_scene_targets** out_targets);
RADICLE_API void radicle_scene_targets_free(const radicle_scene_targets* targets);
RADICLE_API radicle_status radicle_info_result_decode(const radicle_env* env, const radicle_record* record, const radicle_info_result** out_result);
RADICLE_API void radicle_info_result_free(const radicle_info_result* result);
RADICLE_API radicle_status radicle_term_fragments_decode(const radicle_env* env, const radicle_record* record, const radicle_term_fragments** out_fragments);
RADICLE_API void radicle_term_fragments_free(const radicle_term_fragments* fragments);

#ifdef __cplusplus
}
#endif
