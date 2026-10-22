#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_behavior_condition {
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
} radicle_behavior_condition;

typedef struct radicle_package_payload {
  char signature[5];
  const uint8_t* bytes;
  size_t size;
} radicle_package_payload;

typedef struct radicle_package_location {
  size_t subrecord_index;
  int32_t type;
  uint32_t value;
  int32_t has_radius;
  int32_t radius;
  int32_t has_item_index;
  uint32_t item_index;
  radicle_form_key form;
} radicle_package_location;

typedef struct radicle_package_target {
  size_t subrecord_index;
  int32_t type;
  uint32_t value;
  int32_t count_or_distance;
  radicle_form_key form;
} radicle_package_target;

typedef struct radicle_package_info {
  int32_t has_data;
  uint32_t flags;
  uint8_t type;
  uint8_t interrupt_override;
  uint8_t preferred_speed;
  uint8_t unknown;
  uint16_t interrupt_flags;
  uint16_t trailing_value;
  int32_t has_schedule;
  int8_t month;
  int8_t weekday;
  int8_t date;
  int8_t hour;
  int8_t minute;
  uint32_t duration_minutes;
  int32_t has_counter;
  uint32_t input_count;
  uint32_t version;
  radicle_form_key template_package;
  radicle_form_key combat_style;
  radicle_form_key owner_quest;
  radicle_form_key begin_idle;
  radicle_form_key end_idle;
  radicle_form_key change_idle;
  const radicle_behavior_condition* conditions;
  size_t condition_count;
  const radicle_package_payload* opaque;
  size_t opaque_count;
  const radicle_package_location* locations;
  size_t location_count;
  const radicle_package_target* targets;
  size_t target_count;
  const radicle_form_key* idle_animations;
  size_t idle_animation_count;
  void* internal;
} radicle_package_info;

typedef struct radicle_combat_style_info {
  int32_t has_general;
  float general[12];
  size_t melee_count;
  float melee[10];
  size_t long_range_count;
  float long_range[5];
  int32_t has_close_range;
  float close_range[9];
  uint32_t throw_max_targets;
  float flank_variance;
  int32_t has_flight;
  float flight[8];
  int32_t has_ranged_accuracy;
  float ranged_accuracy;
  int32_t has_cover_search;
  float cover_search;
  int32_t has_flags;
  uint32_t flags;
} radicle_combat_style_info;

typedef struct radicle_idle_info {
  int32_t has_data;
  uint8_t loop_min;
  uint8_t loop_max;
  uint8_t flags;
  uint8_t group_section;
  uint16_t replay_delay;
  radicle_form_key parent;
  radicle_form_key previous;
  const char* behavior_graph;
  const char* animation_event;
  const char* animation_file;
  const radicle_behavior_condition* conditions;
  size_t condition_count;
  void* internal;
} radicle_idle_info;

typedef enum radicle_package_input_value_kind {
  RADICLE_PACKAGE_INPUT_NONE = 0,
  RADICLE_PACKAGE_INPUT_BOOLEAN = 1,
  RADICLE_PACKAGE_INPUT_INTEGER = 2,
  RADICLE_PACKAGE_INPUT_REAL = 3,
  RADICLE_PACKAGE_INPUT_REFERENCE = 4,
  RADICLE_PACKAGE_INPUT_TARGET = 5,
  RADICLE_PACKAGE_INPUT_LOCATION = 6,
  RADICLE_PACKAGE_INPUT_OBJECT_LIST = 7,
  RADICLE_PACKAGE_INPUT_TOPIC = 8
} radicle_package_input_value_kind;

typedef struct radicle_package_procedure_input {
  const char* type_name;
  size_t subrecord_begin;
  size_t subrecord_end;
  uint8_t index;
  const char* value_tag;
  size_t value_index;
  radicle_package_input_value_kind kind;
  int32_t boolean;
  int32_t integer;
  uint32_t integer_raw;
  float real;
} radicle_package_procedure_input;

typedef struct radicle_package_input_value {
  radicle_package_input_value_kind kind;
  int32_t boolean;
  int32_t integer;
  float real;
  const uint8_t* payload;
  size_t payload_size;
} radicle_package_input_value;

typedef struct radicle_package_procedure_node {
  int32_t is_leaf;
  const char* name;
  uint32_t condition_count;
  uint32_t child_count;
  int32_t has_flags;
  uint32_t flags;
  size_t opaque_begin;
  size_t opaque_end;
  const size_t* children;
  size_t children_count;
} radicle_package_procedure_node;

typedef struct radicle_package_procedure_tree {
  int32_t decoded;
  size_t region_begin;
  size_t region_end;
  int32_t has_next_input_id;
  uint8_t next_input_id;
  const radicle_package_procedure_input* inputs;
  size_t input_count;
  const radicle_package_procedure_node* nodes;
  size_t node_count;
  void* internal;
} radicle_package_procedure_tree;

RADICLE_API radicle_status radicle_package_decode(const radicle_env* env, const radicle_record* record, const radicle_package_info** out_package);
RADICLE_API void radicle_package_free(const radicle_package_info* package);
RADICLE_API radicle_status radicle_combat_style_get_info(const radicle_env* env, const radicle_record* record, radicle_combat_style_info* out_info);
RADICLE_API radicle_status radicle_idle_decode(const radicle_env* env, const radicle_record* record, const radicle_idle_info** out_idle);
RADICLE_API void radicle_idle_free(const radicle_idle_info* idle);
RADICLE_API radicle_status radicle_package_procedure_decode(const radicle_env* env, const radicle_record* record,
                                                            const radicle_package_procedure_tree** out_tree);
RADICLE_API void radicle_package_procedure_free(const radicle_package_procedure_tree* tree);
RADICLE_API radicle_status radicle_package_input_add(radicle_plugin* plugin, radicle_form_key key,
                                                     const char* type_name,
                                                     const radicle_package_input_value* value);
RADICLE_API radicle_status radicle_package_input_set(radicle_plugin* plugin, radicle_form_key key, uint8_t id,
                                                     const radicle_package_input_value* value);
RADICLE_API radicle_status radicle_package_input_remove(radicle_plugin* plugin, radicle_form_key key, uint8_t id);
RADICLE_API const char* radicle_package_location_type_name(int32_t type);
RADICLE_API const char* radicle_package_target_type_name(int32_t type);

#ifdef __cplusplus
}
#endif
