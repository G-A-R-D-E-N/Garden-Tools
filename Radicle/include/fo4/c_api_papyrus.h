#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a runtime borrows the pex handles added to it and is not thread-safe; different handles may run on different threads. */

typedef struct radicle_papyrus radicle_papyrus;
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

/* Requires: a callback returns 1 when it handled the request, 0 to fall back, and any returned value handle is freed by the runtime. */
/* Invariant: a NULL callback means not handled; a NULL gate allows every semantic key. */
typedef struct radicle_papyrus_callbacks {
  void* user;
  int32_t (*gate)(void* user, const char* key);
  int32_t (*get_variable)(void* user, const radicle_papyrus_object* object, const char* name, radicle_papyrus_value** out_value);
  int32_t (*set_variable)(void* user, const radicle_papyrus_object* object, const char* name, const radicle_papyrus_value* value);
  int32_t (*get_property)(void* user, const radicle_papyrus_object* object, const char* name, radicle_papyrus_value** out_value);
  int32_t (*set_property)(void* user, const radicle_papyrus_object* object, const char* name, const radicle_papyrus_value* value);
  int32_t (*call_native)(void* user, const char* script, const char* function, const radicle_papyrus_value* const* args, size_t arg_count, radicle_papyrus_value** out_value);
} radicle_papyrus_callbacks;

RADICLE_API radicle_status radicle_papyrus_create(const radicle_papyrus_callbacks* callbacks, radicle_papyrus** out_runtime);
RADICLE_API void radicle_papyrus_destroy(radicle_papyrus* runtime);
RADICLE_API radicle_status radicle_papyrus_add_pex(radicle_papyrus* runtime, const radicle_pex* pex);
RADICLE_API size_t radicle_papyrus_pex_count(const radicle_papyrus* runtime);
/* Invariant: every out-parameter is cleared before the argument checks that reject a call; an error run leaves out_result NULL. */
RADICLE_API radicle_status radicle_papyrus_run(radicle_papyrus* runtime, const char* script, const char* function,
                                               const radicle_papyrus_value* const* args, size_t arg_count,
                                               radicle_papyrus_value** out_result,
                                               radicle_papyrus_status* out_status,
                                               size_t* out_instructions);
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
