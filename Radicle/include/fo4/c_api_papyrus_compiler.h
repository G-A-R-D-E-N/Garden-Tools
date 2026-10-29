#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: all-zero options compile with debug info and debug-only and beta-only code; imports is NULL or a ';'-separated list of root directories. */
typedef struct radicle_papyrus_compile_options {
  int32_t release;
  int32_t no_debug_info;
  const char* imports;
} radicle_papyrus_compile_options;

/* Invariant: a source error is a row with status OK, never an error status; out_pex is non-NULL only when no row has severity error. */
/* Requires radicle_free_string_array and radicle_pex_destroy. */
RADICLE_API radicle_status radicle_papyrus_compile_source(const uint8_t* source,
                                                          size_t size,
                                                          const char* file_name,
                                                          radicle_papyrus_compile_options options,
                                                          radicle_pex** out_pex,
                                                          char*** diag_rows,
                                                          size_t* diag_count);

/* Requires radicle_free_bytes. A nonzero assembly prints the listing instead of source. */
RADICLE_API radicle_status radicle_papyrus_decompile(const radicle_pex* pex,
                                                     int32_t assembly,
                                                     uint8_t** out_text,
                                                     size_t* out_size);

#ifdef __cplusplus
}
#endif
