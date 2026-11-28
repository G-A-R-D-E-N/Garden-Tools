#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_env_record_json(const radicle_env* env,
                                                   const char* plugin,
                                                   const char* form_key,
                                                   int32_t include_raw,
                                                   char*** out_strings,
                                                   size_t* out_count);

#ifdef __cplusplus
}
#endif
