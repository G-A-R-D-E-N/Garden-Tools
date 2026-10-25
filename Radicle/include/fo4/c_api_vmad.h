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
RADICLE_API radicle_status radicle_plugin_set_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint16_t stage, int32_t stage_index, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_remove_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint16_t stage, int32_t stage_index);
RADICLE_API radicle_status radicle_plugin_add_vmad(radicle_plugin* plugin, radicle_form_key key);
RADICLE_API radicle_status radicle_plugin_add_vmad_script(radicle_plugin* plugin, radicle_form_key key, size_t index, const char* script_name, uint8_t flags, size_t* out_script_index);
RADICLE_API radicle_status radicle_plugin_remove_vmad_script(radicle_plugin* plugin, radicle_form_key key, size_t script_index);
RADICLE_API radicle_status radicle_plugin_add_vmad_property(radicle_plugin* plugin, radicle_form_key key, size_t script_index, size_t index, const char* name, uint8_t type, uint8_t flags, size_t* out_property_index);
RADICLE_API radicle_status radicle_plugin_remove_vmad_property(radicle_plugin* plugin, radicle_form_key key, size_t script_index, size_t property_index);
RADICLE_API radicle_status radicle_plugin_add_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t fragment_key, const char* script, const char* function);
RADICLE_API radicle_status radicle_plugin_remove_vmad_fragment(radicle_plugin* plugin, radicle_form_key key, uint32_t fragment_key);

#ifdef __cplusplus
}
#endif
