#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_duplicate_formid_scanner radicle_duplicate_formid_scanner;

RADICLE_API radicle_status radicle_duplicate_formid_scanner_create(
    radicle_duplicate_formid_scanner** out_scanner);
RADICLE_API void radicle_duplicate_formid_scanner_destroy(
    radicle_duplicate_formid_scanner* scanner);
RADICLE_API radicle_status radicle_duplicate_formid_scanner_invalidate(
    radicle_duplicate_formid_scanner* scanner,
    const char* path);
/* Rows are "<8-hex rawFormId>\t<count>\t<sig1>,<sig2>,..." and must be freed with radicle_free_string_array. */
RADICLE_API radicle_status radicle_duplicate_formid_scanner_scan(
    radicle_duplicate_formid_scanner* scanner,
    const char* path,
    char*** out_rows,
    size_t* out_count);
RADICLE_API radicle_status radicle_duplicate_formid_scan_file(
    const char* path,
    char*** out_rows,
    size_t* out_count);
RADICLE_API radicle_status radicle_duplicate_formid_scan_bytes(
    const uint8_t* bytes,
    size_t size,
    char*** out_rows,
    size_t* out_count);

#ifdef __cplusplus
}
#endif
