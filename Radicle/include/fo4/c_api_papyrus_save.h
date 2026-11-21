#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a string returned here is released with radicle_papyrus_string_free. */
RADICLE_API void radicle_papyrus_string_free(char* text);

/* Invariant: the fingerprint changes when any instruction, state, function, property, variable or struct of the script changes. */
RADICLE_API radicle_status radicle_papyrus_script_fingerprint(const radicle_pex* pex, const char* script, char** out_fingerprint);

/* Invariant: the capture is sorted and its JSON text is stable, so the digest is stable for the same state. */
RADICLE_API radicle_status radicle_papyrus_save_capture(const radicle_papyrus_runtime* runtime, char** out_json, char** out_digest);

/* Invariant: a thread is resumed only when its fingerprint matches; a latent wait and an absent script are rejected. */
RADICLE_API radicle_status radicle_papyrus_save_restore(const char* json, const char* const* scripts,
                                                        const char* const* fingerprints, size_t script_count,
                                                        char** out_report, int32_t* out_resumed,
                                                        int32_t* out_rejected);

/* Requires: flags is zero or RADICLE_PAPYRUS_STATE_SKIP_UNKNOWN. */
#define RADICLE_PAPYRUS_STATE_SKIP_UNKNOWN 1u

/* Requires: struct_size is sizeof(radicle_papyrus_state_capture) before the call. */
typedef struct radicle_papyrus_state_capture {
  size_t struct_size;
  char* blob;
  char* digest;
  uint64_t* engine_handles;
  size_t engine_handle_count;
} radicle_papyrus_state_capture;

/* Invariant: an engine handle (bit 63) is session-local, so restore maps each saved handle to a live one. */
typedef struct radicle_papyrus_engine_handle_map {
  uint64_t saved;
  uint64_t live;
} radicle_papyrus_engine_handle_map;

/* Requires: struct_size is sizeof(radicle_papyrus_state_report) before the call. */
typedef struct radicle_papyrus_state_report {
  size_t struct_size;
  int32_t instances;
  int32_t variables;
  int32_t skipped_instances;
  int32_t skipped_variables;
  int32_t unbound_references;
  char* report;
} radicle_papyrus_state_report;

/* Invariant: instances are identified by owner and script, never by runtime id; two with one identity refuse capture. */
RADICLE_API radicle_status radicle_papyrus_runtime_state_capture(const radicle_papyrus_runtime* runtime, radicle_papyrus_state_capture* out_capture);
RADICLE_API void radicle_papyrus_state_capture_destroy(radicle_papyrus_state_capture* capture);

/* Invariant: all or nothing; PARSE_ERROR is a bad blob, NOT_FOUND an unknown target, INVALID_ARGUMENT a type mismatch or a step in progress. */
RADICLE_API radicle_status radicle_papyrus_runtime_state_restore(radicle_papyrus_runtime* runtime, const char* blob, size_t blob_size, uint32_t flags, const radicle_papyrus_engine_handle_map* engine_map, size_t engine_map_count, radicle_papyrus_state_report* out_report);
RADICLE_API void radicle_papyrus_state_report_destroy(radicle_papyrus_state_report* report);

/* Invariant: only the identity_count instances named by owner kind, base key, discriminator and script are restored; every other instance is left untouched and uncounted, and a named identity the blob does not hold restores nothing. */
/* Requires: the four identity arrays hold identity_count entries each (null with 0); an unusable owner is INVALID_ARGUMENT; the rest is as radicle_papyrus_runtime_state_restore. */
RADICLE_API radicle_status radicle_papyrus_runtime_state_restore_instances(radicle_papyrus_runtime* runtime, const char* blob, size_t blob_size, uint32_t flags, const radicle_papyrus_engine_handle_map* engine_map, size_t engine_map_count, const radicle_papyrus_owner_kind* owner_kinds, const char* const* base_form_keys, const char* const* discriminators, const char* const* scripts, size_t identity_count, radicle_papyrus_state_report* out_report);

#ifdef __cplusplus
}
#endif
