#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_condition {
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
} radicle_condition;

typedef struct radicle_condition_list {
  const radicle_condition* rows;
  size_t count;
  void* internal;
} radicle_condition_list;

typedef enum radicle_condition_write_status {
  RADICLE_CONDITION_UPDATED = 0,
  RADICLE_CONDITION_UNCHANGED = 1,
  RADICLE_CONDITION_NO_LIST = 2,
  RADICLE_CONDITION_MULTIPLE_LISTS = 3,
  RADICLE_CONDITION_ALIAS_STRUCTURE_INVALID = 4,
  RADICLE_CONDITION_SCOPE_INDEX_OUT_OF_RANGE = 5,
  RADICLE_CONDITION_ELEMENT_NOT_CONDITIONED = 6,
  RADICLE_CONDITION_UNREPRESENTABLE = 7
} radicle_condition_write_status;

RADICLE_API radicle_status radicle_record_conditions(const radicle_env* env,
                                                     const radicle_record* record,
                                                     const char* scope_path,
                                                     const radicle_condition_list** out_list);
RADICLE_API void radicle_condition_list_free(const radicle_condition_list* list);
RADICLE_API radicle_status radicle_plugin_set_conditions(
    radicle_plugin* plugin, radicle_form_key record, const char* scope_path,
    const radicle_condition* rows, size_t count, radicle_condition_write_status* out_status);

#ifdef __cplusplus
}
#endif
