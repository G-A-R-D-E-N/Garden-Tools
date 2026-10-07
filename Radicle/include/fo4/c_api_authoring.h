#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* signature is a NUL-terminated four-character subrecord signature. Returns OK (also when already present), NOT_FOUND (no such record), INVALID_ARGUMENT (not authorable, unknown layout order, no registered default). */
RADICLE_API radicle_status radicle_plugin_add_subrecord(radicle_plugin* plugin,
                                                        radicle_form_key key,
                                                        const char* signature);
/* Removes the occurrence-th physical subrecord with that signature. NOT_FOUND when the record or occurrence is absent; INVALID_ARGUMENT when the signature is not authorable or removal would go below the schema minimum. */
RADICLE_API radicle_status radicle_plugin_remove_subrecord(radicle_plugin* plugin,
                                                           radicle_form_key key,
                                                           const char* signature,
                                                           size_t occurrence);
/* Creates a local record holding EDID plus every required subrecord at its registered default. INVALID_ARGUMENT when the signature has no authoring schema or default, or the editor ID cannot be stored in Windows-1252. */
RADICLE_API radicle_status radicle_plugin_create_record(radicle_plugin* plugin,
                                                        const char* signature,
                                                        const char* editor_id_utf8,
                                                        radicle_form_key* out_key);

#ifdef __cplusplus
}
#endif
