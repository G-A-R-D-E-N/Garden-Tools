#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: an opaque, immutable LOD session owns the plugin environment and the asset roots. */
typedef struct radicle_lod radicle_lod;

/* Constraint: paths must be newline separated (a trailing CR is tolerated) so no binding marshals arrays. */
/* Invariant: data_dir and archive_paths may be empty when no LOD settings archive is present. */
RADICLE_API radicle_status radicle_lod_open(const char* plugin_paths,
                                            const char* data_dir,
                                            const char* archive_paths,
                                            radicle_lod** out_lod);
/* Invariant: destroy accepts NULL so callers can clean up unconditionally. */
RADICLE_API void radicle_lod_destroy(radicle_lod* lod);

/* Invariant: out payloads are malloc'd, freed with radicle_free_bytes, and every output resets on entry; an empty output is returned as NULL with size 0. */
RADICLE_API radicle_status radicle_lod_worlds_json(const radicle_lod* lod,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

/* Invariant: planning is read-only; out_commit holds lines to merge only after a successful write. */
RADICLE_API radicle_status radicle_lod_plan_json(const radicle_lod* lod,
                                                 const char* ledger_text,
                                                 uint8_t** out_plan,
                                                 size_t* out_plan_size,
                                                 uint8_t** out_ledger,
                                                 size_t* out_ledger_size,
                                                 uint8_t** out_commit,
                                                 size_t* out_commit_size);

/* Invariant: merges commit lines into the ledger without a session, and returns the merged ledger text. */
RADICLE_API radicle_status radicle_lod_commit_json(const char* ledger_text,
                                                   const char* commit_text,
                                                   uint8_t** out_ledger,
                                                   size_t* out_ledger_size);

/* Invariant: the preview payload must be the exact NIF the terrain writer produces. */
RADICLE_API radicle_status radicle_lod_preview_nif(const radicle_lod* lod,
                                                   const char* world,
                                                   int32_t level,
                                                   int32_t x,
                                                   int32_t y,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

/* Invariant: the payload is the exact BTO generate writes for that tile, so a preview never differs from the bake. */
/* Invariant: a level and cell that holds no object tile returns RADICLE_STATUS_NOT_FOUND, never empty bytes. */
RADICLE_API radicle_status radicle_lod_preview_object_nif(const radicle_lod* lod,
                                                          const char* world,
                                                          int32_t level,
                                                          int32_t x,
                                                          int32_t y,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);

/* Invariant: candidates are the references that may enter object LOD, with a count per skip reason. */
RADICLE_API radicle_status radicle_lod_candidates_json(const radicle_lod* lod,
                                                       const char* world,
                                                       int32_t level,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);

/* Constraint: apply must be 0 for a dry run, so no caller can write tiles by leaving an argument out. */
/* Invariant: out_ledger only gains entries for tiles that were written successfully in this call. */
RADICLE_API radicle_status radicle_lod_generate_json(const radicle_lod* lod,
                                                     const char* ledger_text,
                                                     int32_t apply,
                                                     uint8_t** out_report,
                                                     size_t* out_report_size,
                                                     uint8_t** out_ledger,
                                                     size_t* out_ledger_size);

/* Invariant: a world-scoped run reports the world it baked, so a partial run never reads as the whole data folder. */
RADICLE_API radicle_status radicle_lod_generate_world_json(const radicle_lod* lod,
                                                           const char* world,
                                                           const char* ledger_text,
                                                           int32_t apply,
                                                           uint8_t** out_report,
                                                           size_t* out_report_size,
                                                           uint8_t** out_ledger,
                                                           size_t* out_ledger_size);

/* Invariant: a scoped run reports each finished tile through this struct, so no function pointer crosses the ABI as a parameter. */
/* Constraint: a level bound of 0 means the world's own range, so a zeroed scope scopes nothing away. */
typedef struct radicle_lod_scope {
  int32_t level_min;
  int32_t level_max;
  void (*progress)(void* user, const char* world, const char* kind, int32_t level, int32_t x, int32_t y,
                   size_t done, size_t total);
  void* user;
  int32_t* cancel;
} radicle_lod_scope;

/* Constraint: a NULL world or scope means every world and level, so an unscoped caller passes NULL twice. */
/* Invariant: a cancelled run writes and records only the tiles that already finished before the flag was seen. */
RADICLE_API radicle_status radicle_lod_generate_scoped_json(const radicle_lod* lod,
                                                           const char* ledger_text,
                                                           int32_t apply,
                                                           const char* world,
                                                           const radicle_lod_scope* scope,
                                                           uint8_t** out_report,
                                                           size_t* out_report_size,
                                                           uint8_t** out_ledger,
                                                           size_t* out_ledger_size);

/* Invariant: verification stats the local output, so a ledger entry alone never counts as present. */
RADICLE_API radicle_status radicle_lod_verify_json(const radicle_lod* lod,
                                                   const char* ledger_text,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

RADICLE_API radicle_status radicle_lod_verify_world_json(const radicle_lod* lod,
                                                         const char* world,
                                                         const char* ledger_text,
                                                         uint8_t** out_bytes,
                                                         size_t* out_size);

/* Invariant: decodes one 16-byte lodsettings record without a session; a record of another size or with an unencodable stride or level range returns RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_lod_settings_read(const uint8_t* bytes,
                                                     size_t size,
                                                     int32_t* out_southwest_x,
                                                     int32_t* out_southwest_y,
                                                     int32_t* out_stride,
                                                     int32_t* out_level_min,
                                                     int32_t* out_level_max);

#ifdef __cplusplus
}
#endif
