#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a larger source is refused before it is read. */
#define RADICLE_PAPYRUS_SOURCE_MAX_BYTES (4u * 1024u * 1024u)

/* Invariant: a parse failure is a row, not an error status. */
/* Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_papyrus_source_summary(const uint8_t* source,
                                                          size_t size,
                                                          char*** out_rows,
                                                          size_t* out_count);

#ifdef __cplusplus
}
#endif
