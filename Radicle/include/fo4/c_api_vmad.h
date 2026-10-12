#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_plugin_set_vmad_object(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, radicle_form_key target, int16_t alias);
RADICLE_API radicle_status radicle_plugin_set_vmad_string(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, const char* value);
RADICLE_API radicle_status radicle_plugin_set_vmad_int(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, int32_t value);
RADICLE_API radicle_status radicle_plugin_set_vmad_float(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, float value);
RADICLE_API radicle_status radicle_plugin_set_vmad_bool(radicle_plugin* plugin, radicle_form_key key, size_t script, size_t property, size_t element, int32_t value);

#ifdef __cplusplus
}
#endif
