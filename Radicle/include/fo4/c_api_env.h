#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable, thread-safe load-order environment; returned strings and records are borrowed until radicle_env_destroy. */
typedef struct radicle_env radicle_env;
typedef struct radicle_record radicle_record;

typedef struct radicle_form_key {
  const char* plugin;
  uint32_t local_id;
} radicle_form_key;

typedef struct radicle_env_plugin_info {
  const char* filename;
  const char* path;
  radicle_plugin_kind kind;
  int32_t master_flag;
  int32_t localized;
  size_t master_count;
  size_t unresolved_master_count;
} radicle_env_plugin_info;

typedef enum radicle_name_kind {
  RADICLE_NAME_ABSENT = 0,
  RADICLE_NAME_INLINE = 1,
  RADICLE_NAME_STRING_ID = 2,
  RADICLE_NAME_MALFORMED = 3
} radicle_name_kind;

typedef struct radicle_record_info {
  const char* owner_plugin;
  uint32_t local_id;
  char signature[5];
  size_t plugin_index;
  uint32_t record_flags;
  int32_t deleted;
  const char* editor_id;
  radicle_name_kind name_kind;
  const char* name_text;
  uint32_t name_string_id;
  const char* model_path;
  int32_t has_bounds;
  int16_t bounds_min[3];
  int16_t bounds_max[3];
} radicle_record_info;

/* Paths lowest priority first; any unreadable, duplicate or self-conflicting plugin fails the load. */
RADICLE_API radicle_status radicle_env_load(const char* const* paths,
                                            size_t count,
                                            radicle_env** out_env);
RADICLE_API void radicle_env_destroy(radicle_env* env);

RADICLE_API size_t radicle_env_plugin_count(const radicle_env* env);
RADICLE_API radicle_status radicle_env_get_plugin_info(const radicle_env* env,
                                                       size_t index,
                                                       radicle_env_plugin_info* out_info);
/* `out_resolved` is 0 when the master does not load earlier than the plugin. */
RADICLE_API radicle_status radicle_env_plugin_master(const radicle_env* env,
                                                     size_t plugin_index,
                                                     size_t master_index,
                                                     const char** out_filename,
                                                     int32_t* out_resolved);
/* Case-insensitive; RADICLE_STATUS_NOT_FOUND when the plugin is not loaded. */
RADICLE_API radicle_status radicle_env_find_plugin(const radicle_env* env,
                                                   const char* filename,
                                                   size_t* out_index);

/* RADICLE_STATUS_NOT_FOUND when no plugin holds the record. */
RADICLE_API radicle_status radicle_env_winner(const radicle_env* env,
                                              radicle_form_key key,
                                              const radicle_record** out_record);
/* Winner last; an unknown record is OK with NULL/0. Free the array with radicle_free_record_array. */
RADICLE_API radicle_status radicle_env_versions(const radicle_env* env,
                                                radicle_form_key key,
                                                const radicle_record*** out_records,
                                                size_t* out_count);
/* Case-insensitive winning EDID, highest priority first; `signature` is NULL or four characters. */
RADICLE_API radicle_status radicle_env_find_editor_id(const radicle_env* env,
                                                      const char* editor_id,
                                                      const char* signature,
                                                      const radicle_record** out_record);
/* Winners grouped by highest-priority plugin, then on-disk order; empty results as radicle_env_versions. */
RADICLE_API radicle_status radicle_env_winners_of(const radicle_env* env,
                                                  const char* signature,
                                                  const radicle_record*** out_records,
                                                  size_t* out_count);
RADICLE_API void radicle_free_record_array(const radicle_record** records);

/* Absent or malformed EDID/MODL report NULL; name_kind separates inline, string-ID and malformed FULL. */
RADICLE_API radicle_status radicle_record_get_info(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_record_info* out_info);

#ifdef __cplusplus
}
#endif
