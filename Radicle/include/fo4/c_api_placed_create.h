#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_plugin_create_placed_actor(radicle_plugin* plugin,
                                                              radicle_form_key cell,
                                                              radicle_form_key base,
                                                              const float position[3],
                                                              const float rotation[3],
                                                              int32_t persistent,
                                                              int32_t initially_disabled,
                                                              radicle_form_key* out_key);

#ifdef __cplusplus
}
#endif