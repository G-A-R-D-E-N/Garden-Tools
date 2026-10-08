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

#ifdef __cplusplus
}
#endif
