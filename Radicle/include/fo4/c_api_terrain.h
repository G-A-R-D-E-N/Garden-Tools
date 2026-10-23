#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_land_heights {
  int32_t has_heights;
  float base;
  float heights[1089];
} radicle_land_heights;

typedef struct radicle_land_blend_layer {
  radicle_form_key texture;
  const float* weights;
} radicle_land_blend_layer;

typedef struct radicle_land_blend {
  radicle_form_key base[4];
  const radicle_land_blend_layer* layers;
  size_t layer_count;
  void* internal;
} radicle_land_blend;

RADICLE_API radicle_status radicle_landscape_heights(const radicle_env* env,
                                                     const radicle_record* record,
                                                     radicle_land_heights* out_heights);
RADICLE_API radicle_status radicle_landscape_blend(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_land_blend** out_blend);
RADICLE_API void radicle_landscape_blend_free(radicle_land_blend* blend);

#ifdef __cplusplus
}
#endif
