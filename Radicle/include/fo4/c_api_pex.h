#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The .pex C ABI.
 *
 * Handles are owned by the caller and released with radicle_pex_destroy;
 * destroying NULL is a no-op. Every byte range this API hands back is a
 * length-delimited copy taken verbatim from the container: the format stores
 * one byte per character and this layer never transcodes. Latin-1 is the
 * reading the C++ and C# layers apply to those bytes; any other character
 * encoding is UNPROVEN. Buffers returned through out_bytes are owned by the
 * caller and released with radicle_free_bytes from fo4/c_api.h.
 *
 * A radicle_pex handle is not thread-safe. Different handles may be used from
 * different threads. radicle_last_error is shared with the rest of the C ABI.
 */

typedef struct radicle_pex radicle_pex;

typedef struct radicle_pex_header {
  uint8_t major_version;
  uint8_t minor_version;
  uint16_t game_id;
  int64_t compilation_time;
  size_t string_count;
  size_t object_count;
} radicle_pex_header;

typedef struct radicle_pex_object_counts {
  size_t struct_count;
  size_t variable_count;
  size_t property_count;
  size_t state_count;
  size_t function_count;
} radicle_pex_object_counts;

/*
 * Parses `bytes`. The handle keeps everything it needs, so the caller may
 * release the input as soon as the call returns. Malformed input returns
 * RADICLE_STATUS_PARSE_ERROR with the reader's message in radicle_last_error.
 */
RADICLE_API radicle_status radicle_pex_parse(const uint8_t* bytes,
                                              size_t size,
                                              radicle_pex** out_pex);

RADICLE_API radicle_status radicle_pex_read_file(const char* path,
                                                  radicle_pex** out_pex);

RADICLE_API void radicle_pex_destroy(radicle_pex* pex);

/*
 * Serializes the handle. As with radicle_plugin_write_bytes, a handle with no
 * bytes returns OK with *out_bytes == NULL and *out_size == 0.
 */
RADICLE_API radicle_status radicle_pex_write_bytes(const radicle_pex* pex,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

RADICLE_API radicle_status radicle_pex_get_header(const radicle_pex* pex,
                                                   radicle_pex_header* out_header);

RADICLE_API radicle_status radicle_pex_object_get_counts(const radicle_pex* pex,
                                                          size_t index,
                                                          radicle_pex_object_counts* out_counts);

RADICLE_API radicle_status radicle_pex_object_name(const radicle_pex* pex,
                                                    size_t index,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

RADICLE_API radicle_status radicle_pex_object_parent_name(const radicle_pex* pex,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_pex_string_table_entry(const radicle_pex* pex,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

/*
 * One object's code: its states, functions, typed names, instructions and values, flattened
 * into arrays the caller walks by index.
 *
 * Every name, type and string value is a string-table index (radicle_pex_string_table_entry),
 * so this layer hands back no string bytes of its own. RADICLE_PEX_NO_STRING marks an empty
 * name the table does not hold. Ranges are (first, count) pairs into the arrays of the same
 * radicle_pex_object_code. Instruction arguments are the decoded operands in file order with
 * the variable-argument count removed: a call opcode's fixed operands come first and its
 * variable arguments follow. What any operand means is the caller's business; this layer
 * names no operand roles.
 *
 * Functions are listed state by state, then each property's getter and setter. A function's
 * `state` is its state index for a state function and RADICLE_PEX_NO_INDEX for a property
 * handler; a property's `read_function` and `write_function` are function indexes or
 * RADICLE_PEX_NO_INDEX. The arrays are owned by the caller and released with
 * radicle_pex_free_object_code; releasing a zeroed struct is a no-op.
 */
#define RADICLE_PEX_NO_STRING ((size_t)-1)
#define RADICLE_PEX_NO_INDEX ((size_t)-1)

typedef struct radicle_pex_value {
  uint8_t type;
  uint8_t boolean;
  int32_t integer;
  float real;
  size_t string;
} radicle_pex_value;

typedef struct radicle_pex_typed_name {
  size_t name;
  size_t type;
} radicle_pex_typed_name;

typedef struct radicle_pex_instruction {
  uint8_t opcode;
  int32_t line;
  size_t first_argument;
  size_t argument_count;
} radicle_pex_instruction;

typedef struct radicle_pex_function {
  size_t state;
  size_t name;
  size_t return_type;
  uint32_t user_flags;
  uint8_t is_global;
  uint8_t is_native;
  size_t first_parameter;
  size_t parameter_count;
  size_t first_local;
  size_t local_count;
  size_t first_instruction;
  size_t instruction_count;
} radicle_pex_function;

typedef struct radicle_pex_state {
  size_t name;
  size_t first_function;
  size_t function_count;
} radicle_pex_state;

typedef struct radicle_pex_variable {
  size_t name;
  size_t type;
  uint32_t user_flags;
  uint8_t is_const;
  uint8_t has_default;
  radicle_pex_value default_value;
} radicle_pex_variable;

typedef struct radicle_pex_property {
  size_t name;
  size_t type;
  uint32_t user_flags;
  uint8_t flags;
  size_t auto_var_name;
  size_t read_function;
  size_t write_function;
} radicle_pex_property;

typedef struct radicle_pex_struct {
  size_t name;
  size_t first_member;
  size_t member_count;
} radicle_pex_struct;

typedef struct radicle_pex_object_code {
  size_t auto_state_name;
  radicle_pex_state* states;
  size_t state_count;
  radicle_pex_function* functions;
  size_t function_count;
  radicle_pex_typed_name* typed_names;
  size_t typed_name_count;
  radicle_pex_instruction* instructions;
  size_t instruction_count;
  radicle_pex_value* values;
  size_t value_count;
  radicle_pex_variable* variables;
  size_t variable_count;
  radicle_pex_property* properties;
  size_t property_count;
  radicle_pex_struct* structs;
  size_t struct_count;
  radicle_pex_variable* struct_members;
  size_t struct_member_count;
} radicle_pex_object_code;

RADICLE_API radicle_status radicle_pex_object_code_get(const radicle_pex* pex,
                                                       size_t index,
                                                       radicle_pex_object_code* out_code);

RADICLE_API void radicle_pex_free_object_code(radicle_pex_object_code* code);

#ifdef __cplusplus
}
#endif
