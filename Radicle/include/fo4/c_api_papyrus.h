#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_papyrus_value radicle_papyrus_value;
typedef struct radicle_papyrus_object radicle_papyrus_object;

typedef enum radicle_papyrus_value_kind {
  RADICLE_PAPYRUS_VALUE_NONE = 0,
  RADICLE_PAPYRUS_VALUE_STRING = 1,
  RADICLE_PAPYRUS_VALUE_INT = 2,
  RADICLE_PAPYRUS_VALUE_FLOAT = 3,
  RADICLE_PAPYRUS_VALUE_BOOL = 4,
  RADICLE_PAPYRUS_VALUE_OBJECT = 5,
  RADICLE_PAPYRUS_VALUE_ARRAY = 6,
  RADICLE_PAPYRUS_VALUE_STRUCT = 7
} radicle_papyrus_value_kind;

/* Invariant: this enum mirrors fo4::papyrus::Status one for one. */
typedef enum radicle_papyrus_status {
  RADICLE_PAPYRUS_STATUS_OK = 0,
  RADICLE_PAPYRUS_STATUS_UNKNOWN_OPCODE = 1,
  RADICLE_PAPYRUS_STATUS_UNSUPPORTED_OPCODE = 2,
  RADICLE_PAPYRUS_STATUS_UNKNOWN_IDENTIFIER = 3,
  RADICLE_PAPYRUS_STATUS_TYPE_MISMATCH = 4,
  RADICLE_PAPYRUS_STATUS_DIVISION_BY_ZERO = 5,
  RADICLE_PAPYRUS_STATUS_INDEX_OUT_OF_RANGE = 6,
  RADICLE_PAPYRUS_STATUS_INVALID_COUNT = 7,
  RADICLE_PAPYRUS_STATUS_ARRAY_TOO_LONG = 8,
  RADICLE_PAPYRUS_STATUS_NULL_ARRAY = 9,
  RADICLE_PAPYRUS_STATUS_NULL_OBJECT = 10,
  RADICLE_PAPYRUS_STATUS_BAD_JUMP_TARGET = 11,
  RADICLE_PAPYRUS_STATUS_STACK_UNDERFLOW = 12,
  RADICLE_PAPYRUS_STATUS_CALL_DEPTH_EXCEEDED = 13,
  RADICLE_PAPYRUS_STATUS_INSTRUCTION_BUDGET_EXCEEDED = 14,
  RADICLE_PAPYRUS_STATUS_FUNCTION_NOT_FOUND = 15,
  RADICLE_PAPYRUS_STATUS_ARGUMENT_COUNT_MISMATCH = 16,
  RADICLE_PAPYRUS_STATUS_HOST_ERROR = 17,
  RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR = 18,
  RADICLE_PAPYRUS_STATUS_STRING_TOO_LONG = 19,
  RADICLE_PAPYRUS_STATUS_GATE_REFUSED = 20,
  RADICLE_PAPYRUS_STATUS_PAUSED = 21,
  RADICLE_PAPYRUS_STATUS_LATENT = 22
} radicle_papyrus_status;

RADICLE_API const char* radicle_papyrus_status_name(radicle_papyrus_status status);

/* Invariant: object strings stay valid for the callback only. */
RADICLE_API uint64_t radicle_papyrus_object_handle(const radicle_papyrus_object* object);
RADICLE_API const char* radicle_papyrus_object_type(const radicle_papyrus_object* object);
RADICLE_API const char* radicle_papyrus_object_state(const radicle_papyrus_object* object);

/* Invariant: a value handle is owned by its creator and released with radicle_papyrus_value_destroy. */
RADICLE_API void radicle_papyrus_value_destroy(radicle_papyrus_value* value);
RADICLE_API radicle_papyrus_value_kind radicle_papyrus_value_kind_of(const radicle_papyrus_value* value);
RADICLE_API radicle_status radicle_papyrus_value_none(radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_string(const char* text, size_t size, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_int(int32_t number, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_float(float number, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_bool(int32_t flag, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_object(uint64_t handle, const char* type, const char* state,
                                                        radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_array(size_t count, radicle_papyrus_value** out_value);
RADICLE_API size_t radicle_papyrus_value_array_count(const radicle_papyrus_value* value);
RADICLE_API radicle_status radicle_papyrus_value_array_set(radicle_papyrus_value* value, size_t index,
                                                           const radicle_papyrus_value* element);
RADICLE_API radicle_status radicle_papyrus_value_array_get(const radicle_papyrus_value* value, size_t index,
                                                           radicle_papyrus_value** out_element);
RADICLE_API radicle_status radicle_papyrus_value_struct(const char* type_name, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_value_struct_set(radicle_papyrus_value* value, const char* member,
                                                            const radicle_papyrus_value* member_value);
RADICLE_API radicle_status radicle_papyrus_value_struct_get(const radicle_papyrus_value* value, const char* member,
                                                            radicle_papyrus_value** out_member);
RADICLE_API int32_t radicle_papyrus_value_int_of(const radicle_papyrus_value* value);
RADICLE_API float radicle_papyrus_value_float_of(const radicle_papyrus_value* value);
RADICLE_API int32_t radicle_papyrus_value_bool_of(const radicle_papyrus_value* value);
/* Invariant: text is the string payload for a string value and NULL for every other kind. */
RADICLE_API const char* radicle_papyrus_value_text(const radicle_papyrus_value* value);
RADICLE_API uint64_t radicle_papyrus_value_object_handle(const radicle_papyrus_value* value);
RADICLE_API const char* radicle_papyrus_value_object_type(const radicle_papyrus_value* value);
RADICLE_API const char* radicle_papyrus_value_object_state(const radicle_papyrus_value* value);

#ifdef __cplusplus
}
#endif
