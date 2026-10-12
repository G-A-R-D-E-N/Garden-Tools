#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Quest runtime rows.
 *
 * radicle_quest_runtime_rows decodes one QUST record's VMAD and DNAM through Radicle's
 * QUST/VMAD reader and returns one tab-separated row per structural fact. Rows are released
 * with radicle_free_string_array from fo4/c_api.h, and the same contract applies: a row is a
 * NUL-terminated string owned by the caller.
 *
 *   quest <form key> <start game enabled 0|1>
 *   script <script name>
 *   fragment-script <script name>
 *   fragment <stage> <stage index> <script> <function>
 *   alias <alias id>
 *   alias-script <alias id> <script name>
 *   unsupported <reason>
 *
 * Script properties are not rows. A property carries a typed value, a value can hold a tab or
 * a newline, and an object property carries a FormID and an alias at once, so properties
 * travel through radicle_quest_runtime_properties instead of the tab-separated grammar.
 *
 * An `unsupported` row is the explicit report the evidence rules require: the reader never
 * guesses, so anything it could not decode is counted in the row set instead of being
 * silently dropped. It is not an error status. A missing quest, or a form key that is not a
 * QUST, produces an `unsupported` row as well.
 *
 * `form_key` is the same "000800:Plugin.esp" text radicle_plugin_collect_form_keys returns.
 */
RADICLE_API radicle_status radicle_quest_runtime_rows(const radicle_plugin* plugin,
                                                       const char* form_key,
                                                       char*** out_rows,
                                                       size_t* out_count);

/*
 * What a property is attached to. A fragment property belongs to the fragment script the
 * `fragment-script` row names, so its own script name is empty and the fragment script name
 * is transferred exactly once, through that row.
 */
typedef enum radicle_quest_property_owner {
  RADICLE_QUEST_PROPERTY_SCRIPT = 0,
  RADICLE_QUEST_PROPERTY_FRAGMENT = 1,
  RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT = 2
} radicle_quest_property_owner;

/*
 * One VMAD property with its decoded value.
 *
 * The scalar and reference values each have a `has_` companion: `has_integer`, `has_real`,
 * `has_boolean`, `has_object`, `has_property_alias` and `has_element_count`. A VMAD property
 * carries at most one value of its type, and an object property carries a FormID and an alias
 * together. The string value has no such flag: absence is a NULL `text`, and a non-NULL `text`
 * with a `text_size` of 0 is the empty string. `element_count` is the leading count of a
 * struct or array property. `alias` is the owning alias for
 * RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT and -1 otherwise, while `property_alias` is the alias
 * the property itself names.
 *
 * `text` and `text_size` describe the string value, and `text_size` is the transfer contract:
 * a NULL `text` means the property holds no string, a non-NULL `text` points at exactly
 * `text_size` raw bytes followed by one trailing NUL for convenience, and a non-NULL `text`
 * with a `text_size` of 0 is the empty string. `text_size` counts embedded NUL bytes as data,
 * so the 12 bytes Garden NUL Patch arrive as `text_size` 12 and not as the 6 bytes a
 * NUL-terminated reader would see. The bytes are not claimed to be any particular character
 * encoding: this ABI carries bytes and a length, and naming a charset is the caller's business.
 *
 * `has_elements` is 1 only when every element of an array property was decoded; then `elements`
 * points at `element_value_count` radicle_script_element values. Otherwise `has_elements` and
 * `element_value_count` are 0 and `elements` is NULL.
 *
 * `has_structs` is 1 only when every member of every struct of a struct or struct array property
 * is an object, string, int, float or bool; then `structs` points at `struct_count` radicle_script_struct
 * values (1 for a struct, n for a struct array). Otherwise `has_structs` and `struct_count` are 0
 * and `structs` is NULL.
 *
 * `script`, `name`, `text`, `elements` and `structs`, with each element's and member's `object`
 * plugin and `text` and each member's `name`, are owned by the caller and released with
 * radicle_free_quest_properties.
 */
typedef struct radicle_quest_property {
  radicle_quest_property_owner owner;
  int32_t alias;
  const char* script;
  const char* name;
  uint8_t type;
  uint8_t flags;
  uint8_t has_integer;
  int32_t integer;
  uint8_t has_real;
  float real;
  uint8_t has_boolean;
  uint8_t boolean;
  const char* text;
  size_t text_size;
  uint8_t has_object;
  uint32_t object_form_id;
  uint8_t has_property_alias;
  int16_t property_alias;
  uint8_t has_element_count;
  uint32_t element_count;
  uint8_t has_elements;
  const radicle_script_element* elements;
  size_t element_value_count;
  uint8_t has_structs;
  const radicle_script_struct* structs;
  size_t struct_count;
} radicle_quest_property;

/*
 * Every property of one quest's VMAD in file order: the record's own scripts, then the
 * fragment script, then each alias's scripts. A quest with no properties, a missing quest and
 * a form key that is not a QUST all return OK with a NULL array and a zero count;
 * radicle_quest_runtime_rows reports which of those happened.
 */
RADICLE_API radicle_status radicle_quest_runtime_properties(const radicle_plugin* plugin,
                                                            const char* form_key,
                                                            radicle_quest_property** out_properties,
                                                            size_t* out_count);
RADICLE_API void radicle_free_quest_properties(radicle_quest_property* properties, size_t count);

typedef struct radicle_quest_structure radicle_quest_structure;

typedef enum radicle_quest_scope {
  RADICLE_QUEST_SCOPE_DIALOGUE = 0,
  RADICLE_QUEST_SCOPE_STORY_MANAGER = 1,
  RADICLE_QUEST_SCOPE_LOG_ENTRY = 2,
  RADICLE_QUEST_SCOPE_OBJECTIVE_TARGET = 3,
  RADICLE_QUEST_SCOPE_ALIAS = 4,
  RADICLE_QUEST_SCOPE_STAGES = 5,
  RADICLE_QUEST_SCOPE_OBJECTIVES = 6,
  RADICLE_QUEST_SCOPE_ALIASES = 7
} radicle_quest_scope;

typedef enum radicle_quest_alias_kind {
  RADICLE_QUEST_ALIAS_REFERENCE = 0,
  RADICLE_QUEST_ALIAS_LOCATION = 1,
  RADICLE_QUEST_ALIAS_COLLECTION = 2
} radicle_quest_alias_kind;

typedef enum radicle_quest_fill_type {
  RADICLE_QUEST_FILL_NONE = 0,
  RADICLE_QUEST_FILL_SPECIFIC_REFERENCE = 1,
  RADICLE_QUEST_FILL_UNIQUE_ACTOR = 2,
  RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE = 3,
  RADICLE_QUEST_FILL_EXTERNAL_ALIAS_REFERENCE = 4,
  RADICLE_QUEST_FILL_CREATE_REFERENCE = 5,
  RADICLE_QUEST_FILL_MATCHING_NEAR_ALIAS = 6,
  RADICLE_QUEST_FILL_MATCHING_REFERENCE_EVENT = 7,
  RADICLE_QUEST_FILL_MATCHING_CLOSEST = 8,
  RADICLE_QUEST_FILL_SPECIFIC_LOCATION = 9,
  RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION = 10,
  RADICLE_QUEST_FILL_EXTERNAL_ALIAS_LOCATION = 11,
  RADICLE_QUEST_FILL_MATCHING_LOCATION_EVENT = 12
} radicle_quest_fill_type;

typedef struct radicle_quest_text {
  radicle_name_kind kind;
  uint32_t string_id;
  const uint8_t* bytes;
  size_t size;
} radicle_quest_text;

typedef struct radicle_quest_stage {
  uint16_t index;
  uint8_t flags;
  uint8_t opaque_byte;
  size_t first_log;
  size_t log_count;
} radicle_quest_stage;

typedef struct radicle_quest_log {
  uint8_t flags;
  radicle_quest_text text;
  const uint8_t* note;
  size_t note_size;
} radicle_quest_log;

typedef struct radicle_quest_objective {
  uint16_t index;
  uint32_t flags;
  radicle_quest_text text;
  size_t first_target;
  size_t target_count;
} radicle_quest_objective;

typedef struct radicle_quest_target {
  int32_t alias;
  uint32_t flags;
  radicle_form_key keyword;
} radicle_quest_target;

typedef struct radicle_quest_alias {
  radicle_quest_alias_kind kind;
  uint32_t id;
  const uint8_t* name;
  size_t name_size;
  uint8_t has_flags;
  uint32_t flags;
  uint8_t has_force_into_alias;
  int32_t force_into_alias;
  uint8_t has_closest_to_alias;
  int32_t closest_to_alias;
  uint8_t fill_supported;
  radicle_quest_fill_type fill_type;
  radicle_form_key target;
  radicle_form_key secondary_target;
  uint8_t has_fill_alias;
  int32_t fill_alias;
  uint16_t create_mode;
  uint8_t has_level;
  uint32_t level;
  uint32_t near_type;
  uint32_t event;
  uint32_t event_data;
  size_t first_collection_entry;
  size_t collection_entry_count;
} radicle_quest_alias;

typedef struct radicle_quest_collection_entry {
  uint32_t id;
  uint8_t has_max_initial_fill_count;
  uint8_t max_initial_fill_count;
} radicle_quest_collection_entry;

typedef struct radicle_quest_condition {
  uint8_t flags_byte;
  uint32_t comparison_raw;
  uint16_t function;
  uint16_t unknown2;
  int32_t param1;
  int32_t param2;
  uint32_t run_on;
  uint32_t reference;
  int32_t unknown3;
  const uint8_t* parameter1_string;
  size_t parameter1_size;
  const uint8_t* parameter2_string;
  size_t parameter2_size;
} radicle_quest_condition;

typedef struct radicle_quest_condition_list {
  radicle_quest_scope scope;
  size_t primary;
  size_t secondary;
  uint8_t supported;
  size_t first_condition;
  size_t condition_count;
} radicle_quest_condition_list;

typedef struct radicle_quest_unsupported {
  radicle_quest_scope scope;
  size_t primary;
  size_t secondary;
  size_t member;
  char signature[5];
  const char* reason;
} radicle_quest_unsupported;

typedef struct radicle_quest_structure_view {
  const char* source_plugin;
  uint8_t stages_supported;
  uint8_t objectives_supported;
  uint8_t aliases_supported;
  const radicle_quest_stage* stages;
  size_t stage_count;
  const radicle_quest_log* logs;
  size_t log_count;
  const radicle_quest_objective* objectives;
  size_t objective_count;
  const radicle_quest_target* targets;
  size_t target_count;
  const radicle_quest_alias* aliases;
  size_t alias_count;
  const radicle_quest_collection_entry* collection_entries;
  size_t collection_entry_count;
  const radicle_quest_condition_list* condition_lists;
  size_t condition_list_count;
  const radicle_quest_condition* conditions;
  size_t condition_count;
  const radicle_quest_unsupported* unsupported;
  size_t unsupported_count;
} radicle_quest_structure_view;

RADICLE_API radicle_status radicle_quest_structure_read(const radicle_plugin* plugin,
                                                        const char* form_key,
                                                        radicle_quest_structure** out_structure);
RADICLE_API radicle_status radicle_quest_structure_get_view(const radicle_quest_structure* structure,
                                                            radicle_quest_structure_view* out_view);
RADICLE_API void radicle_quest_structure_destroy(radicle_quest_structure* structure);

#ifdef __cplusplus
}
#endif
