#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_combat_reaction {
  RADICLE_COMBAT_REACTION_NEUTRAL = 0,
  RADICLE_COMBAT_REACTION_ENEMY = 1,
  RADICLE_COMBAT_REACTION_ALLY = 2,
  RADICLE_COMBAT_REACTION_FRIENDLY = 3,
  RADICLE_COMBAT_REACTION_UNKNOWN = 4
} radicle_combat_reaction;

typedef enum radicle_level_reason {
  RADICLE_LEVEL_REASON_FIXED = 0,
  RADICLE_LEVEL_REASON_LEVEL_MULT_UNPROVEN = 1,
  RADICLE_LEVEL_REASON_TEMPLATE_STATS_UNPROVEN = 2,
  RADICLE_LEVEL_REASON_XLCM_UNPROVEN = 3,
  RADICLE_LEVEL_REASON_ZONE_UNPROVEN = 4
} radicle_level_reason;

typedef enum radicle_reaction_reason {
  RADICLE_REACTION_REASON_RELATION = 0,
  RADICLE_REACTION_REASON_NO_RELATION = 1,
  RADICLE_REACTION_REASON_CONFLICT = 2
} radicle_reaction_reason;

typedef enum radicle_av_evidence {
  RADICLE_AV_EVIDENCE_RECORD_STORED = 0,
  RADICLE_AV_EVIDENCE_UNPROVEN = 1
} radicle_av_evidence;

typedef enum radicle_av_source {
  RADICLE_AV_SOURCE_NONE = 0,
  RADICLE_AV_SOURCE_CALCULATED_HEALTH = 1,
  RADICLE_AV_SOURCE_STORED_PROPERTY = 3,
  RADICLE_AV_SOURCE_CALCULATED_ACTION_POINTS = 2
} radicle_av_source;

typedef enum radicle_combat_style_source {
  RADICLE_COMBAT_STYLE_SOURCE_OWN_RECORD = 0,
  RADICLE_COMBAT_STYLE_SOURCE_ABSENT_FALLBACK_UNPROVEN = 1
} radicle_combat_style_source;

typedef struct radicle_npc_faction_membership {
  radicle_form_key faction;
  int32_t rank;
} radicle_npc_faction_membership;

typedef struct radicle_npc_perk_rank {
  radicle_form_key perk;
  uint32_t rank;
} radicle_npc_perk_rank;

/* Constraint: owner, global_or_rank and condition require has_extra (COED) to be 1, and owner requires has_owner to be 1. */
typedef struct radicle_npc_inventory_item {
  radicle_form_key item;
  int32_t count;
  uint8_t has_extra;
  uint8_t has_owner;
  radicle_form_key owner;
  uint32_t global_or_rank;
  float condition;
} radicle_npc_inventory_item;

/* Constraint: free with radicle_npc_inventory_free. */
typedef struct radicle_npc_inventory {
  radicle_npc_inventory_item* items;
  size_t item_count;
  void* internal;
} radicle_npc_inventory;

typedef struct radicle_npc_ai_data {
  uint8_t aggression;
  uint8_t confidence;
  uint8_t energy_level;
  uint8_t morality;
  uint8_t mood;
  uint8_t assistance;
} radicle_npc_ai_data;

typedef struct radicle_npc_gameplay {
  radicle_form_key npc;
  uint32_t configuration_flags;
  uint32_t level_raw;
  uint32_t calc_min_level;
  uint32_t calc_max_level;
  uint32_t template_flags;
  uint8_t level_is_multiplier;
  uint8_t has_race;
  radicle_form_key race;
  uint8_t has_class;
  radicle_form_key class_form;
  uint8_t has_combat_style;
  radicle_form_key combat_style;
  uint8_t has_crime_faction;
  radicle_form_key crime_faction;
  uint8_t has_template;
  radicle_form_key template_base;
  radicle_form_key* template_chain;
  size_t template_chain_count;
  radicle_form_key* spells;
  size_t spell_count;
  radicle_npc_faction_membership* factions;
  size_t faction_count;
  radicle_npc_perk_rank* perks;
  size_t perk_count;
  uint8_t has_ai_data;
  radicle_npc_ai_data ai;
  uint8_t has_calculated_stats;
  uint32_t calculated_health;
  uint32_t calculated_action_points;
  const char* const* stubs;
  size_t stub_count;
  void* internal;
} radicle_npc_gameplay;

/* Constraint: the slot number is the ACBS template flag bit and the TPTA link index (docs/NPCS.md). */
typedef enum radicle_npc_template_slot {
  RADICLE_NPC_TEMPLATE_TRAITS = 0,
  RADICLE_NPC_TEMPLATE_STATS = 1,
  RADICLE_NPC_TEMPLATE_FACTIONS = 2,
  RADICLE_NPC_TEMPLATE_SPELL_LIST = 3,
  RADICLE_NPC_TEMPLATE_AI_DATA = 4,
  RADICLE_NPC_TEMPLATE_AI_PACKAGES = 5,
  RADICLE_NPC_TEMPLATE_MODEL_ANIMATION = 6,
  RADICLE_NPC_TEMPLATE_BASE_DATA = 7,
  RADICLE_NPC_TEMPLATE_INVENTORY = 8,
  RADICLE_NPC_TEMPLATE_SCRIPT = 9,
  RADICLE_NPC_TEMPLATE_DEF_PACK_LIST = 10,
  RADICLE_NPC_TEMPLATE_ATTACK_DATA = 11,
  RADICLE_NPC_TEMPLATE_KEYWORDS = 12
} radicle_npc_template_slot;

/* Constraint: OWN and TEMPLATE_NPC are resolved; every other reason means the chain stopped at source and the engine must treat the slot as undecided. */
typedef enum radicle_npc_template_reason {
  RADICLE_NPC_TEMPLATE_REASON_OWN = 0,
  RADICLE_NPC_TEMPLATE_REASON_TEMPLATE_NPC = 1,
  RADICLE_NPC_TEMPLATE_REASON_NO_LINK = 2,
  RADICLE_NPC_TEMPLATE_REASON_NOT_FOUND = 3,
  RADICLE_NPC_TEMPLATE_REASON_UNDECODABLE = 4,
  RADICLE_NPC_TEMPLATE_REASON_NOT_NPC = 5,
  RADICLE_NPC_TEMPLATE_REASON_LEVELED_LIST = 6,
  RADICLE_NPC_TEMPLATE_REASON_TOO_DEEP = 7
} radicle_npc_template_reason;

typedef struct radicle_npc_template_source {
  radicle_form_key source;
  radicle_npc_template_reason reason;
  uint32_t hops;
  uint8_t has_blocker;
  radicle_form_key blocker;
} radicle_npc_template_source;

typedef enum radicle_glob_reason {
  RADICLE_GLOB_REASON_OK = 0,
  RADICLE_GLOB_REASON_NOT_GLOB = 1,
  RADICLE_GLOB_REASON_NOT_CONSTANT = 2,
  RADICLE_GLOB_REASON_NO_VALUE = 3
} radicle_glob_reason;

typedef struct radicle_actor_level {
  uint32_t level;
  uint32_t raw_level;
  radicle_level_reason reason;
  uint32_t zone_level;
} radicle_actor_level;

typedef struct radicle_faction_reaction_result {
  radicle_combat_reaction reaction;
  radicle_reaction_reason reason;
  uint8_t has_decider;
  radicle_form_key decider_actor_faction;
  radicle_form_key decider_target_faction;
} radicle_faction_reaction_result;

typedef struct radicle_actor_value_base_result {
  int32_t base;
  radicle_av_source source;
  radicle_av_evidence evidence;
} radicle_actor_value_base_result;

typedef struct radicle_combat_profile {
  radicle_form_key npc;
  uint8_t has_style;
  radicle_form_key style;
  radicle_combat_style_source source;
} radicle_combat_profile;

typedef struct radicle_crime_rules {
  radicle_form_key faction;
  uint8_t has_crime_values;
  uint8_t arrest;
  uint8_t attack_on_sight;
  uint16_t murder;
  uint16_t assault;
  uint16_t trespass;
  uint16_t pickpocket;
  uint16_t unknown;
  uint8_t has_steal_multiplier;
  float steal_multiplier;
  uint8_t has_escape;
  uint16_t escape;
  uint8_t has_werewolf;
  uint16_t werewolf;
  uint8_t has_shared_crime_list;
  radicle_form_key shared_crime_list;
} radicle_crime_rules;

RADICLE_API radicle_status radicle_npc_gameplay_decode(const radicle_env* env,
                                                       const radicle_record* npc,
                                                       radicle_npc_gameplay** out_gameplay);
RADICLE_API void radicle_npc_gameplay_free(radicle_npc_gameplay* gameplay);

/* Constraint: the NPC_ own CNTO rows only; a list item is returned as its key and must be resolved with radicle_leveled_pick_count. */
RADICLE_API radicle_status radicle_npc_inventory_decode(const radicle_env* env,
                                                        const radicle_record* npc,
                                                        radicle_npc_inventory** out_inventory);
RADICLE_API void radicle_npc_inventory_free(radicle_npc_inventory* inventory);

RADICLE_API radicle_status radicle_actor_level_for_spawn(const radicle_env* env,
                                                         const radicle_record* npc,
                                                         uint32_t player_level,
                                                         int32_t level_modifier,
                                                         const radicle_form_key* encounter_zone,
                                                         radicle_actor_level* out);

RADICLE_API radicle_status radicle_faction_reaction(const radicle_env* env,
                                                    const radicle_form_key* actor_factions,
                                                    size_t actor_faction_count,
                                                    const radicle_form_key* target_factions,
                                                    size_t target_faction_count,
                                                    radicle_faction_reaction_result* out);

RADICLE_API radicle_status radicle_actor_value_base(const radicle_env* env,
                                                    const radicle_record* npc,
                                                    radicle_form_key avif,
                                                    radicle_actor_value_base_result* out);

RADICLE_API radicle_status radicle_combat_profile_decode(const radicle_env* env,
                                                         const radicle_record* npc,
                                                         radicle_combat_profile* out);

RADICLE_API radicle_status radicle_crime_rules_decode(const radicle_env* env,
                                                      const radicle_record* faction,
                                                      radicle_crime_rules* out);

RADICLE_API radicle_status radicle_ownership_check(const radicle_env* env,
                                                   radicle_form_key owner,
                                                   radicle_form_key actor,
                                                   const radicle_npc_faction_membership* actor_factions,
                                                   size_t actor_faction_count,
                                                   int32_t required_rank,
                                                   uint8_t* allowed);

/* Constraint: source is the NPC_ whose own data applies for the slot (read PKID, DPLT, RNAM, SNAM or KWDA from it); blocker is the link the walk could not follow (an LVLN is reported, never picked from). */
RADICLE_API radicle_status radicle_npc_template_source_resolve(const radicle_env* env,
                                                               const radicle_record* npc,
                                                               uint32_t slot,
                                                               radicle_npc_template_source* out);

/* Constraint: keys are PKID entries of the AI Packages template source, then LNAM entries of the FLST named by DPLT on the Def Pack List template source, in stored order with null links dropped and no deduplication; a malformed PKID or LNAM or an undecodable FLST is PARSE_ERROR; out_reason is the first slot whose chain stopped (AI packages first), and a reason other than OWN or TEMPLATE_NPC means the list is undecided with *out_keys null; release with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_npc_package_list(const radicle_env* env,
                                                    const radicle_record* npc,
                                                    radicle_form_key** out_keys,
                                                    size_t* out_count,
                                                    radicle_npc_template_source* out_reason);

/* Constraint: *out_value is set only when *out_reason is OK; the Constant record flag (0x40) is required only when constant_only is non-zero; a record that is not a GLOB reports NOT_GLOB with status OK. */
RADICLE_API radicle_status radicle_glob_value(const radicle_env* env,
                                              const radicle_record* glob,
                                              uint8_t constant_only,
                                              double* out_value,
                                              radicle_glob_reason* out_reason);

/* Constraint: the NPC_ own VTCK wins, else TPLT is followed regardless of ACBS template flags and TPTA; OWN or TEMPLATE_NPC in out_reason means *out_voice is set (out_reason->source carries it), any other reason leaves *out_voice zeroed and the walk stopped at out_reason->source. */
RADICLE_API radicle_status radicle_npc_voice_type(const radicle_env* env,
                                                  const radicle_record* npc,
                                                  radicle_form_key* out_voice,
                                                  radicle_npc_template_source* out_reason);

#ifdef __cplusplus
}
#endif
