#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A runtime must own the interpreter and the movies it creates, so closing it must destroy them all. */
typedef struct radicle_runtime radicle_runtime;
typedef struct radicle_movie radicle_movie;
typedef struct radicle_runtime_snapshot radicle_runtime_snapshot;

/* A host callback must return its buffer as malloc'd memory the runtime releases with
   radicle_free_bytes; RADICLE_STATUS_NOT_FOUND must mean the asset or key is simply absent. */
typedef struct radicle_runtime_host {
  void* user;
  radicle_status (*resolve_asset)(void* user, const char* path, uint8_t** out_bytes,
                                  size_t* out_size);
  radicle_status (*read_storage)(void* user, const char* name, uint8_t** out_bytes,
                                 size_t* out_size);
  radicle_status (*write_storage)(void* user, const char* name, const uint8_t* bytes, size_t size);
  /* The runtime must free *out_text with radicle_free_bytes after copying the answer. */
  radicle_status (*host_call)(void* user, const char* method, const char* const* arguments,
                              size_t argument_count, uint8_t** out_text, size_t* out_size);
} radicle_runtime_host;

typedef struct radicle_runtime_matrix {
  double a;
  double b;
  double c;
  double d;
  double translate_x;
  double translate_y;
} radicle_runtime_matrix;

typedef struct radicle_runtime_glyph {
  uint16_t font_id;
  uint16_t glyph_id;
  double x;
  double advance;
} radicle_runtime_glyph;

typedef struct radicle_runtime_run {
  uint16_t font_id;
  double font_size;
  double x;
  double y;
  uint32_t color;
  size_t glyph_count;
  const radicle_runtime_glyph* glyphs; /* must stay valid until radicle_runtime_snapshot_destroy */
} radicle_runtime_run;

typedef struct radicle_runtime_entry {
  uint8_t kind; /* 0 display object, 1 shape, 2 bitmap, 3 text */
  uint32_t character_id;
  int32_t clip_depth;
  uint8_t blend_mode;
  uint8_t visible;
  double alpha;
  const char* path; /* must stay valid until the snapshot is destroyed */
  const char* name;
  radicle_runtime_matrix matrix;
  double colour[8]; /* red/green/blue/alpha multipliers then offsets */
  size_t run_count;
  const radicle_runtime_run* runs; /* must stay valid until the snapshot is destroyed */
} radicle_runtime_entry;

typedef struct radicle_runtime_sound {
  uint8_t kind; /* 0 play, 1 stop, 2 volume */
  uint16_t sound_id;
  double volume;
  uint32_t loops;
} radicle_runtime_sound;

typedef enum radicle_input_kind {
  RADICLE_INPUT_MOUSE_MOVE = 0,
  RADICLE_INPUT_MOUSE_DOWN = 1,
  RADICLE_INPUT_MOUSE_UP = 2,
  RADICLE_INPUT_KEY_DOWN = 3,
  RADICLE_INPUT_KEY_UP = 4,
  RADICLE_INPUT_CHAR = 5,
  RADICLE_INPUT_WHEEL = 6,
} radicle_input_kind;

/* A host replay must feed inputs in nondecreasing time_ms order so the run stays deterministic. */
typedef struct radicle_runtime_input {
  double time_ms;
  uint8_t kind; /* radicle_input_kind */
  double x;
  double y;
  int32_t button;
  uint32_t key_code;
  int32_t delta;
} radicle_runtime_input;

RADICLE_API radicle_status radicle_runtime_open(const radicle_runtime_host* host,
                                                radicle_runtime** out_runtime);
RADICLE_API void radicle_runtime_close(radicle_runtime* runtime);
RADICLE_API radicle_status radicle_runtime_open_movie(radicle_runtime* runtime,
                                                      const uint8_t* bytes, size_t size,
                                                      radicle_movie** out_movie);
RADICLE_API radicle_status radicle_runtime_open_movie_from_ba2(radicle_runtime* runtime,
                                                               const radicle_ba2* ba2,
                                                               const char* path,
                                                               radicle_movie** out_movie);
RADICLE_API void radicle_movie_close(radicle_movie* movie);
/* The host must advance with its own clock; equal dt sequences must give equal snapshots. */
RADICLE_API radicle_status radicle_movie_advance(radicle_movie* movie, double dt_seconds);
RADICLE_API radicle_status radicle_movie_input(radicle_movie* movie,
                                               const radicle_runtime_input* input);
RADICLE_API radicle_status radicle_movie_snapshot(radicle_movie* movie,
                                                  radicle_runtime_snapshot** out_snapshot);
RADICLE_API void radicle_runtime_snapshot_destroy(radicle_runtime_snapshot* snapshot);
RADICLE_API size_t radicle_runtime_snapshot_entry_count(const radicle_runtime_snapshot* snapshot);
RADICLE_API radicle_status radicle_runtime_snapshot_entry_at(
    const radicle_runtime_snapshot* snapshot, size_t index, radicle_runtime_entry* out_entry);
RADICLE_API size_t radicle_runtime_snapshot_sound_count(const radicle_runtime_snapshot* snapshot);
RADICLE_API radicle_status radicle_runtime_snapshot_sound_at(
    const radicle_runtime_snapshot* snapshot, size_t index, radicle_runtime_sound* out_sound);
RADICLE_API uint64_t radicle_runtime_snapshot_hash(const radicle_runtime_snapshot* snapshot);
/* The replay must apply per-frame inputs before advancing and must report the full frame count. */
RADICLE_API radicle_status radicle_movie_replay(radicle_movie* movie,
                                                const radicle_runtime_input* inputs,
                                                size_t input_count, double frame_dt_seconds,
                                                size_t frame_count, uint64_t* out_hashes,
                                                size_t hash_capacity, size_t* out_hash_count);

#ifdef __cplusplus
}
#endif
