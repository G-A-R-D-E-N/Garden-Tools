#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The .pex C ABI.
 *
 * Handles are owned by the caller and released with radicle_pex_destroy;
 * destroying NULL is a no-op. Every byte range this API hands back is a
 * length-delimited copy taken verbatim from the container: the format stores
 * one byte per character and this layer never transcodes. Latin-1 is the
 * reading the C++ and C# layers apply to those bytes; any other character
 * encoding is UNPROVEN. Buffers returned through out_bytes are owned by the
 * caller and released with radicle_free_bytes from fo4/c_api.h.
 *
 * A radicle_pex handle is not thread-safe. Different handles may be used from
 * different threads. radicle_last_error is shared with the rest of the C ABI.
 */

typedef struct radicle_pex radicle_pex;

typedef struct radicle_pex_header {
  uint8_t major_version;
  uint8_t minor_version;
  uint16_t game_id;
  int64_t compilation_time;
  size_t string_count;
  size_t object_count;
} radicle_pex_header;

typedef struct radicle_pex_object_counts {
  size_t struct_count;
  size_t variable_count;
  size_t property_count;
  size_t state_count;
  size_t function_count;
} radicle_pex_object_counts;

/*
 * Parses `bytes`. The handle keeps everything it needs, so the caller may
 * release the input as soon as the call returns. Malformed input returns
 * RADICLE_STATUS_PARSE_ERROR with the reader's message in radicle_last_error.
 */
RADICLE_API radicle_status radicle_pex_parse(const uint8_t* bytes,
                                              size_t size,
                                              radicle_pex** out_pex);

RADICLE_API radicle_status radicle_pex_read_file(const char* path,
                                                  radicle_pex** out_pex);

RADICLE_API void radicle_pex_destroy(radicle_pex* pex);

/*
 * Serializes the handle. As with radicle_plugin_write_bytes, a handle with no
 * bytes returns OK with *out_bytes == NULL and *out_size == 0.
 */
RADICLE_API radicle_status radicle_pex_write_bytes(const radicle_pex* pex,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

RADICLE_API radicle_status radicle_pex_get_header(const radicle_pex* pex,
                                                   radicle_pex_header* out_header);

RADICLE_API radicle_status radicle_pex_object_get_counts(const radicle_pex* pex,
                                                          size_t index,
                                                          radicle_pex_object_counts* out_counts);

RADICLE_API radicle_status radicle_pex_object_name(const radicle_pex* pex,
                                                    size_t index,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

RADICLE_API radicle_status radicle_pex_object_parent_name(const radicle_pex* pex,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_pex_string_table_entry(const radicle_pex* pex,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

#ifdef __cplusplus
}
#endif
