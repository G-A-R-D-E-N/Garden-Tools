#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: every `.pex` payload the assets reach under Scripts/ resolves to one winner per name. */
/* Invariant: a loose payload beats an archive payload, then a higher load index wins, then the ordinally greater container path. */
/* Invariant: a name is a lower case path under Scripts/ without the .pex extension, so names compare without regard to case. */
/* Invariant: a set borrows the radicle_assets handle it was resolved from, which must outlive the set. */
/* Invariant: strings returned here stay valid until radicle_script_set_destroy and are owned by the set. */
/* Invariant: a set is not thread-safe; different sets may be used from different threads, and radicle_last_error is shared. */
/* Invariant: a rejected call clears every out-parameter it was given when that pointer is not NULL. */

typedef struct radicle_script_set radicle_script_set;

typedef struct radicle_script_set_candidate {
  const char* name;
  const char* path;
  radicle_asset_origin origin;
  /* Invariant: the loose payload path, or the archive file path for an archived payload. */
  const char* container;
  int32_t load_index;
  uint64_t size;
  radicle_asset_location location;
} radicle_script_set_candidate;

typedef struct radicle_script_set_winner {
  const char* name;
  radicle_script_set_candidate winner;
  uint32_t replaced;
} radicle_script_set_winner;

/* Requires root_load_index to hold radicle_assets_root_count indexes, or NULL with a count of zero for the high-to-low root order. */
RADICLE_API radicle_status radicle_script_set_resolve(const radicle_assets* assets,
                                                      const int32_t* root_load_index,
                                                      size_t root_load_index_count,
                                                      radicle_script_set** out_set);
RADICLE_API size_t radicle_script_set_count(const radicle_script_set* set);
/* Invariant: winners are ordered by name, and an index at or past the count is RADICLE_STATUS_INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_script_set_get(const radicle_script_set* set,
                                                  size_t index,
                                                  radicle_script_set_winner* out_winner);
/* Invariant: reads the winner's own asset location, so a load index that contradicts the root order still reads its bytes. */
/* Invariant: an empty winner payload is OK with *out_bytes NULL and *out_size 0, and radicle_free_bytes(NULL) is a no-op. */
/* Invariant: a payload that cannot be read is RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_script_set_read(const radicle_script_set* set,
                                                   size_t index,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);
RADICLE_API void radicle_script_set_destroy(radicle_script_set* set);

#ifdef __cplusplus
}
#endif
