#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Constraint: radicle_leveled_pick_result.reason and radicle_spawn_plan.reason are 0 picked, 1 chance_none, 2 no_entry_at_level, 3 entries_rolled_out, 4 nothing_live, 5 budget_exceeded, 6 depth_exceeded, 7 list_missing, 8 not_leveled_list, 9 undecodable, 10 not_spawnable. */
/* Constraint: reasons 5 and 6 outrank the others. */
/* Decoded views require radicle_leveled_pick_result_free; entries and internal are owned by the view. */
typedef struct radicle_leveled_pick_result {
  const radicle_form_key* entries;
  size_t entry_count;
  uint8_t reason;
  uint32_t depth_used;
  void* internal;
} radicle_leveled_pick_result;

/* Constraint: reason uses the radicle_leveled_pick_result codes. rolls is the number of independent rolls drawn and multiplier is what every entry's count is multiplied by (1 when calculate-for-each applied, else the requested count). entries joins the rolls in order. Free with radicle_leveled_pick_count_result_free. */
typedef struct radicle_leveled_pick_count_result {
  const radicle_form_key* entries;
  size_t entry_count;
  uint8_t reason;
  uint32_t depth_used;
  uint32_t rolls;
  uint32_t multiplier;
  /* Constraint: entry_counts[i] must be read as the row count of entries[i]. */
  const uint16_t* entry_counts;
  void* internal;
} radicle_leveled_pick_count_result;

/* Constraint: enabled must be 0 or 1; the same ref may not appear twice in one call. */
typedef struct radicle_ref_state_override {
  radicle_form_key ref;
  uint8_t enabled;
} radicle_ref_state_override;

/* Constraint: radicle_enable_verdict.stop is 0 root, 1 overridden, 2 cycle, 3 missing_parent, 4 depth_limit. */
typedef struct radicle_enable_verdict {
  uint8_t enabled;
  uint8_t stop;
  uint32_t chain_length;
  radicle_form_key deciding;
} radicle_enable_verdict;

/* Constraint: base_kind is 0 none, 1 NPC_, 2 LVLN, 3 LVLI, 4 other; resolved_base is the LVLN pick at player_level; resolved_level stays 0 and level_rule_verified 0 (XLCM rule UNVERIFIED). Plain struct, nothing to free. */
typedef struct radicle_actor_spawn {
  uint8_t has_base;
  uint8_t base_kind;
  uint8_t actor_reference;
  uint8_t persistent;
  radicle_form_key base;
  int32_t has_count;
  int32_t count;
  int32_t has_level_modifier;
  int32_t level_modifier;
  uint8_t has_encounter_zone;
  uint8_t initially_disabled;
  radicle_form_key encounter_zone;
  radicle_enable_verdict enable;
  uint8_t has_resolved_base;
  radicle_form_key resolved_base;
  uint8_t has_zone_min_level;
  int32_t zone_min_level;
  uint8_t level_rule_verified;
  uint32_t resolved_level;
} radicle_actor_spawn;

/* Constraint: radicle_spawn_plan.base_kind uses the radicle_actor_spawn codes; level_modifier is echoed, not applied (meaning UNVERIFIED). Free with radicle_spawn_plan_free. */
typedef struct radicle_spawn_plan {
  uint8_t base_kind;
  uint8_t reason;
  uint32_t depth_used;
  radicle_form_key base;
  const radicle_form_key* entries;
  size_t entry_count;
  int32_t has_level_modifier;
  int32_t level_modifier;
  void* internal;
} radicle_spawn_plan;

/* level must be 1..65535; list may be any record: a missing list or a non-leveled record reports through reason, not a status. flags_override_mask may set 0x04 (use all) and 0x02 (calculate for each), applied to the top list; any other bit is refused. This single-draw form is the count-1 case of radicle_leveled_pick_count. */
RADICLE_API radicle_status radicle_leveled_pick(const radicle_env* env,
                                                radicle_form_key list,
                                                uint32_t level,
                                                uint64_t seed,
                                                uint32_t flags_override_mask,
                                                radicle_leveled_pick_result** out_pick);
RADICLE_API void radicle_leveled_pick_result_free(radicle_leveled_pick_result* pick);

/* count must be 1..32767 (the engine clamps the leveled count at 0x7FFF). When the top list has LVLF 0x02 (or flags_override_mask sets it) the pick is count independent rolls whose entries are joined, multiplier 1; otherwise one roll with multiplier count. Roll 0 uses seed unchanged and roll n derives its seed from seed, so the result is deterministic and count 1 equals radicle_leveled_pick. The call stops with reason 5 (budget_exceeded) at 65536 nodes or 65536 entries in total across rolls. Other arguments follow radicle_leveled_pick. */
RADICLE_API radicle_status radicle_leveled_pick_count(const radicle_env* env,
                                                      radicle_form_key list,
                                                      uint32_t level,
                                                      uint64_t seed,
                                                      uint32_t flags_override_mask,
                                                      uint32_t count,
                                                      radicle_leveled_pick_count_result** out_pick);
RADICLE_API void radicle_leveled_pick_count_result_free(radicle_leveled_pick_count_result* pick);

/* ref must be a placed reference and the load-order winner; RADICLE_STATUS_INVALID_ARGUMENT otherwise. Overrides may be NULL when override_count is 0. */
RADICLE_API radicle_status radicle_ref_enable_state(const radicle_env* env,
                                                    const radicle_record* ref,
                                                    const radicle_ref_state_override* overrides,
                                                    size_t override_count,
                                                    radicle_enable_verdict* out_verdict);

/* placed must be an ACHR or other placed reference and the load-order winner; player_level must be 1..65535 and seed feeds the LVLN pick. A malformed placed record reports RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_placed_actor_spawn_decode(const radicle_env* env,
                                                             const radicle_record* placed,
                                                             uint32_t player_level,
                                                             uint64_t seed,
                                                             const radicle_ref_state_override* overrides,
                                                             size_t override_count,
                                                             radicle_actor_spawn* out_spawn);

/* has_level_modifier must be 0 or 1; level_modifier is ignored when it is 0. */
RADICLE_API radicle_status radicle_script_spawn_plan(const radicle_env* env,
                                                     radicle_form_key base,
                                                     uint32_t level,
                                                     uint64_t seed,
                                                     int32_t has_level_modifier,
                                                     int32_t level_modifier,
                                                     radicle_spawn_plan** out_plan);
RADICLE_API void radicle_spawn_plan_free(radicle_spawn_plan* plan);

#ifdef __cplusplus
}
#endif
