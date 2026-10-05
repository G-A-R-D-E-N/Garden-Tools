#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The .seq C ABI.
 *
 * A .seq is a bare array of little-endian uint32 runtime FormIDs: no header, no count
 * and no magic. Entries are handed back exactly as stored and are never resolved to a
 * FormKey, because that needs a load order the container does not carry. The evidence
 * and the rules that stay UNPROVEN are recorded in Radicle/docs/SEQ_FORMAT.md.
 *
 * Handles are owned by the caller and released with radicle_seq_destroy; destroying
 * NULL is a no-op. Buffers returned through out_bytes are owned by the caller and
 * released with radicle_free_bytes from fo4/c_api.h. A radicle_seq handle is not
 * thread-safe. radicle_last_error is shared with the rest of the C ABI.
 */

typedef struct radicle_seq radicle_seq;

/*
 * Parses `bytes`. The handle keeps everything it needs, so the caller may release the
 * input as soon as the call returns. A byte count that is not a multiple of four
 * returns RADICLE_STATUS_PARSE_ERROR with the reader's message in radicle_last_error.
 */
RADICLE_API radicle_status radicle_seq_parse(const uint8_t* bytes,
                                              size_t size,
                                              radicle_seq** out_seq);

RADICLE_API radicle_status radicle_seq_read_file(const char* path,
                                                  radicle_seq** out_seq);

RADICLE_API void radicle_seq_destroy(radicle_seq* seq);

RADICLE_API radicle_status radicle_seq_write_bytes(const radicle_seq* seq,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

RADICLE_API radicle_status radicle_seq_entry_count(const radicle_seq* seq,
                                                    size_t* out_count);

RADICLE_API radicle_status radicle_seq_entry_form_id(const radicle_seq* seq,
                                                      size_t index,
                                                      uint32_t* out_form_id);

#ifdef __cplusplus
}
#endif
