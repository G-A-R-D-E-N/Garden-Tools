#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_plugin_add_subrecord(radicle_plugin* plugin,
                                                        radicle_form_key key,
                                                        const char* signature);
RADICLE_API radicle_status radicle_plugin_remove_subrecord(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const char* signature,
                                                           size_t occurrence);
RADICLE_API radicle_status radicle_plugin_create_record(radicle_plugin* plugin,
                                                        const char* signature,
                                                        const char* editor_id_utf8,
                                                        radicle_form_key* out_key);
/* Constraint: rows fill the reserved localized rows in STRINGS, DLSTRINGS, ILSTRINGS order, string_count must equal that count, and a mismatch leaves the plugin unchanged; tables are written under data_dir. */
RADICLE_API radicle_status radicle_plugin_create_record_with_string_rows(radicle_plugin* plugin,
                                                                        const char* signature,
                                                                        const char* editor_id_utf8,
                                                                        const char* const* strings,
                                                                        size_t string_count,
                                                                        const char* data_dir_utf8,
                                                                        const char* language_utf8,
                                                                        radicle_form_key* out_key);

/* Requires: an empty top-level record with no schema check; form_version 0 keeps the plugin form version, and CELL, REFR, LAND and other nested types must be refused. */
RADICLE_API radicle_status radicle_plugin_create_raw_record(radicle_plugin* plugin,
                                                            const char* signature,
                                                            uint16_t form_version,
                                                            radicle_form_key* out_key);
/* Requires: the payload is stored unvalidated; occurrence equal to the current count appends at the record end, and a larger one must report NOT_FOUND. */
RADICLE_API radicle_status radicle_plugin_set_subrecord_bytes(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              const char* signature,
                                                              size_t occurrence,
                                                              const uint8_t* bytes,
                                                              size_t size);
/* Requires: the record exists; its payload is then written as declared_size followed by the caller bytes verbatim (a broken zlib stream stays broken), the compressed flag is set, and its subrecords are ignored on write. */
RADICLE_API radicle_status radicle_plugin_set_record_raw_compressed_payload(radicle_plugin* plugin,
                                                                            radicle_form_key key,
                                                                            uint32_t declared_size,
                                                                            const uint8_t* bytes,
                                                                            size_t size);

#ifdef __cplusplus
}
#endif
