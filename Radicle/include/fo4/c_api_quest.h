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
  size_t text_size;
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
