#pragma once

#include "fo4/c_api.h"

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
 * `fragment-script` row names, so its own script name is empty.
 */
typedef enum radicle_quest_property_owner {
  RADICLE_QUEST_PROPERTY_SCRIPT = 0,
  RADICLE_QUEST_PROPERTY_FRAGMENT = 1,
  RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT = 2
} radicle_quest_property_owner;

/*
 * One VMAD property with its decoded value.
 *
 * Every value has a `has_` companion: a VMAD property carries at most one value of its type,
 * and an object property carries a FormID and an alias together. `text` is NULL when the
 * property holds no string, and an empty string is a value. `element_count` is the leading
 * count of a struct or array property. `alias` is the owning alias for
 * RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT and -1 otherwise, while `property_alias` is the alias
 * the property itself names.
 *
 * `script`, `name` and `text` are owned by the caller and released with
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
  uint8_t has_object;
  uint32_t object_form_id;
  uint8_t has_property_alias;
  int16_t property_alias;
  uint8_t has_element_count;
  uint32_t element_count;
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

#ifdef __cplusplus
}
#endif
