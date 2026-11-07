#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_papyrus.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: the runtime borrows its pex handles and callbacks; both must outlive it. */

typedef struct radicle_papyrus_runtime radicle_papyrus_runtime;

typedef enum radicle_papyrus_lifecycle {
  RADICLE_PAPYRUS_LIFECYCLE_NOT_INITIALIZED = 0,
  RADICLE_PAPYRUS_LIFECYCLE_INITIALIZING = 1,
  RADICLE_PAPYRUS_LIFECYCLE_READY = 2,
  RADICLE_PAPYRUS_LIFECYCLE_FAILED = 3
} radicle_papyrus_lifecycle;

typedef enum radicle_papyrus_owner_kind {
  RADICLE_PAPYRUS_OWNER_FORM = 0,
  RADICLE_PAPYRUS_OWNER_PLACED_REFERENCE = 1,
  RADICLE_PAPYRUS_OWNER_DYNAMIC_REFERENCE = 2,
  RADICLE_PAPYRUS_OWNER_QUEST_ALIAS = 3,
  RADICLE_PAPYRUS_OWNER_RUNTIME_INSTANCE = 4
} radicle_papyrus_owner_kind;

typedef enum radicle_papyrus_clock {
  RADICLE_PAPYRUS_CLOCK_REAL_TIME = 0,
  RADICLE_PAPYRUS_CLOCK_GAME_TIME = 1
} radicle_papyrus_clock;

typedef enum radicle_papyrus_native_kind {
  RADICLE_PAPYRUS_NATIVE_STATIC = 0,
  RADICLE_PAPYRUS_NATIVE_SCRIPT_INSTANCE = 1,
  RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT = 2
} radicle_papyrus_native_kind;

#define RADICLE_PAPYRUS_ENGINE_HANDLE_BIT 0x8000000000000000ull

typedef struct radicle_papyrus_native_call radicle_papyrus_native_call;

typedef enum radicle_papyrus_native_flags {
  RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT = 1,
  RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE = 2
} radicle_papyrus_native_flags;

typedef enum radicle_papyrus_override_outcome {
  RADICLE_PAPYRUS_OVERRIDE_NOT_REGISTERED = 0,
  RADICLE_PAPYRUS_OVERRIDE_DECLINED = 1,
  RADICLE_PAPYRUS_OVERRIDE_HANDLED = 2,
  RADICLE_PAPYRUS_OVERRIDE_REFUSED = 3
} radicle_papyrus_override_outcome;

typedef struct radicle_papyrus_step_result {
  int32_t spent;
  int32_t completed;
  int32_t parked;
  int32_t paused;
  int32_t refused;
  int32_t dropped;
  int32_t waiting;
} radicle_papyrus_step_result;

typedef enum radicle_papyrus_call_state {
  RADICLE_PAPYRUS_CALL_PENDING = 0,
  RADICLE_PAPYRUS_CALL_WAITING = 1,
  RADICLE_PAPYRUS_CALL_DONE = 2,
  RADICLE_PAPYRUS_CALL_FAILED = 3,
  RADICLE_PAPYRUS_CALL_DROPPED = 4
} radicle_papyrus_call_state;

/* Requires: struct_size is sizeof(radicle_papyrus_call_info) before the call; status is the failure status and instructions the cumulative count. */
typedef struct radicle_papyrus_call_info {
  size_t struct_size;
  radicle_papyrus_call_state state;
  radicle_papyrus_status status;
  uint64_t instructions;
} radicle_papyrus_call_info;

typedef enum radicle_papyrus_oninit_policy {
  RADICLE_PAPYRUS_ONINIT_REFUSE = 0,
  RADICLE_PAPYRUS_ONINIT_QUEUE = 1
} radicle_papyrus_oninit_policy;

/* Requires: struct_size is sizeof(radicle_papyrus_runtime_stats) before the call; ready and waiting are the current queue sizes and every other field is a cumulative counter. */
typedef struct radicle_papyrus_runtime_stats {
  size_t struct_size;
  int32_t ready;
  int32_t waiting;
  int32_t completed;
  int32_t parked;
  int32_t paused;
  int32_t refused;
  int32_t dropped;
  int32_t waiting_events;
  int32_t refused_completions;
  int32_t queued_during_oninit;
  int32_t native_target_refusals;
  int32_t overrides_handled;
  int32_t overrides_declined;
  int32_t overrides_refused;
} radicle_papyrus_runtime_stats;

/* Requires: a callback returns 1 when it handled the request; an override returns its outcome through out_outcome. */
typedef struct radicle_papyrus_runtime_callbacks {
  void* user;
  int32_t (*gate)(void* user, const char* key);
  int32_t (*call_native)(void* user, const char* script, const char* function, const radicle_papyrus_value* const* args, size_t arg_count, radicle_papyrus_value** out_value);
  int32_t (*override_function)(void* user, uint64_t instance, const char* function, const radicle_papyrus_value* const* args, size_t arg_count, int32_t* out_outcome, radicle_papyrus_value** out_value);
  int64_t (*begin_latent)(void* user, const char* script, const char* function, const radicle_papyrus_value* const* args, size_t arg_count);
} radicle_papyrus_runtime_callbacks;

/* Requires: struct_size is sizeof(radicle_papyrus_engine_callbacks); call_native_ex, get_property and set_property return 1 when handled. */
typedef struct radicle_papyrus_engine_callbacks {
  size_t struct_size;
  void* user;
  int32_t (*call_native_ex)(void* user, radicle_papyrus_native_call* call);
  int32_t (*get_property)(void* user, const radicle_papyrus_object* object, const char* name, radicle_papyrus_value** out_value);
  int32_t (*set_property)(void* user, const radicle_papyrus_object* object, const char* name, const radicle_papyrus_value* value);
} radicle_papyrus_engine_callbacks;

RADICLE_API radicle_status radicle_papyrus_runtime_create(const radicle_papyrus_runtime_callbacks* callbacks, radicle_papyrus_runtime** out_runtime);
RADICLE_API void radicle_papyrus_runtime_destroy(radicle_papyrus_runtime* runtime);
RADICLE_API radicle_status radicle_papyrus_runtime_add_pex(radicle_papyrus_runtime* runtime, const radicle_pex* pex);
RADICLE_API radicle_status radicle_papyrus_runtime_attach(radicle_papyrus_runtime* runtime, const char* script, radicle_papyrus_owner_kind owner_kind, const char* base_form_key, const char* discriminator, uint64_t* out_instance);
RADICLE_API int32_t radicle_papyrus_runtime_detach(radicle_papyrus_runtime* runtime, uint64_t instance);
RADICLE_API radicle_papyrus_lifecycle radicle_papyrus_runtime_lifecycle(const radicle_papyrus_runtime* runtime, uint64_t instance);
RADICLE_API const char* radicle_papyrus_runtime_failure(const radicle_papyrus_runtime* runtime, uint64_t instance);
RADICLE_API const char* radicle_papyrus_runtime_instance_state(const radicle_papyrus_runtime* runtime, uint64_t instance);
RADICLE_API radicle_status radicle_papyrus_runtime_initialize(radicle_papyrus_runtime* runtime, uint64_t instance, radicle_papyrus_status* out_status);
RADICLE_API radicle_status radicle_papyrus_runtime_send_event(radicle_papyrus_runtime* runtime, uint64_t instance, const char* function, const radicle_papyrus_value* const* args, size_t arg_count, radicle_papyrus_status* out_status);
/* Requires: out_ticket is 0 on a refusal and out_status then carries the refusal; a ticket above zero is read with radicle_papyrus_runtime_call_result. */
RADICLE_API radicle_status radicle_papyrus_runtime_call_function(radicle_papyrus_runtime* runtime, uint64_t instance, const char* function, const radicle_papyrus_value* const* args, size_t arg_count, uint64_t* out_ticket, radicle_papyrus_status* out_status);
/* Requires: out_info.struct_size is set by the caller; out_value may be NULL and is set only when the call is DONE; consume releases a terminal record and leaves a PENDING or WAITING one in place; an unknown, consumed or evicted ticket is NOT_FOUND. */
RADICLE_API radicle_status radicle_papyrus_runtime_call_result(radicle_papyrus_runtime* runtime, uint64_t ticket, int32_t consume, radicle_papyrus_call_info* out_info, radicle_papyrus_value** out_value);
/* Requires: REFUSE refuses an event that arrives during OnInit; QUEUE queues it and drains it once the instance is Ready. */
RADICLE_API radicle_status radicle_papyrus_runtime_set_oninit_policy(radicle_papyrus_runtime* runtime, radicle_papyrus_oninit_policy policy);
/* Requires: parks is at least 1; a slice that parks that many times is dropped. */
RADICLE_API radicle_status radicle_papyrus_runtime_set_max_parks(radicle_papyrus_runtime* runtime, int32_t parks);
RADICLE_API int32_t radicle_papyrus_runtime_has_script(const radicle_papyrus_runtime* runtime, const char* name);
/* Requires: the check covers the instance's own script and its current state only, not the parent chain. */
RADICLE_API int32_t radicle_papyrus_runtime_instance_declares(const radicle_papyrus_runtime* runtime, uint64_t instance, const char* function);
/* Invariant: the string stays valid until the next runtime mutation; NULL means the instance does not exist. */
RADICLE_API const char* radicle_papyrus_runtime_instance_script(const radicle_papyrus_runtime* runtime, uint64_t instance);
/* Requires: out_count is always set to the instance count; out_ids may be NULL, and otherwise up to capacity ids are written in ascending order. */
RADICLE_API radicle_status radicle_papyrus_runtime_list_instances(const radicle_papyrus_runtime* runtime, uint64_t* out_ids, size_t capacity, size_t* out_count);
RADICLE_API radicle_status radicle_papyrus_runtime_get_stats(const radicle_papyrus_runtime* runtime, radicle_papyrus_runtime_stats* out);
RADICLE_API radicle_status radicle_papyrus_runtime_step(radicle_papyrus_runtime* runtime, int32_t budget, radicle_papyrus_step_result* out_result);
RADICLE_API radicle_status radicle_papyrus_runtime_complete_latent(radicle_papyrus_runtime* runtime, uint64_t token, const radicle_papyrus_value* value, int32_t* out_accepted);
RADICLE_API radicle_status radicle_papyrus_runtime_register_native(radicle_papyrus_runtime* runtime, const char* script, const char* function, radicle_papyrus_native_kind kind, const char* required_script, int32_t latent);
RADICLE_API radicle_status radicle_papyrus_runtime_register_override(radicle_papyrus_runtime* runtime, const char* script, const char* function, int32_t parameter_count);
RADICLE_API radicle_status radicle_papyrus_runtime_get_variable(const radicle_papyrus_runtime* runtime, uint64_t instance, const char* name, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_runtime_set_variable(radicle_papyrus_runtime* runtime, uint64_t instance, const char* name, const radicle_papyrus_value* value);
RADICLE_API radicle_status radicle_papyrus_runtime_register_custom_event(radicle_papyrus_runtime* runtime, uint64_t instance, const char* event_name, int64_t* out_handle);
RADICLE_API radicle_status radicle_papyrus_runtime_register_timer(radicle_papyrus_runtime* runtime, uint64_t instance, double interval_seconds, int32_t repeats, radicle_papyrus_clock clock, int64_t* out_handle);
RADICLE_API radicle_status radicle_papyrus_runtime_unregister(radicle_papyrus_runtime* runtime, int64_t handle, int32_t* out_removed);
RADICLE_API radicle_status radicle_papyrus_runtime_advance_clock(radicle_papyrus_runtime* runtime, const char* owner, double delta_seconds, radicle_papyrus_clock clock, int32_t* out_fired);
/* Requires: names and values hold count entries and are copied; one bad entry refuses the call and leaves no instance behind. */
RADICLE_API radicle_status radicle_papyrus_runtime_attach_with_properties(radicle_papyrus_runtime* runtime, const char* script, radicle_papyrus_owner_kind owner_kind, const char* base_form_key, const char* discriminator, const char* const* names, const radicle_papyrus_value* const* values, size_t count, uint64_t* out_instance);
RADICLE_API radicle_status radicle_papyrus_runtime_get_property(const radicle_papyrus_runtime* runtime, uint64_t instance, const char* name, radicle_papyrus_value** out_value);
RADICLE_API radicle_status radicle_papyrus_runtime_register_remote_event(radicle_papyrus_runtime* runtime, uint64_t instance, radicle_papyrus_owner_kind source_kind, const char* source_base_form_key, const char* source_discriminator, const char* event_name, int64_t* out_handle);
/* Requires: capacity entries in out_handles and out_instances (either may be null with capacity 0); out_count is the full match count, an owner with no instance reports instance 0. */
RADICLE_API radicle_status radicle_papyrus_runtime_match_remote_event(const radicle_papyrus_runtime* runtime, radicle_papyrus_owner_kind source_kind, const char* source_base_form_key, const char* source_discriminator, const char* event_name, int64_t* out_handles, uint64_t* out_instances, size_t capacity, size_t* out_count);
/* Invariant: engine callbacks and runtime calls are single-threaded; a NULL callbacks pointer clears the engine hooks. */
RADICLE_API radicle_status radicle_papyrus_runtime_set_engine_callbacks(radicle_papyrus_runtime* runtime, const radicle_papyrus_engine_callbacks* callbacks);
/* Requires: flags is zero or a combination of RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT and RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE; begin_wait is refused on a direct entry, which cannot park, and without the flag; ACCEPT_SCRIPT_INSTANCE requires RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT. */
RADICLE_API radicle_status radicle_papyrus_runtime_register_native_ex(radicle_papyrus_runtime* runtime, const char* script, const char* function, radicle_papyrus_native_kind kind, const char* required_script, uint32_t flags);
/* Invariant: the call and every pointer returned by radicle_papyrus_native_call_* stay valid only inside the callback that receives the call. */
RADICLE_API const radicle_papyrus_object* radicle_papyrus_native_call_self(const radicle_papyrus_native_call* call);
RADICLE_API const char* radicle_papyrus_native_call_script(const radicle_papyrus_native_call* call);
RADICLE_API const char* radicle_papyrus_native_call_function(const radicle_papyrus_native_call* call);
RADICLE_API size_t radicle_papyrus_native_call_arg_count(const radicle_papyrus_native_call* call);
RADICLE_API const radicle_papyrus_value* radicle_papyrus_native_call_arg(const radicle_papyrus_native_call* call, size_t index);
RADICLE_API radicle_status radicle_papyrus_native_call_set_result(radicle_papyrus_native_call* call, const radicle_papyrus_value* value);
/* Requires: after a successful begin_wait the callback returns 1, never 0: a token from a begin_wait followed by 0 stays in the runtime for its whole life, and a later complete_latent on it is stored, not refused. */
/* Invariant: when a token and a result are both set, the token wins and the result is ignored. */
RADICLE_API radicle_status radicle_papyrus_native_call_begin_wait(radicle_papyrus_native_call* call, uint64_t* out_token);
/* Invariant: a failure takes effect only when the callback returns 0; the status is then the call's failure. */
RADICLE_API radicle_status radicle_papyrus_native_call_fail(radicle_papyrus_native_call* call, radicle_papyrus_status status);

#ifdef __cplusplus
}
#endif
