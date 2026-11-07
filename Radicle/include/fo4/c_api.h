#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(RADICLE_C_BUILD)
    #define RADICLE_API __declspec(dllexport)
  #else
    #define RADICLE_API __declspec(dllimport)
  #endif
#else
  #define RADICLE_API
#endif

#define RADICLE_ABI_VERSION 432u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_plugin radicle_plugin;

typedef enum radicle_status {
  RADICLE_STATUS_OK = 0,
  RADICLE_STATUS_INVALID_ARGUMENT = 1,
  RADICLE_STATUS_IO_ERROR = 2,
  RADICLE_STATUS_PARSE_ERROR = 3,
  /* Returned by the environment lookups in c_api_env.h. */
  RADICLE_STATUS_NOT_FOUND = 4,
  RADICLE_STATUS_OUT_OF_MEMORY = 5,
  RADICLE_STATUS_INTERNAL_ERROR = 255
} radicle_status;

typedef enum radicle_plugin_kind {
  RADICLE_PLUGIN_FULL = 0,
  RADICLE_PLUGIN_LIGHT = 1
} radicle_plugin_kind;

typedef struct radicle_master_descriptor {
  const char* filename;
  radicle_plugin_kind kind;
} radicle_master_descriptor;

RADICLE_API uint32_t radicle_abi_version(void);

/*
 * Handles are owned by the caller and must be released with radicle_plugin_destroy.
 * Byte and string arrays returned by this API are owned by the caller and must be
 * released with the matching radicle_free_* function.
 *
 * A radicle_plugin handle is not thread-safe. Different handles may be used from
 * different threads. radicle_last_error is thread-local and remains valid until
 * the next Radicle C API call on the same thread.
 */
RADICLE_API radicle_status radicle_plugin_read_file(const char* path,
                                                     radicle_plugin** out_plugin);
RADICLE_API void radicle_plugin_destroy(radicle_plugin* plugin);

/*
 * Writes the plugin to `path`. On any failure (including a partially failed
 * write) the pre-existing file at `path` is left untouched: the payload is
 * staged in a sibling temporary file and renamed into place. Returns
 * RADICLE_STATUS_OK, or an error status with details in radicle_last_error().
 */
RADICLE_API radicle_status radicle_plugin_write_file(const radicle_plugin* plugin,
                                                       const char* path);
/*
 * Serializes the plugin. A plugin whose bytes are empty returns OK with
 * *out_bytes == NULL and *out_size == 0; FFI consumers must treat that pair
 * as "zero-length output", not as an error.
 */
RADICLE_API radicle_status radicle_plugin_write_bytes(const radicle_plugin* plugin,
                                                        uint8_t** out_bytes,
                                                        size_t* out_size);
RADICLE_API void radicle_free_bytes(uint8_t* bytes);

RADICLE_API radicle_status radicle_plugin_stamp_master_kinds(
    radicle_plugin* plugin,
    const char* const* master_paths,
    size_t master_count);

RADICLE_API radicle_status radicle_plugin_remap_masters(
    radicle_plugin* plugin,
    const radicle_master_descriptor* masters,
    size_t master_count);

RADICLE_API radicle_status radicle_plugin_set_author(radicle_plugin* plugin, const char* author);
RADICLE_API radicle_status radicle_plugin_set_description(radicle_plugin* plugin,
                                                           const char* description);
RADICLE_API radicle_status radicle_plugin_set_master_flag(radicle_plugin* plugin, int32_t master);
RADICLE_API radicle_status radicle_plugin_set_localized_flag(radicle_plugin* plugin,
                                                             int32_t localized);

RADICLE_API radicle_status radicle_plugin_collect_form_keys(
    const radicle_plugin* plugin,
    char*** out_strings,
    size_t* out_count);

RADICLE_API radicle_status radicle_plugin_collect_form_links(
    const radicle_plugin* plugin,
    char*** out_strings,
    size_t* out_count);

RADICLE_API void radicle_free_string_array(char** strings, size_t count);
RADICLE_API const char* radicle_last_error(void);

#ifdef __cplusplus
}
#endif
