#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_delta_patch_counts {
  size_t kept;
  size_t removed;
} radicle_delta_patch_counts;

RADICLE_API radicle_status radicle_env_build_delta_patch(const radicle_env* env,
                                                         const char* older_plugin,
                                                         const char* newer_plugin,
                                                         const char* output_path,
                                                         radicle_delta_patch_counts* out_counts);

#ifdef __cplusplus
}
#endif
