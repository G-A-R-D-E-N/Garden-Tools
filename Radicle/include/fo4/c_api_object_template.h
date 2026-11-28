#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_object_template_link_kind {
  RADICLE_OBJECT_TEMPLATE_KEYWORD = 0,
  RADICLE_OBJECT_TEMPLATE_INCLUDE = 1,
  RADICLE_OBJECT_TEMPLATE_PROPERTY = 2,
} radicle_object_template_link_kind;

typedef enum radicle_object_template_row_operation {
  RADICLE_OBJECT_TEMPLATE_CLONE = 0,
  RADICLE_OBJECT_TEMPLATE_REMOVE = 1,
  RADICLE_OBJECT_TEMPLATE_MOVE = 2,
} radicle_object_template_row_operation;

typedef enum radicle_object_template_write {
  RADICLE_OBJECT_TEMPLATE_UPDATED = 0,
  RADICLE_OBJECT_TEMPLATE_RECORD_MISSING = 1,
  RADICLE_OBJECT_TEMPLATE_UNSUPPORTED_RECORD = 2,
  RADICLE_OBJECT_TEMPLATE_COMBINATION_MISSING = 3,
  RADICLE_OBJECT_TEMPLATE_MALFORMED = 4,
  RADICLE_OBJECT_TEMPLATE_INDEX_OUT_OF_RANGE = 5,
  RADICLE_OBJECT_TEMPLATE_INVALID_VALUE = 6,
  RADICLE_OBJECT_TEMPLATE_AMBIGUOUS = 7,
} radicle_object_template_write;

typedef struct radicle_object_template_include {
  radicle_form_key mod;
  uint8_t attach_point;
  uint8_t optional;
  uint8_t dont_use_all;
} radicle_object_template_include;

typedef struct radicle_object_template_property {
  uint8_t value_type;
  uint8_t function_type;
  uint16_t property;
  uint32_t value1;
  uint32_t value2;
  uint32_t step_bits;
  uint8_t has_form_value;
  radicle_form_key form_value;
} radicle_object_template_property;

typedef struct radicle_object_template_rows {
  const radicle_form_key* keywords;
  size_t keyword_count;
  const radicle_object_template_include* includes;
  size_t include_count;
  const radicle_object_template_property* properties;
  size_t property_count;
  void* internal;
  uint8_t min_level_for_ranks;
  uint8_t alt_levels_per_tier;
} radicle_object_template_rows;

typedef struct radicle_object_template_insert_row {
  radicle_form_key link;
  uint8_t attach_point;
  uint8_t optional;
  uint8_t dont_use_all;
  radicle_object_template_property property;
} radicle_object_template_insert_row;

/* Constraint: rows and their pointers remain valid until radicle_object_template_rows_free. */
RADICLE_API radicle_status radicle_plugin_object_template_rows(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    const radicle_object_template_rows** out_rows, radicle_object_template_write* out_status);
RADICLE_API void radicle_object_template_rows_free(const radicle_object_template_rows* rows);

RADICLE_API radicle_status radicle_plugin_set_object_template_rank_tier(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    uint8_t min_level_for_ranks, uint8_t alt_levels_per_tier,
    radicle_object_template_write* out_status);

RADICLE_API radicle_status radicle_plugin_set_object_template_include_options(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    uint8_t attach_point, uint8_t optional, uint8_t dont_use_all,
    radicle_object_template_write* out_status);
/* Constraint: part 0 is value1, 1 is value2 and 2 is the raw float step bits. */
RADICLE_API radicle_status radicle_plugin_set_object_template_property_value(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    uint8_t part, uint32_t value, radicle_object_template_write* out_status);
RADICLE_API radicle_status radicle_plugin_set_object_template_property(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    const radicle_object_template_property* property, radicle_object_template_write* out_status);

RADICLE_API radicle_status radicle_plugin_set_object_template_link(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, size_t index, radicle_form_key target,
    radicle_object_template_write* out_status);

RADICLE_API radicle_status radicle_plugin_edit_object_template_row(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, radicle_object_template_row_operation operation,
    size_t source, size_t destination, radicle_object_template_write* out_status);

RADICLE_API radicle_status radicle_plugin_insert_object_template_row(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, size_t index,
    const radicle_object_template_insert_row* row, radicle_object_template_write* out_status);

#ifdef __cplusplus
}
#endif
