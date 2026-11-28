#pragma once

#include "fo4/c_api_conditions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: values are append-only; a host answers only the facts it owns and reports the rest as unknown. */
typedef enum radicle_condition_fact {
  RADICLE_CONDITION_FACT_CURRENT_TIME = 1,
  RADICLE_CONDITION_FACT_IS_DEAD = 2,
  RADICLE_CONDITION_FACT_LEVEL = 3,
  RADICLE_CONDITION_FACT_IS_ID = 4,
  RADICLE_CONDITION_FACT_IS_RACE = 5,
  RADICLE_CONDITION_FACT_IN_FACTION = 6,
  RADICLE_CONDITION_FACT_FACTION_RANK = 7,
  RADICLE_CONDITION_FACT_HAS_KEYWORD = 8,
  RADICLE_CONDITION_FACT_GLOBAL_VALUE = 9,
  RADICLE_CONDITION_FACT_ACTOR_VALUE = 10,
  RADICLE_CONDITION_FACT_QUEST_STAGE = 11,
  RADICLE_CONDITION_FACT_QUEST_STAGE_DONE = 12,
  RADICLE_CONDITION_FACT_QUEST_RUNNING = 13,
  RADICLE_CONDITION_FACT_QUEST_COMPLETED = 14,
  RADICLE_CONDITION_FACT_IS_VOICE_TYPE = 15
  , RADICLE_CONDITION_FACT_IS_SEX = 16
  , RADICLE_CONDITION_FACT_ITEM_COUNT = 17
  , RADICLE_CONDITION_FACT_IS_ALIAS_REF = 18
} radicle_condition_fact;

/* Constraint: form is the typed first parameter (plugin NULL for current time, dead and level), reference is the resolved run-on reference (plugin NULL unless run_on is the reference code), reference_raw and param3 are the row's raw reference field and CTDA third parameter so a host can resolve run-on codes that carry a selector there; integer is the second parameter for QUEST_STAGE_DONE and the alias id (param 1 raw) for IS_ALIAS_REF; IS_SEX asks the host for the subject's sex ordinal and Radicle compares it with param 1 raw; IS_ALIAS_REF carries in form the quest of a QUST row or the parent quest of a SCEN row and a NULL-plugin key for any other record; ITEM_COUNT is the host's count of form in the subject's inventory; keys borrow from the environment and are valid only inside the callback. Radicle passes every run_on through unchanged: a host must return unknown (0) for a run_on it cannot resolve. */
typedef struct radicle_condition_query {
  radicle_condition_fact fact;
  uint32_t run_on;
  radicle_form_key reference;
  radicle_form_key form;
  int32_t integer;
  uint32_t reference_raw;
  int32_t param3;
} radicle_condition_query;

/* Requires: struct_size is sizeof(radicle_condition_live_state) and query is set; query returns 1 and writes *out_value when the host knows the fact (booleans are 1.0 and 0.0, a faction rank is -1 for a non-member), else 0 for unknown, and must not call back into Radicle with this environment's records. */
typedef struct radicle_condition_live_state {
  size_t struct_size;
  void* user;
  int32_t (*query)(void* user, const radicle_condition_query* query, double* out_value);
} radicle_condition_live_state;

/* Constraint: out_values must hold count entries and entry i is the live value of rows[i]; an unknown fact, a row the live state cannot decide (unmapped function, alias, package-data or swap flag, unresolved form parameter) is known 0, never a false value; a use-global comparison is read by the same host-first rule as radicle_glob_value_live (a Constant GLOB falls back to its record, any other GLOB without a host value leaves the row unevaluable); values are narrowed to float and a non-finite or out-of-float-range host number is unknown, and resolved_run_on and resolved_reference echo the row so the evaluation kernel accepts them. record must be the record the rows were read from, because its master table resolves the FormIDs. */
RADICLE_API radicle_status radicle_condition_live_values(const radicle_env* env,
                                                         const radicle_record* record,
                                                         const radicle_condition* rows, size_t count,
                                                         const radicle_condition_live_state* live,
                                                         radicle_condition_value* out_values);

/* Constraint: the same rows as radicle_condition_live_values followed by radicle_conditions_evaluate; an unknown fact makes the row unevaluable, and the verdict is unevaluable only when no OR group is already false. */
RADICLE_API radicle_status radicle_conditions_evaluate_live(const radicle_env* env,
                                                            const radicle_record* record,
                                                            const radicle_condition* rows, size_t count,
                                                            const radicle_condition_live_state* live,
                                                            radicle_condition_result* out);

typedef enum radicle_glob_live_reason {
  RADICLE_GLOB_LIVE_HOST = 0,
  RADICLE_GLOB_LIVE_RECORD_CONSTANT = 1,
  RADICLE_GLOB_LIVE_NOT_GLOB = 2,
  RADICLE_GLOB_LIVE_NO_VALUE = 3
} radicle_glob_live_reason;

/* Constraint: the live state is asked first with fact GLOBAL_VALUE and the GLOB's key; when it has no value a GLOB with the Constant record flag answers from its stored float (RECORD_CONSTANT) and any other GLOB reports NO_VALUE, never its stored default; *out_value is 0 unless the reason is HOST or RECORD_CONSTANT. */
RADICLE_API radicle_status radicle_glob_value_live(const radicle_env* env,
                                                   const radicle_record* glob,
                                                   const radicle_condition_live_state* live,
                                                   double* out_value,
                                                   radicle_glob_live_reason* out_reason);

#ifdef __cplusplus
}
#endif
