#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: imports is a semicolon-separated root list or NULL, and base_imports holds base_count non-null UTF-8 roots. */

/* Requires radicle_free_bytes; a graph that will not load is a RESULT: FAILED text with status OK. */
RADICLE_API radicle_status radicle_graph_validate(const char* graph_path,
                                                  const char* imports,
                                                  const char* const* base_imports,
                                                  size_t base_count,
                                                  uint8_t** out_text,
                                                  size_t* out_size);

/* Requires radicle_free_bytes; a NULL or empty output_path writes nothing, and a '..' segment is refused as hygiene, not containment. */
RADICLE_API radicle_status radicle_graph_compile(const char* graph_path,
                                                 const char* output_path,
                                                 const char* imports,
                                                 int source_only,
                                                 const char* const* base_imports,
                                                 size_t base_count,
                                                 uint8_t** out_text,
                                                 size_t* out_size);

/* Requires radicle_free_bytes; a limit of 0 or below means the default of 30. */
RADICLE_API radicle_status radicle_graph_palette_search(const char* query,
                                                        const char* imports,
                                                        int limit,
                                                        const char* const* base_imports,
                                                        size_t base_count,
                                                        uint8_t** out_text,
                                                        size_t* out_size);

/* Requires radicle_free_bytes; an unknown node type is a RESULT: FAILED text with status OK. */
RADICLE_API radicle_status radicle_graph_describe_node(const char* node_type,
                                                       const char* imports,
                                                       const char* const* base_imports,
                                                       size_t base_count,
                                                       uint8_t** out_text,
                                                       size_t* out_size);

/* Requires radicle_free_bytes; a script that will not load or lift is PARSE_ERROR with its reason and no text. */
RADICLE_API radicle_status radicle_graph_load_script(const char* script_path,
                                                     const char* imports,
                                                     const char* const* base_imports,
                                                     size_t base_count,
                                                     uint8_t** out_text,
                                                     size_t* out_size);

#ifdef __cplusplus
}
#endif
