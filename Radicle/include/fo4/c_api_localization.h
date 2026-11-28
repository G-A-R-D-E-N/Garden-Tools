#pragma once

#include "fo4/c_api_env.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_localization_tables radicle_localization_tables;

typedef enum radicle_plugin_localization_status {
  RADICLE_PLUGIN_LOCALIZATION_CONVERTED = 0,
  RADICLE_PLUGIN_LOCALIZATION_ALREADY_LOCALIZED = 1,
  RADICLE_PLUGIN_LOCALIZATION_NOT_LOCALIZED = 2,
  RADICLE_PLUGIN_LOCALIZATION_UNCONVERTIBLE_FIELD = 3,
  RADICLE_PLUGIN_LOCALIZATION_UNRESOLVED_STRING_ID = 4
} radicle_plugin_localization_status;

typedef struct radicle_plugin_localization_result {
  radicle_plugin_localization_status status;
  size_t converted;
  size_t empty_text;
} radicle_plugin_localization_result;

typedef enum radicle_localized_value_status {
  RADICLE_LOCALIZED_VALUE_UPDATED = 0,
  RADICLE_LOCALIZED_VALUE_UNCHANGED = 1,
  RADICLE_LOCALIZED_VALUE_RECORD_MISSING = 2,
  RADICLE_LOCALIZED_VALUE_SUBRECORD_MISSING = 3,
  RADICLE_LOCALIZED_VALUE_FIELD_NOT_LOCALIZED = 4,
  RADICLE_LOCALIZED_VALUE_PLUGIN_NOT_LOCALIZED = 5,
  RADICLE_LOCALIZED_VALUE_PAYLOAD_NOT_AN_ID = 6,
  RADICLE_LOCALIZED_VALUE_EMPTY_TEXT = 7,
  /* The string table that would hold the text is not the kind this field reads. */
  RADICLE_LOCALIZED_VALUE_TABLE_KIND_MISMATCH = 8
} radicle_localized_value_status;

RADICLE_API radicle_status radicle_localization_tables_create(radicle_localization_tables** out_tables);
RADICLE_API radicle_status radicle_localization_tables_load(const char* data_dir,
                                                            const char* plugin_file_name,
                                                            const char* language,
                                                            radicle_localization_tables** out_tables);
RADICLE_API radicle_status radicle_localization_tables_write(const radicle_localization_tables* tables,
                                                             const char* data_dir,
                                                             const char* plugin_file_name,
                                                             const char* language);
RADICLE_API void radicle_localization_tables_destroy(radicle_localization_tables* tables);

RADICLE_API radicle_status radicle_plugin_localize(radicle_plugin* plugin,
                                                   radicle_localization_tables* tables,
                                                   radicle_plugin_localization_result* out_result);
RADICLE_API radicle_status radicle_plugin_delocalize(radicle_plugin* plugin,
                                                     radicle_localization_tables* tables,
                                                     radicle_plugin_localization_result* out_result);

RADICLE_API radicle_status radicle_plugin_set_localized_string(radicle_plugin* plugin,
                                                               radicle_form_key record,
                                                               const char* subrecord_signature,
                                                               size_t index,
                                                               const char* text,
                                                               radicle_localization_tables* tables,
                                                               radicle_localized_value_status* out_status);

#ifdef __cplusplus
}
#endif
