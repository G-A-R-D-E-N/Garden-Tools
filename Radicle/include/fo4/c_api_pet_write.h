#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_field_kind {
  RADICLE_FIELD_SCALAR = 0,
  RADICLE_FIELD_STRING = 1,
  RADICLE_FIELD_FORMLINK = 2,
  RADICLE_FIELD_FORMLINK_LIST = 3,
  RADICLE_FIELD_COUNTED_ARRAY = 4,
  RADICLE_FIELD_SUBRECORD_GROUP = 5
} radicle_field_kind;

typedef struct radicle_field_schema_row {
  const char* path;
  radicle_field_kind kind;
  int32_t can_insert;
  int32_t repeated;
} radicle_field_schema_row;

typedef struct radicle_field_schema_name {
  const char* name;
  int64_t value;
} radicle_field_schema_name;

typedef struct radicle_field_schema_ex_row {
  const char* path;
  radicle_field_kind kind;
  int32_t can_insert;
  int32_t repeated;
  const char* subrecord;
  const char* count_subrecord;
  int32_t storage;
  int32_t presentation;
  int32_t embedded;
  int32_t localized;
  size_t offset;
  size_t payload_size;
  size_t stride;
  double minimum;
  double maximum;
  uint16_t min_form_version;
  uint16_t max_form_version;
  const radicle_field_schema_name* names;
  size_t name_count;
} radicle_field_schema_ex_row;

RADICLE_API radicle_status radicle_plugin_compact_to_esl(radicle_plugin* plugin,
                                                         size_t* out_remapped,
                                                         uint32_t* out_next_id);
RADICLE_API radicle_status radicle_plugin_set_light_flag(radicle_plugin* plugin, int32_t light);

/* Constraint: QUST group lists take Parent[i]\Child paths (i is 1-9 decimal digits, one bracket pair, only for a group child whose schema parent matches), e.g. QUST\Stages[2]\LogEntries and QUST\Objectives[0]\ObjectiveTargets; at and the size are then relative to that parent element, and scalar rows such as QUST\LogEntryFlags stay record-wide with a flat index. */
RADICLE_API radicle_status radicle_plugin_list_size(const radicle_plugin* plugin,
                                                    radicle_form_key key,
                                                    const char* path,
                                                    size_t* out_size);
RADICLE_API radicle_status radicle_plugin_list_insert(radicle_plugin* plugin,
                                                      radicle_form_key key,
                                                      const char* path,
                                                      size_t at,
                                                      const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_list_remove(radicle_plugin* plugin,
                                                      radicle_form_key key,
                                                      const char* path,
                                                      size_t at);

// Constraint: *out_text is malloc-owned and must be freed with radicle_record_scalar_free.
RADICLE_API radicle_status radicle_record_get_field(const radicle_env* env,
                                                    const radicle_record* record,
                                                    const char* path,
                                                    size_t index,
                                                    char** out_text);
RADICLE_API radicle_status radicle_record_list_size(const radicle_env* env,
                                                    const radicle_record* record,
                                                    const char* path,
                                                    size_t* out_size);
/* Constraint: path must name one schema record-flag row, such as LIGH\RandomAnimStart, and *out_value is 1 when that header bit is set and 0 otherwise; an unknown path, a row of another record type or a record of another environment is INVALID_ARGUMENT and leaves *out_value 0. */
RADICLE_API radicle_status radicle_record_get_flag(const radicle_env* env,
                                                   const radicle_record* record,
                                                   const char* path,
                                                   int32_t* out_value);
// Constraint: strings may be NULL, and a localized string must then return INVALID_ARGUMENT.
RADICLE_API radicle_status radicle_record_get_text(const radicle_env* env,
                                                   const radicle_strings* strings,
                                                   const radicle_record* record,
                                                   const char* path,
                                                   size_t index,
                                                   char** out_text);

// Constraint: text must be released with radicle_env_read_winners_free, and key.plugin must not be freed.
typedef struct radicle_winner_field {
  radicle_form_key key;
  char* text;
} radicle_winner_field;

RADICLE_API radicle_status radicle_env_read_winners(const radicle_env* env,
                                                    const radicle_strings* strings,
                                                    const char* path,
                                                    size_t index,
                                                    radicle_winner_field** out_rows,
                                                    size_t* out_count);
RADICLE_API void radicle_env_read_winners_free(radicle_winner_field* rows, size_t count);

typedef struct radicle_winner_fields_row {
  radicle_form_key key;
  char** texts;
} radicle_winner_fields_row;

// Constraint: each row's texts has path_count entries; a missing occurrence is NULL. Free with radicle_env_read_fields_free.
RADICLE_API radicle_status radicle_env_read_fields(const radicle_env* env,
                                                   const radicle_strings* strings,
                                                   const char* signature,
                                                   const char* const* paths,
                                                   size_t path_count,
                                                   size_t index,
                                                   radicle_winner_fields_row** out_rows,
                                                   size_t* out_count);
RADICLE_API void radicle_env_read_fields_free(radicle_winner_fields_row* rows, size_t count, size_t path_count);

/* Constraint: a counted-array or subrecord-group path takes the element-hex form radicle_record_get_field returns, and index == the current element count appends. A localized string path in a localized plugin needs radicle_plugin_strings_load first (INVALID_ARGUMENT otherwise); the write stores a string id and edits the loaded tables. */
RADICLE_API radicle_status radicle_plugin_set_record_field(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const char* path,
                                                           size_t index,
                                                           const char* text);
/* Constraint: loads the plugin's STRINGS/DLSTRINGS/ILSTRINGS files for one language from strings_dir; a missing file is an empty table. language is a non-empty alphanumeric token. INVALID_ARGUMENT unless the plugin is localized. */
RADICLE_API radicle_status radicle_plugin_strings_load(radicle_plugin* plugin,
                                                       const char* strings_dir,
                                                       const char* language);
/* Constraint: writes the three loaded tables as <stem>_<language>.<ext> in strings_dir, reusing each loaded file's name when plugin_file_name is NULL; plugin_file_name may be NULL to use the plugin's own file name. INVALID_ARGUMENT before radicle_plugin_strings_load. */
RADICLE_API radicle_status radicle_plugin_strings_save(const radicle_plugin* plugin,
                                                       const char* plugin_file_name,
                                                       const char* strings_dir,
                                                       const char* language);
// Constraint: rows are one malloc block freed by radicle_field_schema_free; an empty schema gives NULL rows and count 0.
RADICLE_API radicle_status radicle_record_field_schema(const char* signature,
                                                       radicle_field_schema_row** out_rows,
                                                       size_t* out_count);
RADICLE_API void radicle_field_schema_free(radicle_field_schema_row* rows);
// Constraint: rows are one malloc block freed by radicle_field_schema_ex_free.
RADICLE_API radicle_status radicle_record_field_schema_ex(const char* signature,
                                                         radicle_field_schema_ex_row** out_rows,
                                                         size_t* out_count);
RADICLE_API void radicle_field_schema_ex_free(radicle_field_schema_ex_row* rows);
// Constraint: signatures is one malloc block freed by radicle_schema_record_signatures_free.
RADICLE_API radicle_status radicle_schema_record_signatures(char*** out_signatures, size_t* out_count);
RADICLE_API void radicle_schema_record_signatures_free(char** signatures);

RADICLE_API radicle_status radicle_plugin_clean_deleted(radicle_plugin* plugin,
                                                        size_t* out_undeleted,
                                                        size_t* out_skipped);
RADICLE_API radicle_status radicle_plugin_remove_record(radicle_plugin* plugin, radicle_form_key key);

// Constraint: records nested in cell groups cannot be renumbered; the call returns INVALID_ARGUMENT and changes nothing.
RADICLE_API radicle_status radicle_plugin_renumber_form_id(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           uint32_t new_local_id,
                                                           int32_t repoint_links,
                                                           radicle_form_key* out_key);
// Constraint: a plugin with nested records, or ids that would leave the local range, returns INVALID_ARGUMENT and changes nothing.
RADICLE_API radicle_status radicle_plugin_renumber_all(radicle_plugin* plugin,
                                                       uint32_t start_local_id,
                                                       size_t* out_count);

RADICLE_API radicle_status radicle_plugin_copy_record_version(radicle_plugin* plugin,
                                                              const radicle_env* env,
                                                              const radicle_record* version,
                                                              int32_t overwrite,
                                                              radicle_record_copy_status* out_status);
RADICLE_API radicle_status radicle_plugin_retarget_links(radicle_plugin* plugin,
                                                         radicle_form_key record,
                                                         radicle_form_key from_key,
                                                         radicle_form_key to_key,
                                                         size_t* out_changed);

#ifdef __cplusplus
}
#endif
