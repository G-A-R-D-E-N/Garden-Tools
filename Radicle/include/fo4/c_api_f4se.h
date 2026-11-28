#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: plugin_json is one UTF-8 plugin binding object, existing_json is NULL or an object of relative path to previously emitted text, and validation errors are a success:false RESULT with status OK. Requires radicle_free_bytes; an empty output is returned as NULL with size 0. */
RADICLE_API radicle_status radicle_f4se_emit(const char* plugin_json,
                                             const char* existing_json,
                                             int emit_despite_errors,
                                             uint8_t** out_text,
                                             size_t* out_size);

/* Invariant: same inputs as radicle_f4se_emit, and the result adds a roundTrip object comparing the emitted C++ registrations with the emitted Papyrus declarations. Requires radicle_free_bytes. */
RADICLE_API radicle_status radicle_f4se_round_trip(const char* plugin_json,
                                                   const char* existing_json,
                                                   int emit_despite_errors,
                                                   uint8_t** out_text,
                                                   size_t* out_size);

#ifdef __cplusplus
}
#endif
