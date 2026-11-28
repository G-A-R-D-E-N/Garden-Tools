#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Requires radicle_free_string_array; one tab-separated row per element: record, level, path, category, roles. Level is record, subrecord or field; roles are comma-separated, one per plugin in load order. */
RADICLE_API radicle_status radicle_env_classify_conflicts(const radicle_env* env,
                                                          char*** out_strings,
                                                          size_t* out_count);

#ifdef __cplusplus
}
#endif
