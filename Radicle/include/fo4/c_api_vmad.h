#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_plugin_set_vmad_object(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, radicle_form_key target, int16_t alias);
RADICLE_API radicle_status radicle_plugin_set_vmad_string(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, const char* value);
RADICLE_API radicle_status radicle_plugin_set_vmad_int(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, int32_t value);
RADICLE_API radicle_status radicle_plugin_set_vmad_float(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, float value);
RADICLE_API radicle_status radicle_plugin_set_vmad_bool(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, int32_t value);

// The VMAD read ABI must decode the script list of any record, arrays and structs included.
// Invariant: every string returned here stays valid until radicle_vmad_scripts_destroy.
// A handle is not thread-safe; different handles may be used from different threads.

typedef struct radicle_vmad_scripts radicle_vmad_scripts;

typedef enum radicle_vmad_value_kind {
  RADICLE_VMAD_NONE = 0,
  RADICLE_VMAD_OBJECT = 1,
  RADICLE_VMAD_ALIAS = 2,
  RADICLE_VMAD_TEXT = 3,
  RADICLE_VMAD_INT = 4,
  RADICLE_VMAD_REAL = 5,
  RADICLE_VMAD_BOOL = 6,
  RADICLE_VMAD_STRUCT_MEMBER = 7
} radicle_vmad_value_kind;

typedef struct radicle_vmad_script {
  const char* name;
  uint8_t flags;
  size_t property_count;
  size_t trailing_bytes;
  int32_t unsupported;
} radicle_vmad_script;

typedef struct radicle_vmad_property {
  const char* name;
  uint8_t type;
  uint8_t flags;
  radicle_vmad_value_kind value_kind;
  uint32_t object_form_id;
  int16_t alias;
  const char* text;
  int32_t integer;
  float real;
  int32_t boolean;
  uint32_t declared_element_count;
  radicle_vmad_value_kind element_kind;
  size_t element_total;
} radicle_vmad_property;

typedef struct radicle_vmad_element {
  radicle_vmad_value_kind kind;
  const char* member_name;
  size_t struct_index;
  size_t index_in_property;
  uint8_t type;
  uint8_t flags;
  uint32_t object_form_id;
  int16_t alias;
  const char* text;
  int32_t integer;
  float real;
  int32_t boolean;
} radicle_vmad_element;

// Invariant: env must own record; a record without a VMAD subrecord is RADICLE_STATUS_NOT_FOUND.
RADICLE_API radicle_status radicle_record_vmad_scripts(const radicle_env* env,
                                                       const radicle_record* record,
                                                       radicle_vmad_scripts** out_scripts);
RADICLE_API void radicle_vmad_scripts_destroy(radicle_vmad_scripts* scripts);
RADICLE_API size_t radicle_vmad_script_count(const radicle_vmad_scripts* scripts);
RADICLE_API radicle_status radicle_vmad_script(const radicle_vmad_scripts* scripts,
                                               size_t index,
                                               radicle_vmad_script* out_script);
// Invariant: script_index and property_index must index what radicle_vmad_script reported.
RADICLE_API radicle_status radicle_vmad_property(const radicle_vmad_scripts* scripts,
                                                 size_t script_index,
                                                 size_t property_index,
                                                 radicle_vmad_property* out_property);
RADICLE_API size_t radicle_vmad_element_count(const radicle_vmad_scripts* scripts,
                                              size_t script_index,
                                              size_t property_index);
RADICLE_API radicle_status radicle_vmad_element(const radicle_vmad_scripts* scripts,
                                                size_t script_index,
                                                size_t property_index,
                                                size_t element_index,
                                                radicle_vmad_element* out_element);

#ifdef __cplusplus
}
#endif
