#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded views require radicle_armor_free and radicle_armor_addon_free. */
typedef struct radicle_armor_info {
  radicle_form_key record;
  const char* editor_id;
  const char* male_world_model;
  const char* male_icon;
  const char* male_message_icon;
  const char* female_world_model;
  const char* female_icon;
  const char* female_message_icon;
  radicle_name_kind full_name_kind;
  const char* full_name_text;
  uint32_t full_name_string_id;
  radicle_name_kind description_kind;
  const char* description_text;
  uint32_t description_string_id;
  int32_t value;
  float weight;
  uint32_t health;
  void* internal;
} radicle_armor_info;

typedef struct radicle_armor_addon_info {
  radicle_form_key record;
  const char* editor_id;
  const char* male_biped_model;
  const char* female_biped_model;
  const char* male_first_person_model;
  const char* female_first_person_model;
  uint8_t male_priority;
  uint8_t female_priority;
  uint8_t male_weight_slider;
  uint8_t female_weight_slider;
  uint8_t unknown_before_detection[2];
  uint8_t detection_sound_value;
  uint8_t unknown_before_weapon_adjust;
  float weapon_adjust;
  void* internal;
} radicle_armor_addon_info;

/* A NULL creation string leaves the field absent; localized DESC requires inline text. */
typedef struct radicle_armor_creation {
  const char* editor_id;
  const char* male_world_model;
  const char* male_icon;
  const char* male_message_icon;
  const char* female_world_model;
  const char* female_icon;
  const char* female_message_icon;
  const char* description;
  int32_t value;
  float weight;
  uint32_t health;
} radicle_armor_creation;

typedef struct radicle_armor_addon_creation {
  const char* editor_id;
  const char* male_biped_model;
  const char* female_biped_model;
  const char* male_first_person_model;
  const char* female_first_person_model;
  uint8_t male_priority;
  uint8_t female_priority;
  uint8_t male_weight_slider;
  uint8_t female_weight_slider;
  uint8_t unknown_before_detection[2];
  uint8_t detection_sound_value;
  uint8_t unknown_before_weapon_adjust;
  float weapon_adjust;
} radicle_armor_addon_creation;

/* Decode requires a record from the same environment; a malformed record reports RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_armor_decode(const radicle_env* env,
                                                const radicle_record* record,
                                                radicle_armor_info** out_info);
RADICLE_API void radicle_armor_free(radicle_armor_info* info);
RADICLE_API radicle_status radicle_armor_addon_decode(const radicle_env* env,
                                                      const radicle_record* record,
                                                      radicle_armor_addon_info** out_info);
RADICLE_API void radicle_armor_addon_free(radicle_armor_addon_info* info);
RADICLE_API radicle_status radicle_plugin_create_armor(radicle_plugin* plugin,
                                                       const radicle_armor_creation* creation,
                                                       radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_create_armor_addon(radicle_plugin* plugin,
                                                             const radicle_armor_addon_creation* creation,
                                                             radicle_form_key* out_key);
/* A NULL model string requires no change to that subrecord; a missing record reports RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_plugin_set_armor_models(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const char* male_world_model,
                                                           const char* male_icon,
                                                           const char* male_message_icon,
                                                           const char* female_world_model,
                                                           const char* female_icon,
                                                           const char* female_message_icon);
RADICLE_API radicle_status radicle_plugin_set_armor_addon_models(radicle_plugin* plugin,
                                                                 radicle_form_key key,
                                                                 const char* male_biped_model,
                                                                 const char* female_biped_model,
                                                                 const char* male_first_person_model,
                                                                 const char* female_first_person_model);

#ifdef __cplusplus
}
#endif
