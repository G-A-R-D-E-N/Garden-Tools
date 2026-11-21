#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Constraint: radicle_npc_visual_part.source is 0 FaceGen head, 1 race or NPC_ skin, 2 default outfit, 3 object-template mesh. */
typedef struct radicle_npc_visual_part {
  const char* mesh_entry;
  uint32_t biped_slots;
  uint8_t source;
} radicle_npc_visual_part;

/* has_level 0 ignores level and uses 1; a set level must be 1..65535 and reserved must be zero. */
typedef struct radicle_npc_visual_options {
  uint8_t has_level;
  uint8_t reserved[3];
  uint32_t level;
  uint64_t seed;
} radicle_npc_visual_options;

/* Decoded views require radicle_npc_visual_free. Every pointer, array and string is owned by the view. */
typedef struct radicle_npc_visual {
  radicle_form_key npc;
  radicle_form_key race;
  uint8_t has_race;
  const char* race_editor_id;
  uint8_t female;
  uint8_t has_skin_tint;
  float skin_tint[4];
  const char* skeleton_hkx_entry;
  const radicle_npc_visual_part* parts;
  size_t part_count;
  const char* idle_clip_entry;
  const char* const* stubs;
  size_t stub_count;
  void* internal;
  /* Constraint: valid only for the options.level it was decoded with. */
  uint8_t seed_dependent;
} radicle_npc_visual;

/* An engine requires the level the actor spawns at. A record that is not an NPC_ reports RADICLE_STATUS_INVALID_ARGUMENT and a malformed NPC_ reports RADICLE_STATUS_PARSE_ERROR. A NULL options pointer requires the defaults (level 1, seed 0). has_level above 1, a non-zero reserved byte, or a set level outside 1..65535 reports RADICLE_STATUS_INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_npc_visual_decode_ex(const radicle_env* env,
                                                        const radicle_record* npc,
                                                        const radicle_npc_visual_options* options,
                                                        radicle_npc_visual** out_visual);
RADICLE_API void radicle_npc_visual_free(radicle_npc_visual* visual);

#ifdef __cplusplus
}
#endif
