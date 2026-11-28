#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_env_identical_to_previous_override(const radicle_env* env,
                                                                      const char* plugin,
                                                                      size_t limit,
                                                                      char*** out_strings,
                                                                      size_t* out_count);
RADICLE_API radicle_status radicle_plugin_remove_identical_to_previous_override(radicle_plugin* plugin,
                                                                                const radicle_env* env,
                                                                                size_t* out_removed);

#ifdef __cplusplus
}
#endif
