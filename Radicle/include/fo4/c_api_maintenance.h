#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rows must be freed with radicle_free_string_array. */
RADICLE_API radicle_status radicle_env_identical_to_master(const radicle_env* env,
                                                           const char* plugin,
                                                           size_t limit,
                                                           char*** out_strings,
                                                           size_t* out_count);
RADICLE_API radicle_status radicle_env_deleted_counts(const radicle_env* env,
                                                      const char* plugin,
                                                      size_t* out_records,
                                                      size_t* out_references,
                                                      size_t* out_navmeshes);
/* Names must be freed with radicle_free_string_array. */
RADICLE_API radicle_status radicle_env_unused_masters(const radicle_env* env,
                                                      const char* plugin,
                                                      char*** out_strings,
                                                      size_t* out_count,
                                                      int32_t* out_complete);
RADICLE_API radicle_status radicle_env_esl_eligibility(const radicle_env* env,
                                                       const char* plugin,
                                                       size_t* out_native_records,
                                                       size_t* out_out_of_range,
                                                       size_t* out_capacity);

#ifdef __cplusplus
}
#endif
