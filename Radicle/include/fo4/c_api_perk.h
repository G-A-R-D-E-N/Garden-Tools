#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

// Constraint: one radicle_perk_effect is one PRKE..PRKF run and the type selects the quest, ability or entry-point arm; tabs carries the PRKC runs in file order, parameter_type is the EPFT byte and data is the EPFD payload for that arm.
// Constraint: parameter1, parameter2 and button_label are UTF-8 on this surface and are stored as Windows-1252, so text that is not valid UTF-8 or has no Windows-1252 form is INVALID_ARGUMENT.
// Constraint: exactly one of button_label, button_label_id or button_label_raw is set when has_button_label is 1, and a malformed payload must be preserved byte for byte.
typedef struct radicle_perk_condition {
  uint8_t flags;
  uint8_t padding[3];
  uint32_t comparison_raw;
  uint16_t function;
  uint16_t unknown2;
  int32_t param1;
  int32_t param2;
  uint32_t run_on;
  uint32_t reference;
  int32_t unknown3;
  const char* parameter1;
  const char* parameter2;
} radicle_perk_condition;

typedef struct radicle_perk_condition_tab {
  int8_t run_on;
  const radicle_perk_condition* conditions;
  size_t condition_count;
} radicle_perk_condition_tab;

typedef struct radicle_perk_effect {
  uint8_t type;
  uint8_t rank;
  uint8_t priority;
  radicle_form_key quest;
  uint16_t quest_stage;
  radicle_form_key ability;
  uint8_t entry_point;
  uint8_t function;
  uint8_t condition_tab_count;
  const radicle_perk_condition_tab* tabs;
  size_t tab_count;
  int32_t has_parameter_type;
  uint8_t parameter_type;
  int32_t has_entry_id;
  uint16_t entry_id;
  int32_t has_button_label;
  uint32_t button_label_id;
  const char* button_label;
  const uint8_t* button_label_raw;
  size_t button_label_raw_size;
  int32_t has_script_flags;
  uint16_t script_flags;
  const uint8_t* data;
  size_t data_size;
  void* internal;
} radicle_perk_effect;

RADICLE_API radicle_status radicle_plugin_perk_effects(const radicle_plugin* plugin,
                                                       radicle_form_key key,
                                                       radicle_perk_effect** out_effects,
                                                       size_t* out_count);
RADICLE_API void radicle_perk_effects_free(radicle_perk_effect* effects);

// Constraint: a NULL array with count 0 removes every effect and leaves the header intact; a quest effect must name its quest and an ability its ability, or the call is INVALID_ARGUMENT.
RADICLE_API radicle_status radicle_plugin_set_perk_effects(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const radicle_perk_effect* effects,
                                                           size_t count);

// Constraint: nothing is written if the create or any effect fails, and effects may be NULL with count 0 to create a perk without effects.
RADICLE_API radicle_status radicle_plugin_create_perk(radicle_plugin* plugin,
                                                      const char* editor_id_utf8,
                                                      const radicle_perk_effect* effects,
                                                      size_t count,
                                                      radicle_form_key* out_key);

#ifdef __cplusplus
}
#endif
