#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: these mirror fo4::actor_state::kFormatVersion, kMaxStringBytes and kMaxListEntries; the C++ side static_asserts the match. */
#define RADICLE_ACTOR_STATE_FORMAT_VERSION 1u
#define RADICLE_ACTOR_STATE_MAX_STRING_BYTES 512u
#define RADICLE_ACTOR_STATE_MAX_LIST_ENTRIES 4096u

#define RADICLE_ACTOR_FLAG_ENABLED 1u
#define RADICLE_ACTOR_FLAG_DELETED 2u
#define RADICLE_ACTOR_FLAG_DEAD 4u

/* Invariant: text is a pointer and a byte count, not NUL terminated; a count of 0 means empty and the pointer may be NULL. */
/* Invariant: an empty plugin is "none" and needs local_id 0; plugin forms are named by plugin file name and local id, never a runtime id. */
typedef struct radicle_actor_form_key {
  const char* plugin;
  size_t plugin_size;
  uint32_t local_id;
} radicle_actor_form_key;

typedef enum radicle_actor_ref_kind {
  RADICLE_ACTOR_REF_PLUGIN = 0,
  RADICLE_ACTOR_REF_CREATED = 1
} radicle_actor_ref_kind;

/* Invariant: a PLUGIN ref names its form; a CREATED ref names an engine-allocated created_id, its base and its spawn_seed. The unused fields are carried as written. */
typedef struct radicle_actor_ref {
  radicle_actor_ref_kind kind;
  radicle_actor_form_key form;
  uint32_t created_id;
  radicle_actor_form_key base;
  uint64_t spawn_seed;
} radicle_actor_ref;

typedef struct radicle_actor_inventory_delta {
  radicle_actor_form_key item;
  int32_t count;
} radicle_actor_inventory_delta;

typedef struct radicle_actor_value_modifier {
  const char* actor_value;
  size_t actor_value_size;
  float value;
} radicle_actor_value_modifier;

typedef struct radicle_actor_faction_rank {
  radicle_actor_form_key faction;
  int32_t rank;
} radicle_actor_faction_rank;

/* Invariant: a list is a pointer and an entry count; a count of 0 means empty and the pointer may be NULL; a count above RADICLE_ACTOR_STATE_MAX_LIST_ENTRIES is refused. */
/* Invariant: has_combat_target is 0 or 1; when it is 0 combat_target is ignored on encode and zero on decode. */
/* Invariant: unknown_tail holds the payload bytes after the known fields; decode keeps them and encode writes them back verbatim, so a blob with trailing fields re-encodes byte for byte. */
/* Invariant: flags bits other than the RADICLE_ACTOR_FLAG_* ones are carried as written. */
/* Invariant: encode reads every pointer here as borrowed; a decoded view borrows from its handle. */
typedef struct radicle_actor_state {
  radicle_actor_ref ref;
  uint32_t flags;
  radicle_actor_form_key cell;
  float position[3];
  float rotation[3];
  float scale;
  const radicle_actor_inventory_delta* inventory;
  size_t inventory_count;
  const radicle_actor_value_modifier* actor_values;
  size_t actor_value_count;
  const radicle_actor_faction_rank* factions;
  size_t faction_count;
  radicle_actor_form_key forced_package;
  radicle_actor_form_key current_package;
  int32_t has_combat_target;
  radicle_actor_ref combat_target;
  int32_t bounty;
  uint16_t spawn_level;
  const uint8_t* unknown_tail;
  size_t unknown_tail_size;
} radicle_actor_state;

/* Invariant: owns the strings, lists and tail a decoded radicle_actor_state view points into; released with radicle_actor_state_free. */
typedef struct radicle_actor_state_decoded radicle_actor_state_decoded;

/* Invariant: the blob is "RASV", u32 version and u32 payload size, all little-endian, then the payload; only version 1 exists. */
/* Invariant: a rejected call clears *out_bytes and *out_size; INVALID_ARGUMENT for NULL arguments, a NULL list or text with a non-zero count, a count or text above its cap, an empty form key with a non-zero local id, a ref kind outside the enum, a has_combat_target other than 0 or 1, or a NULL unknown_tail with a non-zero size. */
/* Invariant: the blob is malloc'd and released with radicle_free_bytes. */
RADICLE_API radicle_status radicle_actor_state_encode(const radicle_actor_state* state,
                                                      uint8_t** out_bytes,
                                                      size_t* out_size);

/* Invariant: `data` may be NULL only when size is 0 and may be freed on return; any failure leaves *out NULL. */
/* Invariant: PARSE_ERROR for a bad magic, any version other than 1, a payload size that is not the bytes after the header, truncated or oversized text or lists, a ref kind above 1, a combat target presence above 1, or an empty form key with a non-zero local id; it never reads past `size`. */
RADICLE_API radicle_status radicle_actor_state_decode(const uint8_t* data,
                                                      size_t size,
                                                      radicle_actor_state_decoded** out);

/* Invariant: the view and every pointer in it stay valid until radicle_actor_state_free; it can be passed to radicle_actor_state_encode as is. */
RADICLE_API radicle_status radicle_actor_state_view(const radicle_actor_state_decoded* decoded,
                                                    radicle_actor_state* out_state);

/* Invariant: accepts NULL. */
RADICLE_API void radicle_actor_state_free(radicle_actor_state_decoded* decoded);

#ifdef __cplusplus
}
#endif
