#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owns a parsed save header and a copy of its screenshot; released with radicle_save_header_free. */
typedef struct radicle_save_header radicle_save_header;

/* Invariant: every pointer is borrowed and valid until radicle_save_header_free; text is UTF-8 and ends at its first NUL. */
typedef struct radicle_save_header_info {
  uint32_t version;
  uint32_t save_number;
  const char* player_name;
  uint32_t level;
  const char* location;
  const char* playtime;
  const char* race;
  uint16_t sex;
  float current_xp;
  float required_xp;
  uint64_t filetime; /* raw 100 ns ticks since 1601-01-01 */
  uint32_t screenshot_width;
  uint32_t screenshot_height;
  const uint8_t* screenshot; /* 4 bytes per pixel as stored; NULL when screenshot_size is 0 */
  size_t screenshot_size;
  uint8_t form_version;
  const char* game_version;
  size_t plugin_count;
  size_t light_plugin_count;
} radicle_save_header_info;

/* Reads only the header; `bytes` may be NULL only when size is 0 and may be freed on return. Any failure leaves *out NULL, with PARSE_ERROR for bytes that are not a supported save and the reason in radicle_last_error. */
RADICLE_API radicle_status radicle_save_header_parse(const uint8_t* bytes,
                                                     size_t size,
                                                     radicle_save_header** out);

RADICLE_API radicle_status radicle_save_header_get_info(const radicle_save_header* header,
                                                        radicle_save_header_info* out_info);

/* `kind` selects the full or the light list; an index past its count is INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_save_header_plugin(const radicle_save_header* header,
                                                      radicle_plugin_kind kind,
                                                      size_t index,
                                                      const char** out_name);

/* Accepts NULL. */
RADICLE_API void radicle_save_header_free(radicle_save_header* header);

#ifdef __cplusplus
}
#endif
