#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_papyrus.h"
#include "fo4/c_api_papyrus_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_papyrus_stage_input {
  int32_t index;
  int32_t startup_stage;
  int32_t fragments;
} radicle_papyrus_stage_input;

typedef struct radicle_papyrus_quest_stages {
  const char* quest;
  const radicle_papyrus_stage_input* stages;
  size_t stage_count;
} radicle_papyrus_quest_stages;

typedef struct radicle_papyrus_stage_outcome {
  const char* quest;
  int32_t state;
  int32_t has_stage;
  int32_t stage;
  const char* reason;
} radicle_papyrus_stage_outcome;

typedef struct radicle_papyrus_stage_report {
  radicle_papyrus_stage_outcome* outcomes;
  size_t count;
  int32_t identified;
  int32_t refused;
  int32_t unresolved;
  char* report;
  char* digest;
} radicle_papyrus_stage_report;

/* Invariant: exactly one marked stage identifies the startup stage; none or more than one is unresolved. */
RADICLE_API radicle_status radicle_papyrus_identify_startup_stages(const radicle_papyrus_quest_stages* quests,
                                                                   size_t count,
                                                                   radicle_papyrus_stage_report* out_report);
RADICLE_API void radicle_papyrus_stage_report_destroy(radicle_papyrus_stage_report* report);

typedef struct radicle_papyrus_vmad_shape {
  const char* name;
  const char* type;
  int32_t has_alias;
  int32_t has_element_count;
  int32_t element_count;
  const char* text;
  int32_t has_real;
  double real;
  int32_t has_integer;
  int64_t integer;
  int32_t has_boolean;
  int32_t boolean;
  const char* object;
} radicle_papyrus_vmad_shape;

typedef struct radicle_papyrus_bound_property {
  const char* owner_key;
  const char* name;
  int32_t kind;
  radicle_papyrus_value* value;
  const char* reason;
} radicle_papyrus_bound_property;

typedef struct radicle_papyrus_bind_report {
  radicle_papyrus_bound_property* properties;
  size_t count;
  int32_t bound;
  int32_t unsupported;
  int32_t silently_defaulted;
  char* report;
  char* digest;
} radicle_papyrus_bind_report;

/* Invariant: a shape Radicle cannot reproduce is reported unsupported with a reason, never defaulted. */
RADICLE_API radicle_status radicle_papyrus_bind_vmad_values(radicle_papyrus_owner_kind owner_kind,
                                                            const char* base_form_key,
                                                            const char* discriminator,
                                                            const radicle_papyrus_vmad_shape* shapes,
                                                            size_t count,
                                                            radicle_papyrus_bind_report* out_report);
RADICLE_API void radicle_papyrus_bind_report_destroy(radicle_papyrus_bind_report* report);

typedef struct radicle_papyrus_alias_input {
  const char* quest;
  int32_t alias_id;
  int32_t kind;
  const char* target;
  const char* location;
} radicle_papyrus_alias_input;

typedef struct radicle_papyrus_alias_outcome {
  const char* owner_key;
  int32_t kind;
  int32_t state;
  const char* value;
  const char* reason;
} radicle_papyrus_alias_outcome;

typedef struct radicle_papyrus_alias_report {
  radicle_papyrus_alias_outcome* outcomes;
  size_t count;
  int32_t filled;
  int32_t open;
  char* report;
  char* digest;
} radicle_papyrus_alias_report;

/* Invariant: an unproven fill leaves the alias empty and counted; find-matching and unique-actor stay open. */
RADICLE_API radicle_status radicle_papyrus_fill_quest_aliases(const radicle_papyrus_alias_input* aliases,
                                                              size_t count,
                                                              radicle_papyrus_alias_report* out_report);
RADICLE_API void radicle_papyrus_alias_report_destroy(radicle_papyrus_alias_report* report);

#ifdef __cplusplus
}
#endif
