#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_loose_plugins radicle_loose_plugins;

RADICLE_API radicle_status radicle_loose_plugins_create(radicle_loose_plugins** out_plugins);
RADICLE_API void radicle_loose_plugins_destroy(radicle_loose_plugins* plugins);
RADICLE_API radicle_status radicle_loose_plugins_replace(radicle_loose_plugins* plugins, const char* name, radicle_plugin* plugin);
RADICLE_API radicle_status radicle_loose_plugins_release(radicle_loose_plugins* plugins, const char* name);
RADICLE_API radicle_status radicle_loose_plugins_set_editable(radicle_loose_plugins* plugins, const char* name, radicle_plugin* plugin);
RADICLE_API radicle_status radicle_loose_plugins_get(radicle_loose_plugins* plugins, const char* name, radicle_plugin** out_plugin);
RADICLE_API radicle_status radicle_loose_plugins_index(radicle_loose_plugins* plugins, const char* name);
RADICLE_API radicle_status radicle_loose_plugins_has_index(radicle_loose_plugins* plugins, const char* name, int32_t* out_has_index);

#ifdef __cplusplus
}
#endif
