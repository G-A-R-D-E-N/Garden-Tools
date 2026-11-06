#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_papyrus.h"
#include "fo4/c_api_papyrus_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a lifecycle borrows its runtime and takes its gate from the runtime callbacks; destroy the lifecycle first. */

typedef struct radicle_papyrus_lifecycle_handle radicle_papyrus_lifecycle_handle;

typedef struct radicle_papyrus_lifecycle_property {
  const char* script;
  const char* name;
  const char* type;
  int32_t has_alias;
  const char* text;
  int32_t has_real;
  double real;
  int32_t has_integer;
  int64_t integer;
  int32_t has_boolean;
  int32_t boolean;
  const char* object;
} radicle_papyrus_lifecycle_property;

typedef struct radicle_papyrus_quest_startup_input {
  const char* quest;
  int32_t start_game_enabled;
  const char* const* scripts;
  size_t script_count;
  int32_t fragments;
  int32_t aliases;
  const char* const* unsupported;
  size_t unsupported_count;
  const radicle_papyrus_lifecycle_property* properties;
  size_t property_count;
} radicle_papyrus_quest_startup_input;

typedef struct radicle_papyrus_alias_script_input {
  const char* quest;
  int32_t alias_id;
  const char* script;
  const radicle_papyrus_lifecycle_property* properties;
  size_t property_count;
  const int32_t* objectives;
  size_t objective_count;
} radicle_papyrus_alias_script_input;

typedef struct radicle_papyrus_actor_alias_input {
  radicle_papyrus_alias_script_input alias;
  int32_t bound;
  const char* target;
} radicle_papyrus_actor_alias_input;

typedef struct radicle_papyrus_quest_startup_plan {
  int32_t ready;
  int32_t blocked;
  int32_t skipped;
  char* report;
  char* digest;
} radicle_papyrus_quest_startup_plan;

typedef struct radicle_papyrus_quest_startup_result {
  int32_t applied;
  int32_t started;
  int32_t attached_scripts;
  char** reasons;
  size_t reason_count;
} radicle_papyrus_quest_startup_result;

typedef struct radicle_papyrus_alias_attach_report {
  int32_t attached;
  int32_t skipped;
  int32_t refused;
  int32_t properties;
  int32_t natives;
  int32_t objectives;
  char* report;
  char* digest;
} radicle_papyrus_alias_attach_report;

typedef struct radicle_papyrus_actor_bind_report {
  int32_t bound;
  int32_t unbound;
  int32_t attached;
  int32_t refused;
  char* report;
  char* digest;
} radicle_papyrus_actor_bind_report;

typedef struct radicle_papyrus_lifecycle_counters {
  int32_t started;
  int32_t blocked;
  int32_t skipped;
  int32_t attached_scripts;
  int32_t approximations;
} radicle_papyrus_lifecycle_counters;

typedef enum radicle_papyrus_fanout_result {
  RADICLE_PAPYRUS_FANOUT_DELIVERED = 0,
  RADICLE_PAPYRUS_FANOUT_REFUSED = 1,
  RADICLE_PAPYRUS_FANOUT_SKIPPED_PARKED = 2,
  RADICLE_PAPYRUS_FANOUT_NO_HANDLER = 3,
  RADICLE_PAPYRUS_FANOUT_UNKNOWN_OWNER = 4
} radicle_papyrus_fanout_result;

typedef struct radicle_papyrus_fanout_outcome {
  char* owner_key;
  uint64_t instance;
  char* script;
  radicle_papyrus_fanout_result result;
  radicle_papyrus_status status;
  char* reason;
} radicle_papyrus_fanout_outcome;

/* Invariant: delivered means the event was queued; a later step reports how the handler ran. */
typedef struct radicle_papyrus_fanout_report {
  radicle_papyrus_fanout_outcome* outcomes;
  size_t count;
  int32_t delivered;
  int32_t refused;
  int32_t skipped_parked;
  int32_t no_handler;
  int32_t unknown_owner;
  char* handler;
  char* report;
  char* digest;
} radicle_papyrus_fanout_report;

RADICLE_API radicle_status radicle_papyrus_lifecycle_create(radicle_papyrus_runtime* runtime, radicle_papyrus_lifecycle_handle** out_lifecycle);
RADICLE_API void radicle_papyrus_lifecycle_destroy(radicle_papyrus_lifecycle_handle* lifecycle);

RADICLE_API radicle_status radicle_papyrus_lifecycle_preflight(radicle_papyrus_lifecycle_handle* lifecycle, const radicle_papyrus_quest_startup_input* inputs, size_t count, radicle_papyrus_quest_startup_plan* out_plan);
RADICLE_API void radicle_papyrus_quest_startup_plan_destroy(radicle_papyrus_quest_startup_plan* plan);
RADICLE_API radicle_status radicle_papyrus_lifecycle_start(radicle_papyrus_lifecycle_handle* lifecycle, const radicle_papyrus_quest_startup_input* inputs, size_t count, radicle_papyrus_quest_startup_result* out_result);
RADICLE_API void radicle_papyrus_quest_startup_result_destroy(radicle_papyrus_quest_startup_result* result);
RADICLE_API radicle_status radicle_papyrus_lifecycle_stop(radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, int32_t* out_was_started);
RADICLE_API int32_t radicle_papyrus_lifecycle_is_started(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest);
RADICLE_API size_t radicle_papyrus_lifecycle_instance_count(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest);
RADICLE_API int32_t radicle_papyrus_lifecycle_instance_at(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, size_t index, uint64_t* out_instance);
RADICLE_API radicle_status radicle_papyrus_lifecycle_get_counters(const radicle_papyrus_lifecycle_handle* lifecycle, radicle_papyrus_lifecycle_counters* out_counters);

RADICLE_API radicle_status radicle_papyrus_lifecycle_attach_aliases(radicle_papyrus_lifecycle_handle* lifecycle, const radicle_papyrus_alias_script_input* aliases, size_t count, radicle_papyrus_alias_attach_report* out_report);
RADICLE_API void radicle_papyrus_alias_attach_report_destroy(radicle_papyrus_alias_attach_report* report);
RADICLE_API radicle_status radicle_papyrus_lifecycle_bind_actors(radicle_papyrus_lifecycle_handle* lifecycle, const radicle_papyrus_actor_alias_input* aliases, size_t count, radicle_papyrus_actor_bind_report* out_report);
RADICLE_API void radicle_papyrus_actor_bind_report_destroy(radicle_papyrus_actor_bind_report* report);
RADICLE_API radicle_status radicle_papyrus_lifecycle_clear_alias(radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, int32_t alias_id, int32_t* out_cleared);
RADICLE_API radicle_status radicle_papyrus_lifecycle_clear_quest_aliases(radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, int32_t* out_cleared);
RADICLE_API int32_t radicle_papyrus_lifecycle_alias_instance(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, int32_t alias_id, uint64_t* out_instance);
RADICLE_API int32_t radicle_papyrus_lifecycle_objective(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest, int32_t alias_id, int32_t index);

RADICLE_API radicle_status radicle_papyrus_runtime_fan_out_event(radicle_papyrus_runtime* runtime, radicle_papyrus_owner_kind owner_kind, const char* base_form_key, const char* discriminator, const char* event_name, const radicle_papyrus_value* const* args, size_t arg_count, radicle_papyrus_fanout_report* out_report);
RADICLE_API radicle_status radicle_papyrus_runtime_broadcast_event(radicle_papyrus_runtime* runtime, const char* event_name, const radicle_papyrus_value* const* args, size_t arg_count, radicle_papyrus_fanout_report* out_report);
RADICLE_API void radicle_papyrus_fanout_report_destroy(radicle_papyrus_fanout_report* report);
RADICLE_API radicle_status radicle_papyrus_runtime_park_owner(radicle_papyrus_runtime* runtime, radicle_papyrus_owner_kind owner_kind, const char* base_form_key, const char* discriminator, int32_t parked);

#ifdef __cplusplus
}
#endif
