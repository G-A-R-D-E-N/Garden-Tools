#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A rank title requires a matching stored form: a string ID in a localized plugin, inline text otherwise. */
typedef struct radicle_faction_rank_input {
  radicle_name_kind male_title_kind;
  const char* male_title_text;
  uint32_t male_title_string_id;
  radicle_name_kind female_title_kind;
  const char* female_title_text;
  uint32_t female_title_string_id;
  const uint8_t* insignia;
  size_t insignia_size;
} radicle_faction_rank_input;

/* full_name must be inline text in a non-localized plugin; a NULL argument leaves the field absent. */
RADICLE_API radicle_status radicle_plugin_create_faction(radicle_plugin* plugin,
                                                         const char* editor_id,
                                                         const char* full_name,
                                                         uint32_t flags,
                                                         radicle_form_key* out_key);
/* A missing record reports RADICLE_STATUS_NOT_FOUND; an out-of-range index must report RADICLE_STATUS_INVALID_ARGUMENT. */
/* reaction must be the stored XNAM byte: 0 neutral, 1 enemy, 2 ally, 3 friendly. */
RADICLE_API radicle_status radicle_plugin_add_faction_relation(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               radicle_form_key faction,
                                                               int32_t modifier,
                                                               uint32_t reaction);
RADICLE_API radicle_status radicle_plugin_set_faction_relation(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               size_t index,
                                                               radicle_form_key faction,
                                                               int32_t modifier,
                                                               uint32_t reaction);
RADICLE_API radicle_status radicle_plugin_remove_faction_relation(radicle_plugin* plugin,
                                                                  radicle_form_key key,
                                                                  size_t index);
RADICLE_API radicle_status radicle_plugin_add_faction_rank(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           uint32_t number,
                                                           const radicle_faction_rank_input* titles);
RADICLE_API radicle_status radicle_plugin_set_faction_rank(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           size_t index,
                                                           uint32_t number,
                                                           const radicle_faction_rank_input* titles);
RADICLE_API radicle_status radicle_plugin_remove_faction_rank(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              size_t index);

#ifdef __cplusplus
}
#endif
