#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: text must not be NULL; a position outside the text is out_offset -1 with status OK. */
RADICLE_API radicle_status radicle_papyrus_line_column_to_offset(const uint8_t* text,
                                                                 size_t size,
                                                                 int32_t line,
                                                                 int32_t column,
                                                                 int32_t* out_offset);

/* Invariant: source is a .psc path or a folder on disk; a failure is a RESULT line in the text, not an error status. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_check(const char* source,
                                                 int32_t semantic,
                                                 const char* imports,
                                                 const char* const* base_imports,
                                                 size_t base_count,
                                                 uint8_t** out_text,
                                                 size_t* out_size);

/* Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_outline(const char* source,
                                                   uint8_t** out_text,
                                                   size_t* out_size);

/* Invariant: a position that resolves to nothing is a RESULT: not resolved text with status OK. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_definition(const char* source,
                                                      int32_t line,
                                                      int32_t column,
                                                      const char* imports,
                                                      const char* const* base_imports,
                                                      size_t base_count,
                                                      uint8_t** out_text,
                                                      size_t* out_size);

/* Invariant: text is the buffer analysed, file_path may be NULL, and a parse failure is an {"error"} JSON object with status OK. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_analyze_json(const uint8_t* text,
                                                        size_t size,
                                                        const char* file_path,
                                                        uint8_t** out_text,
                                                        size_t* out_size);

/* Invariant: an unresolved offset is {"resolved":false} with status OK. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_symbol_at_json(const uint8_t* text,
                                                          size_t size,
                                                          const char* file_path,
                                                          int32_t offset,
                                                          const char* imports,
                                                          const char* const* base_imports,
                                                          size_t base_count,
                                                          uint8_t** out_text,
                                                          size_t* out_size);

/* Invariant: text is the unsaved buffer and offset must lie inside it, else INVALID_ARGUMENT. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_completion_json(const uint8_t* text,
                                                           size_t size,
                                                           const char* file_path,
                                                           int32_t offset,
                                                           const char* imports,
                                                           const char* const* base_imports,
                                                           size_t base_count,
                                                           uint8_t** out_text,
                                                           size_t* out_size);

/* Invariant: text is the unsaved buffer and offset must lie inside it, else INVALID_ARGUMENT. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_signature_help_json(const uint8_t* text,
                                                               size_t size,
                                                               const char* file_path,
                                                               int32_t offset,
                                                               const char* imports,
                                                               const char* const* base_imports,
                                                               size_t base_count,
                                                               uint8_t** out_text,
                                                               size_t* out_size);

/* Invariant: text is the unsaved buffer and revision is echoed unchanged. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_check_text_json(const uint8_t* text,
                                                           size_t size,
                                                           const char* file_path,
                                                           int32_t revision,
                                                           const char* imports,
                                                           const char* const* base_imports,
                                                           size_t base_count,
                                                           uint8_t** out_text,
                                                           size_t* out_size);

/* Invariant: a NULL or empty output_path writes to the source's folder; a nonzero release drops DebugOnly and BetaOnly code. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_papyrus_compile_path(const char* source,
                                                        const char* output_path,
                                                        const char* imports,
                                                        int32_t release,
                                                        int32_t no_debug_info,
                                                        const char* flag_file,
                                                        const char* const* base_imports,
                                                        size_t base_count,
                                                        uint8_t** out_text,
                                                        size_t* out_size);

/* Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_papyrus_natural_roots(const char* folder,
                                                         char*** out_roots,
                                                         size_t* out_count);

#ifdef __cplusplus
}
#endif
