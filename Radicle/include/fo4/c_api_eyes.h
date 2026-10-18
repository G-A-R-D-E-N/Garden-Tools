#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoded views require radicle_eyes_free. full_raw_size is the stored FULL length; full_text is NULL for opaque storage. */
typedef struct radicle_eyes_info {
  radicle_form_key record;
  uint32_t record_flags;
  const char* editor_id;
  const char* full_text;
  size_t full_raw_size;
  const char* icon;
  uint8_t data_flags;
  void* internal;
} radicle_eyes_info;

/* full_text and icon are required; record_flags must not carry master, deleted, compressed or small-file bits. */
typedef struct radicle_eyes_creation {
  const char* editor_id;
  const char* full_text;
  const char* icon;
  uint8_t data_flags;
  uint32_t record_flags;
} radicle_eyes_creation;

/* Decode requires an EYES record from the same environment; a malformed record reports RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_eyes_decode(const radicle_env* env,
                                               const radicle_record* record,
                                               radicle_eyes_info** out_info);
RADICLE_API void radicle_eyes_free(radicle_eyes_info* info);
RADICLE_API radicle_status radicle_plugin_create_eyes(radicle_plugin* plugin,
                                                      const radicle_eyes_creation* creation,
                                                      radicle_form_key* out_key);
/* A NULL text argument requires no change to that subrecord; a missing record reports RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_plugin_set_eyes_text(radicle_plugin* plugin,
                                                        radicle_form_key key,
                                                        const char* editor_id,
                                                        const char* full_text,
                                                        const char* icon);
RADICLE_API radicle_status radicle_plugin_set_eyes_data_flags(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              uint8_t flags);

#ifdef __cplusplus
}
#endif
