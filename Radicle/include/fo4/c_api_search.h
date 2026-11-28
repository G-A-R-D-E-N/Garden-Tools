#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_conflict_category_bit {
  RADICLE_CONFLICT_BIT_UNKNOWN = 1,
  RADICLE_CONFLICT_BIT_ONLY_ONE = 2,
  RADICLE_CONFLICT_BIT_NO_CONFLICT = 4,
  RADICLE_CONFLICT_BIT_BENIGN = 8,
  RADICLE_CONFLICT_BIT_OVERRIDE = 16,
  RADICLE_CONFLICT_BIT_CONFLICT = 32,
  RADICLE_CONFLICT_BIT_CRITICAL = 64
} radicle_conflict_category_bit;

typedef struct radicle_record_search {
  const char* text;
  const char* form_id;
  const char* const* signatures;
  size_t signature_count;
  const char* editor_id;
  const char* editor_id_pattern;
  const char* display_name;
  const char* field_value;
  uint32_t conflict_category_mask;
  size_t limit;
} radicle_record_search;

RADICLE_API radicle_status radicle_env_search_records(const radicle_env* env,
                                                      const radicle_record_search* query,
                                                      char*** out_strings,
                                                      size_t* out_count,
                                                      int32_t* out_truncated);
RADICLE_API radicle_status radicle_env_search_records_with_strings(const radicle_env* env,
                                                                   const radicle_record_search* query,
                                                                   const radicle_strings* strings,
                                                                   char*** out_strings,
                                                                   size_t* out_count,
                                                                   int32_t* out_truncated);

RADICLE_API radicle_status radicle_env_diff_plugins(const radicle_env* env,
                                                    const char* before_plugin,
                                                    const char* after_plugin,
                                                    char*** out_strings,
                                                    size_t* out_count);

#ifdef __cplusplus
}
#endif
