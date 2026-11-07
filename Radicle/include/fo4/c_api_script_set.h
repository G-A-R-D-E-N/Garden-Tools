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

typedef enum radicle_archive_policy {
  RADICLE_ARCHIVE_POLICY_FAIL = 0,
  RADICLE_ARCHIVE_POLICY_SKIP = 1
} radicle_archive_policy;

typedef struct radicle_script_set_failure_info {
  size_t archive_index;
  const char* path;
  const char* message;
} radicle_script_set_failure_info;

/* Invariant: an open set owns its assets, so the caller keeps no radicle_assets handle; roots are ordered high to low priority and archives low to high. */
/* Invariant: empty and duplicate roots are dropped as radicle_assets_open_roots drops them, and a nameless archive reveals only paths another source names. */
/* Invariant: under RADICLE_ARCHIVE_POLICY_FAIL the first unreadable archive fails the open with its status; under SKIP it is dropped and reported by radicle_script_set_failure. */
/* Invariant: a skipped archive leaves the rest in their order, and archive_index counts the archive_paths the caller passed. */
RADICLE_API radicle_status radicle_script_set_open(const char* const* root_paths,
                                                   size_t root_count,
                                                   const char* const* archive_paths,
                                                   size_t archive_count,
                                                   radicle_archive_policy policy,
                                                   radicle_script_set** out_set);
RADICLE_API size_t radicle_script_set_failure_count(const radicle_script_set* set);
RADICLE_API radicle_status radicle_script_set_failure(const radicle_script_set* set,
                                                      size_t index,
                                                      radicle_script_set_failure_info* out_failure);
/* Invariant: the name is a Papyrus object name such as Quest:QF_Test, compared without case. */
/* Invariant: a missing parent, an unreadable payload and a cycle give *out_valid 0, an object without a parent gives 1, and the walk has no depth limit. */
RADICLE_API radicle_status radicle_script_set_parent_chain_valid(const radicle_script_set* set,
                                                                 const char* object_name,
                                                                 int32_t* out_valid);

typedef struct radicle_script_census radicle_script_census;

typedef struct radicle_script_census_totals {
  size_t object_count;
  size_t unreadable_count;
  size_t site_count;
  size_t script_call_count;
  size_t unresolved_count;
} radicle_script_census_totals;

typedef struct radicle_script_census_site {
  const char* script;
  const char* function;
  size_t instruction;
  int32_t is_static;
  const char* target;
  const char* called_function;
} radicle_script_census_site;

/* Invariant: a census visits every winner; a method call looks for its declaration up the parent chain and a native site is a call whose declaration is native. */
/* Invariant: a call with no declaration is reported by radicle_script_census_unresolved, and a call to a script function is only counted. */
RADICLE_API radicle_status radicle_script_set_census(const radicle_script_set* set, radicle_script_census** out_census);
RADICLE_API void radicle_script_census_destroy(radicle_script_census* census);
RADICLE_API radicle_status radicle_script_census_totals_get(const radicle_script_census* census,
                                                            radicle_script_census_totals* out_totals);
RADICLE_API radicle_status radicle_script_census_site_get(const radicle_script_census* census,
                                                          size_t index,
                                                          radicle_script_census_site* out_site);
RADICLE_API radicle_status radicle_script_census_unresolved(const radicle_script_census* census,
                                                            size_t index,
                                                            const char** out_text);
RADICLE_API radicle_status radicle_script_census_unreadable(const radicle_script_census* census,
                                                            size_t index,
                                                            const char** out_name);

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
