#pragma once

#include "fo4/c_api_components.h"
#include "fo4/c_api_conditions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_ui_text {
  radicle_name_kind kind;
  const char* text;
  uint32_t string_id;
} radicle_ui_text;

typedef struct radicle_form_link {
  char subrecord[5];
  size_t ordinal;
  size_t offset;
  uint32_t raw_form_id;
  radicle_form_key key;
} radicle_form_link;

typedef struct radicle_form_links {
  const radicle_form_link* links;
  size_t link_count;
  void* internal;
} radicle_form_links;

RADICLE_API radicle_status radicle_record_form_links(const radicle_env* env,
                                                     const radicle_record* record,
                                                     radicle_form_links** out_links);
RADICLE_API void radicle_form_links_free(radicle_form_links* links);

/* Constraint: a key with an empty owner means the CTDA slot is not a form link for that function; keys are resolved through the plugin masters, unlike the raw ids in radicle_condition. */
typedef struct radicle_condition_links {
  radicle_form_key global;
  radicle_form_key param1;
  radicle_form_key param2;
  radicle_form_key reference;
} radicle_condition_links;

typedef struct radicle_constructible_object {
  radicle_form_key record;
  radicle_form_key workbench_keyword;
  radicle_form_key created_object;
  uint8_t has_created_count;
  uint8_t has_priority;
  uint16_t created_count;
  uint16_t priority;
  radicle_ui_text description;
  const radicle_form_key* category_keywords;
  size_t category_keyword_count;
  const radicle_component_entry* components;
  size_t component_count;
  /* Constraint: conditions[i] and condition_links[i] describe the same CTDA row; all rows must hold for the recipe to be craftable. */
  const radicle_condition* conditions;
  const radicle_condition_links* condition_links;
  size_t condition_count;
  void* internal;
} radicle_constructible_object;

RADICLE_API radicle_status radicle_constructible_object_decode(const radicle_env* env,
                                                               const radicle_record* record,
                                                               radicle_constructible_object** out_object);
RADICLE_API void radicle_constructible_object_free(radicle_constructible_object* object);

/* Constraint: value is valid only when has_value is 1; entry_point and function are raw bytes; conditions[i] pairs with condition_links[i]. */
typedef struct radicle_perk_entry_point_tab {
  int8_t run_on;
  const radicle_condition* conditions;
  const radicle_condition_links* condition_links;
  size_t condition_count;
} radicle_perk_entry_point_tab;

typedef struct radicle_perk_entry_point_effect {
  uint8_t rank;
  uint8_t priority;
  uint8_t entry_point;
  uint8_t function;
  uint8_t has_parameter_type;
  uint8_t parameter_type;
  uint8_t has_value;
  float value;
  const radicle_perk_entry_point_tab* tabs;
  size_t tab_count;
} radicle_perk_entry_point_effect;

typedef struct radicle_perk_entry_points_info {
  radicle_form_key record;
  const radicle_perk_entry_point_effect* effects;
  size_t effect_count;
  void* internal;
} radicle_perk_entry_points_info;

RADICLE_API radicle_status radicle_perk_entry_points_decode(const radicle_env* env,
                                                            const radicle_record* record,
                                                            radicle_perk_entry_points_info** out_info);
RADICLE_API void radicle_perk_entry_points_free(radicle_perk_entry_points_info* info);

/* Constraint: value1 and value2 are the raw four-byte slots; value1_form is the master-resolved key when value_type is 4 or 6 and a null key otherwise. */
typedef struct radicle_omod_property_row {
  uint8_t value_type;
  uint8_t function_type;
  uint16_t property;
  uint8_t value1[4];
  uint8_t value2[4];
  float step;
  radicle_form_key value1_form;
} radicle_omod_property_row;

typedef struct radicle_omod_properties_info {
  radicle_form_key record;
  const radicle_omod_property_row* properties;
  size_t property_count;
  void* internal;
} radicle_omod_properties_info;

RADICLE_API radicle_status radicle_omod_properties_decode(const radicle_env* env,
                                                          const radicle_record* record,
                                                          radicle_omod_properties_info** out_info);
RADICLE_API void radicle_omod_properties_free(radicle_omod_properties_info* info);

typedef enum radicle_terminal_item_type {
  RADICLE_TERMINAL_ITEM_SUBMENU = 4,
  RADICLE_TERMINAL_ITEM_RETURN_TO_TOP = 5,
  RADICLE_TERMINAL_ITEM_FORCE_REDRAW = 6,
  RADICLE_TERMINAL_ITEM_DISPLAY_TEXT = 8,
  RADICLE_TERMINAL_ITEM_DISPLAY_IMAGE = 16
} radicle_terminal_item_type;

typedef struct radicle_terminal_body_text {
  radicle_ui_text text;
  size_t condition_count;
  /* Constraint: conditions[i] and condition_links[i] describe the same CTDA row of this body text; all rows must hold for it to be chosen. */
  const radicle_condition* conditions;
  const radicle_condition_links* condition_links;
} radicle_terminal_body_text;

typedef struct radicle_terminal_menu_item {
  radicle_ui_text item_text;
  radicle_ui_text response_text;
  radicle_ui_text display_text;
  uint8_t has_type;
  uint8_t type;
  uint8_t has_item_id;
  uint16_t item_id;
  const char* image;
  radicle_form_key submenu;
  size_t condition_count;
  /* Constraint: conditions[i] and condition_links[i] describe the same CTDA row of this menu item; all rows must hold for it to be listed. */
  const radicle_condition* conditions;
  const radicle_condition_links* condition_links;
} radicle_terminal_menu_item;

typedef struct radicle_terminal_holotape {
  radicle_form_key holotape;
  int32_t count;
} radicle_terminal_holotape;

typedef struct radicle_terminal_info {
  radicle_form_key record;
  radicle_ui_text full_name;
  radicle_ui_text header_text;
  radicle_ui_text welcome_text;
  const radicle_terminal_body_text* body_texts;
  size_t body_text_count;
  const radicle_terminal_menu_item* menu_items;
  size_t menu_item_count;
  const radicle_terminal_holotape* holotapes;
  size_t holotape_count;
  void* internal;
} radicle_terminal_info;

RADICLE_API radicle_status radicle_terminal_decode(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_terminal_info** out_info);
RADICLE_API void radicle_terminal_free(radicle_terminal_info* info);

typedef struct radicle_item_count {
  radicle_form_key item;
  int32_t count;
} radicle_item_count;

/* Constraint: a link with plugin == NULL is absent; a text of kind ABSENT is absent; each decode requires its matching free. */
typedef struct radicle_activator_info {
  radicle_form_key record;
  radicle_ui_text full_name;
  radicle_ui_text activate_text;
  uint8_t has_flags;
  uint16_t flags;
  radicle_form_key interaction_keyword;
  radicle_form_key looping_sound;
  radicle_form_key activation_sound;
  radicle_form_key water_type;
  void* internal;
} radicle_activator_info;

RADICLE_API radicle_status radicle_activator_decode(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_activator_info** out_info);
RADICLE_API void radicle_activator_free(radicle_activator_info* info);

/* Constraint: items keep CNTO order and drop null item links. */
typedef struct radicle_container_info {
  radicle_form_key record;
  radicle_ui_text full_name;
  uint8_t has_data;
  uint8_t flags;
  float weight;
  const radicle_item_count* items;
  size_t item_count;
  radicle_form_key open_sound;
  radicle_form_key close_sound;
  radicle_form_key take_all_sound;
  radicle_form_key filter_list;
  void* internal;
} radicle_container_info;

RADICLE_API radicle_status radicle_container_decode(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_container_info** out_info);
RADICLE_API void radicle_container_free(radicle_container_info* info);

/* Constraint: random_destinations are the TNAM CELL/WRLD links in stored order. */
typedef struct radicle_door_info {
  radicle_form_key record;
  radicle_ui_text full_name;
  uint8_t has_flags;
  uint8_t flags;
  radicle_form_key open_sound;
  radicle_form_key close_sound;
  radicle_form_key loop_sound;
  radicle_ui_text open_text;
  radicle_ui_text close_text;
  const radicle_form_key* random_destinations;
  size_t random_destination_count;
  void* internal;
} radicle_door_info;

RADICLE_API radicle_status radicle_door_decode(const radicle_env* env,
                                               const radicle_record* record,
                                               radicle_door_info** out_info);
RADICLE_API void radicle_door_free(radicle_door_info* info);

typedef struct radicle_furniture_marker {
  int32_t index;
  uint8_t has_disabled_entry_points;
  uint16_t disabled_entry_points;
} radicle_furniture_marker;

typedef struct radicle_furniture_entry_point {
  uint16_t anim_type;
  uint16_t entry_points;
} radicle_furniture_entry_point;

/* Constraint: entry_types is 0 below record form version 125. */
typedef struct radicle_furniture_marker_params {
  float offset[3];
  float rotation_z;
  radicle_form_key keyword;
  uint8_t entry_types;
} radicle_furniture_marker_params;

/* Constraint: marker_model is NULL when XMRK is absent. */
typedef struct radicle_furniture_info {
  radicle_form_key record;
  radicle_ui_text full_name;
  radicle_ui_text activate_text;
  uint8_t has_flags;
  uint16_t flags;
  uint8_t has_marker_flags;
  uint32_t marker_flags;
  uint8_t has_bench_type;
  uint8_t bench_type;
  radicle_form_key drinking_water_type;
  radicle_form_key associated_form;
  const radicle_item_count* items;
  size_t item_count;
  const radicle_furniture_marker* markers;
  size_t marker_count;
  const radicle_furniture_entry_point* entry_points;
  size_t entry_point_count;
  const radicle_furniture_marker_params* marker_params;
  size_t marker_param_count;
  const char* marker_model;
  void* internal;
} radicle_furniture_info;

RADICLE_API radicle_status radicle_furniture_decode(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_furniture_info** out_info);
RADICLE_API void radicle_furniture_free(radicle_furniture_info* info);

typedef enum radicle_body_part_type {
  RADICLE_BODY_PART_TORSO = 0,
  RADICLE_BODY_PART_HEAD1 = 1,
  RADICLE_BODY_PART_EYE = 2,
  RADICLE_BODY_PART_LOOK_AT = 3,
  RADICLE_BODY_PART_FLY_GRAB = 4,
  RADICLE_BODY_PART_HEAD2 = 5,
  RADICLE_BODY_PART_LEFT_ARM1 = 6,
  RADICLE_BODY_PART_LEFT_ARM2 = 7,
  RADICLE_BODY_PART_RIGHT_ARM1 = 8,
  RADICLE_BODY_PART_RIGHT_ARM2 = 9,
  RADICLE_BODY_PART_LEFT_LEG1 = 10,
  RADICLE_BODY_PART_LEFT_LEG2 = 11,
  RADICLE_BODY_PART_LEFT_LEG3 = 12,
  RADICLE_BODY_PART_RIGHT_LEG1 = 13,
  RADICLE_BODY_PART_RIGHT_LEG2 = 14,
  RADICLE_BODY_PART_RIGHT_LEG3 = 15,
  RADICLE_BODY_PART_BRAIN = 16,
  RADICLE_BODY_PART_WEAPON = 17,
  RADICLE_BODY_PART_ROOT = 18,
  RADICLE_BODY_PART_COM = 19,
  RADICLE_BODY_PART_PELVIS = 20,
  RADICLE_BODY_PART_CAMERA = 21,
  RADICLE_BODY_PART_OFFSET_ROOT = 22,
  RADICLE_BODY_PART_LEFT_FOOT = 23,
  RADICLE_BODY_PART_RIGHT_FOOT = 24,
  RADICLE_BODY_PART_FACE_TARGET_SOURCE = 25
} radicle_body_part_type;

/* Constraint: has_data is 0 when the part has no BPND and the fields after it must then be ignored; flags is the raw BPND flag byte (bit 0 Severable, 1 Hit Reaction, 2 Hit Reaction - Default, 3 Explodable, 4 Cut - Meat Cap Sever, 5 On Cripple, 6 Explodable - Absolute Chance, 7 Show Cripple Geometry); part_type holds a radicle_body_part_type or an unlisted raw byte; name.kind is RADICLE_NAME_ABSENT and part_node and vats_target are NULL when the part lacks BPTN, BPNN or BPNT. */
typedef struct radicle_body_part_info {
  radicle_ui_text name;
  const char* part_node;
  const char* vats_target;
  uint8_t has_data;
  float damage_mult;
  uint8_t flags;
  uint8_t part_type;
  uint8_t health_percent;
  radicle_form_key actor_value;
  uint8_t to_hit_chance;
  uint8_t explodable_explosion_chance;
  uint8_t non_lethal_dismemberment_chance;
  uint8_t geometry_segment_index;
} radicle_body_part_info;

typedef struct radicle_body_part_data_info {
  radicle_form_key record;
  const radicle_body_part_info* parts;
  size_t part_count;
  void* internal;
} radicle_body_part_data_info;

RADICLE_API radicle_status radicle_body_part_data_decode(const radicle_env* env,
                                                         const radicle_record* record,
                                                         radicle_body_part_data_info** out_info);
RADICLE_API void radicle_body_part_data_free(radicle_body_part_data_info* info);

typedef enum radicle_lock_kind {
  RADICLE_LOCK_NONE = 0,
  RADICLE_LOCK_NOVICE = 1,
  RADICLE_LOCK_ADVANCED = 2,
  RADICLE_LOCK_EXPERT = 3,
  RADICLE_LOCK_MASTER = 4,
  RADICLE_LOCK_BARRED = 5,
  RADICLE_LOCK_CHAINED = 6,
  RADICLE_LOCK_REQUIRES_TERMINAL = 7,
  RADICLE_LOCK_INACCESSIBLE = 8,
  RADICLE_LOCK_REQUIRES_KEY = 9
} radicle_lock_kind;

typedef struct radicle_lock_info {
  uint8_t has_lock_data;
  uint8_t level;
  radicle_lock_kind kind;
  radicle_form_key key;
  uint8_t flags;
  uint8_t leveled_lock;
} radicle_lock_info;

RADICLE_API radicle_status radicle_lock_info_read(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_lock_info* out_info);

#ifdef __cplusplus
}
#endif
