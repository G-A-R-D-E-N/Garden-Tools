#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a string returned here is released with radicle_papyrus_string_free. */
RADICLE_API void radicle_papyrus_string_free(char* text);

/* Invariant: the fingerprint changes when any instruction, state, function, property, variable or struct of the script changes. */
RADICLE_API radicle_status radicle_papyrus_script_fingerprint(const radicle_pex* pex, const char* script, char** out_fingerprint);

/* Invariant: the capture is sorted and its JSON text is stable, so the digest is stable for the same state. */
RADICLE_API radicle_status radicle_papyrus_save_capture(const radicle_papyrus_runtime* runtime, char** out_json, char** out_digest);

/* Invariant: a thread is resumed only when its fingerprint matches; a latent wait and an absent script are rejected. */
RADICLE_API radicle_status radicle_papyrus_save_restore(const char* json, const char* const* scripts,
                                                        const char* const* fingerprints, size_t script_count,
                                                        char** out_report, int32_t* out_resumed,
                                                        int32_t* out_rejected);

#ifdef __cplusplus
}
#endif
