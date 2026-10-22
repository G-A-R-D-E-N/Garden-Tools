#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_delta_patch_counts {
  size_t kept;
  size_t removed;
} radicle_delta_patch_counts;

typedef struct radicle_list_merge_counts {
  size_t merged;
  size_t unchanged;
} radicle_list_merge_counts;

RADICLE_API radicle_status radicle_env_build_delta_patch(const radicle_env* env,
                                                         const char* older_plugin,
                                                         const char* newer_plugin,
                                                         const char* output_path,
                                                         radicle_delta_patch_counts* out_counts);

/* Requires LVLI, LVLN, LVSP or FLST; the patch is written to output_path. */
RADICLE_API radicle_status radicle_env_build_list_merge_patch(const radicle_env* env,
                                                              const char* signature,
                                                              const char* patch_name,
                                                              const char* output_path,
                                                              radicle_list_merge_counts* out_counts);

#ifdef __cplusplus
}
#endif
