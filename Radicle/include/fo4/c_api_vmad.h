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
RADICLE_API radicle_status radicle_plugin_set_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint16_t stage, int32_t stage_index, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_remove_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint16_t stage, int32_t stage_index);
/* Constraint: a binding requires a QUST VMAD that already has a fragment section with a named fragment script (NOT_FOUND otherwise); a (stage, stage_index) pair that is already bound is INVALID_ARGUMENT and leaves the bytes untouched. */
RADICLE_API radicle_status radicle_plugin_add_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint16_t stage, int32_t stage_index, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_add_vmad(radicle_plugin* plugin, radicle_form_key key);
RADICLE_API radicle_status radicle_plugin_add_vmad_script(radicle_plugin* plugin, radicle_form_key key, size_t index, const char* script_name, uint8_t flags, size_t* out_script_index);
RADICLE_API radicle_status radicle_plugin_remove_vmad_script(radicle_plugin* plugin, radicle_form_key key, size_t script_index);
RADICLE_API radicle_status radicle_plugin_add_vmad_property(radicle_plugin* plugin, radicle_form_key key, size_t script_index, size_t index, const char* name, uint8_t type, uint8_t flags, size_t* out_property_index);
RADICLE_API radicle_status radicle_plugin_remove_vmad_property(radicle_plugin* plugin, radicle_form_key key, size_t script_index, size_t property_index);
RADICLE_API radicle_status radicle_plugin_add_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t fragment_key, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_remove_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t fragment_key);
/* Constraint: a SCEN record keeps phase fragments in a second list, so this appends the empty fragment section once and names its fragment script. */
RADICLE_API radicle_status radicle_plugin_bootstrap_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, const char* script);
/* Constraint: a set creates an unbound (phase index, phase flag) pair and renames a bound one, so a fresh scene can gain its first phase fragment. */
RADICLE_API radicle_status radicle_plugin_set_scene_vmad_phase_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t phase_index, uint8_t phase_flag, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_remove_scene_vmad_phase_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t phase_index, uint8_t phase_flag);

// The VMAD read ABI must decode the script list of any record, arrays and structs included.
// Invariant: every string returned here stays valid until radicle_vmad_scripts_destroy.
// Invariant: a handle is not thread-safe; different handles may be used from different threads.
// Invariant: unsupported and trailing_bytes describe the whole read, not one script: every script of the handle reports the same values.
// Invariant: script unsupported is set when a property could not be decoded; a struct or struct array whose members are all decoded, nested arrays and structs included, leaves it clear.
// Invariant: a struct member that is itself a struct, array or struct array is listed as a row with kind RADICLE_VMAD_NONE and its own type, and radicle_vmad_scripts_element_node reaches its children.
// Invariant: a property the decoder cannot read ends the read at that property, so the scripts after it are dropped,
// Invariant: radicle_vmad_script_count is then below the count the file declares and trailing_bytes stays 0, while the scripts decoded before it report unsupported as well.
// Invariant: for a struct array (type 17) declared_element_count is the number of structs, while element_total and the
// rows count members, so a struct with zero members must produce no rows.
// Invariant: for a struct property (type 7) declared_element_count is the member count.
// Invariant: RADICLE_VMAD_ALIAS is reserved, because the reader reports an object reference as RADICLE_VMAD_OBJECT.
// Invariant: names are UTF-8, converted from the Windows-1252 bytes the file stores; string values are stored as UTF-8 and returned unchanged.
// Invariant: names and text cross the ABI as NUL-terminated strings with no length, so an embedded NUL truncates them.

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
// Invariant: the plugin owns the record; a record without a VMAD subrecord is RADICLE_STATUS_NOT_FOUND.
RADICLE_API radicle_status radicle_plugin_vmad_scripts(radicle_plugin* plugin,
                                                       radicle_form_key key,
                                                       radicle_vmad_scripts** out_scripts);
RADICLE_API void radicle_vmad_scripts_destroy(radicle_vmad_scripts* scripts);
RADICLE_API size_t radicle_vmad_script_count(const radicle_vmad_scripts* scripts);
RADICLE_API radicle_status radicle_vmad_scripts_script(const radicle_vmad_scripts* scripts,
                                                       size_t index,
                                                       radicle_vmad_script* out_script);
// Invariant: script_index and property_index must index what radicle_vmad_scripts_script reported.
RADICLE_API radicle_status radicle_vmad_scripts_property(const radicle_vmad_scripts* scripts,
                                                         size_t script_index,
                                                         size_t property_index,
                                                         radicle_vmad_property* out_property);
RADICLE_API size_t radicle_vmad_element_count(const radicle_vmad_scripts* scripts,
                                              size_t script_index,
                                              size_t property_index);
RADICLE_API radicle_status radicle_vmad_scripts_element(const radicle_vmad_scripts* scripts,
                                                        size_t script_index,
                                                        size_t property_index,
                                                        size_t element_index,
                                                        radicle_vmad_element* out_element);
// Invariant: a node is the child list of one container row; a struct row (type 7) lists its members, an array row (types 11 through 15) lists its elements with type 1 through 5 and no member name, and a struct array row (type 17) lists one type 7 row per struct, so an empty struct is a node with no children.
// Invariant: a child row uses the radicle_vmad_element fields with struct_index 0 and index_in_property as its position in the node; a child that is itself a container has a node of its own.
// Invariant: an element that is not a container has no node and radicle_vmad_scripts_element_node reports NOT_FOUND; a node id is valid only for the handle that returned it.
RADICLE_API radicle_status radicle_vmad_scripts_element_node(const radicle_vmad_scripts* scripts,
                                                             size_t script_index,
                                                             size_t property_index,
                                                             size_t element_index,
                                                             size_t* out_node);
RADICLE_API size_t radicle_vmad_node_child_count(const radicle_vmad_scripts* scripts, size_t node);
RADICLE_API radicle_status radicle_vmad_node_child(const radicle_vmad_scripts* scripts,
                                                   size_t node,
                                                   size_t child_index,
                                                   radicle_vmad_element* out_child);
RADICLE_API radicle_status radicle_vmad_node_child_node(const radicle_vmad_scripts* scripts,
                                                        size_t node,
                                                        size_t child_index,
                                                        size_t* out_node);
// Invariant: a read that ended at a property of unknown size reports the script it ended in, that top-level property's name and type (never the nested member that failed) and the script count the file declares; a complete read is NOT_FOUND.
// Invariant: the scripts before out_script_index are complete, the script at out_script_index lists only the properties before the named one, and the scripts after it are absent.
RADICLE_API radicle_status radicle_vmad_scripts_stop(const radicle_vmad_scripts* scripts,
                                                     size_t* out_script_index,
                                                     const char** out_property_name,
                                                     uint8_t* out_property_type,
                                                     size_t* out_declared_scripts);
// Invariant: the handle holds the one fragment script entry of an INFO whose fragment section is present, with its properties; any other state is NOT_FOUND.
RADICLE_API radicle_status radicle_info_fragment_script(const radicle_env* env,
                                                        const radicle_record* record,
                                                        radicle_vmad_scripts** out_scripts);
// Invariant: the handle holds the one fragment script entry of a TERM whose fragment section is present, with its properties; any other state is NOT_FOUND.
RADICLE_API radicle_status radicle_term_fragment_script(const radicle_env* env,
                                                        const radicle_record* record,
                                                        radicle_vmad_scripts** out_scripts);

typedef struct radicle_vmad_phase_fragment {
  uint32_t phase_index;
  uint8_t phase_flag;
  uint8_t unknown;
  const char* script;
  const char* function;
} radicle_vmad_phase_fragment;

typedef struct radicle_vmad_phase_fragments radicle_vmad_phase_fragments;

// Constraint: a record whose VMAD carries no fragment section reports NOT_FOUND, so a caller must bootstrap one before reading.
RADICLE_API radicle_status radicle_plugin_vmad_phase_fragments(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              radicle_vmad_phase_fragments** out_fragments);
RADICLE_API void radicle_free_vmad_phase_fragments(radicle_vmad_phase_fragments* fragments);
RADICLE_API size_t radicle_vmad_phase_fragment_count(const radicle_vmad_phase_fragments* fragments);
// Invariant: every string returned here stays valid until radicle_free_vmad_phase_fragments.
RADICLE_API radicle_status radicle_vmad_phase_fragments_at(const radicle_vmad_phase_fragments* fragments,
                                                           size_t index,
                                                           radicle_vmad_phase_fragment* out_fragment);

#ifdef __cplusplus
}
#endif
