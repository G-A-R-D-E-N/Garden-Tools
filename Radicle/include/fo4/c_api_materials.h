#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_material_substitution {
  const char* original_material;
  const char* replacement_material;
  int32_t has_color_remapping_index;
  float color_remapping_index;
} radicle_material_substitution;

typedef struct radicle_material_swap {
  const radicle_material_substitution* substitutions;
  size_t substitution_count;
  void* internal;
} radicle_material_swap;

RADICLE_API radicle_status radicle_material_swap_decode(const radicle_env* env,
                                                        const radicle_record* record,
                                                        radicle_material_swap** out_swap);
RADICLE_API void radicle_material_swap_free(radicle_material_swap* swap);
RADICLE_API radicle_status radicle_placed_reference_material_swap(const radicle_env* env,
                                                                  const radicle_record* record,
                                                                  radicle_form_key* out_swap);
RADICLE_API radicle_status radicle_record_primary_model_material_swap(const radicle_env* env,
                                                                      const radicle_record* record,
                                                                      radicle_form_key* out_swap);
RADICLE_API radicle_status radicle_record_primary_model_color_remapping_index(const radicle_env* env,
                                                                             const radicle_record* record,
                                                                             float* out_index);

#ifdef __cplusplus
}
#endif
