#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_destructible_header_status {
  RADICLE_DESTRUCTIBLE_HEADER_UPDATED = 0,
  RADICLE_DESTRUCTIBLE_HEADER_PRESENT = 1,
  RADICLE_DESTRUCTIBLE_HEADER_MISSING = 2,
  RADICLE_DESTRUCTIBLE_HEADER_UNKNOWN_ORDER = 3,
  RADICLE_DESTRUCTIBLE_HEADER_TOO_MANY_STAGES = 4
} radicle_destructible_header_status;

typedef enum radicle_destructible_stage_status {
  RADICLE_DESTRUCTIBLE_STAGE_UPDATED = 0,
  RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING = 1,
  RADICLE_DESTRUCTIBLE_STAGE_INDEX_OUT_OF_RANGE = 2,
  RADICLE_DESTRUCTIBLE_STAGE_TOO_MANY_STAGES = 3,
  RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED = 4,
  RADICLE_DESTRUCTIBLE_STAGE_MALFORMED = 5,
  RADICLE_DESTRUCTIBLE_STAGE_PRESENT = 6
} radicle_destructible_stage_status;

typedef enum radicle_destructible_stage_member_status {
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED = 0,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_HEADER_MISSING = 1,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_STAGE_INDEX_OUT_OF_RANGE = 2,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NOT_OPTIONAL = 3,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NO_DEFAULT = 4,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_PRESENT = 5,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_ABSENT = 6,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_MODEL_REQUIRED = 7,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_INTERLEAVED = 8,
  RADICLE_DESTRUCTIBLE_STAGE_MEMBER_MALFORMED = 9
} radicle_destructible_stage_member_status;

typedef struct radicle_destructible_member {
  char signature[5];
  const uint8_t* data;
  size_t size;
} radicle_destructible_member;

typedef struct radicle_destructible_resistance {
  radicle_form_key damage_type;
  uint32_t value;
} radicle_destructible_resistance;

typedef struct radicle_destructible_stage {
  const radicle_destructible_member* members;
  size_t member_count;
} radicle_destructible_stage;

typedef struct radicle_destructible_header {
  int32_t health;
  uint8_t stage_count;
  uint8_t flags;
} radicle_destructible_header;

typedef struct radicle_destructible_group {
  radicle_destructible_header header;
  const radicle_destructible_resistance* resistances;
  size_t resistance_count;
  const radicle_destructible_stage* stages;
  size_t stage_count;
} radicle_destructible_group;

// Constraint: group arrays and resistance owner names are owned by the caller and must be released with radicle_free_destructible_group; a refusal fills no group fields.
RADICLE_API radicle_status radicle_plugin_destructible_group_read(
    const radicle_plugin* plugin, radicle_form_key record, radicle_destructible_group* out_group,
    radicle_destructible_stage_status* out_status);
RADICLE_API void radicle_free_destructible_group(radicle_destructible_group* group);

RADICLE_API radicle_status radicle_plugin_insert_destructible_header(
    radicle_plugin* plugin, radicle_form_key record,
    radicle_destructible_header_status* out_status);
RADICLE_API radicle_status radicle_plugin_remove_destructible_header(
    radicle_plugin* plugin, radicle_form_key record,
    radicle_destructible_header_status* out_status);
RADICLE_API radicle_status radicle_plugin_insert_destructible_stage(
    radicle_plugin* plugin, radicle_form_key record, size_t index,
    radicle_destructible_stage_status* out_status);
RADICLE_API radicle_status radicle_plugin_remove_destructible_stage(
    radicle_plugin* plugin, radicle_form_key record, size_t index,
    radicle_destructible_stage_status* out_status);
RADICLE_API radicle_status radicle_plugin_insert_destructible_stage_member(
    radicle_plugin* plugin, radicle_form_key record, size_t stage_index, const char* member,
    radicle_destructible_stage_member_status* out_status);
RADICLE_API radicle_status radicle_plugin_remove_destructible_stage_member(
    radicle_plugin* plugin, radicle_form_key record, size_t stage_index, const char* member,
    radicle_destructible_stage_member_status* out_status);

#ifdef __cplusplus
}
#endif
