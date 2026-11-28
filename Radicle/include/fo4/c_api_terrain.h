#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADICLE_LAND_GRID_SIZE 33

typedef struct radicle_land_heights {
  int32_t has_heights;
  float base;
  /* Constraint: the length is RADICLE_LAND_GRID_SIZE squared, kept literal for the bindings generator. */
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

typedef struct radicle_land_layer_alpha {
  int32_t is_base;
  radicle_form_key texture;
  uint8_t quadrant;
  uint16_t layer;
  const float* alpha;
} radicle_land_layer_alpha;

typedef struct radicle_land_layers {
  const radicle_land_layer_alpha* layers;
  size_t layer_count;
  void* internal;
} radicle_land_layers;

RADICLE_API radicle_status radicle_landscape_heights(const radicle_env* env,
                                                     const radicle_record* record,
                                                     radicle_land_heights* out_heights);
RADICLE_API radicle_status radicle_land_height_at(const radicle_land_heights* heights,
                                                  int32_t row,
                                                  int32_t column,
                                                  float* out_height);
RADICLE_API radicle_status radicle_landscape_blend(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_land_blend** out_blend);
RADICLE_API radicle_status radicle_land_blend_weight_at(const radicle_land_blend_layer* layer,
                                                         int32_t row,
                                                         int32_t column,
                                                         float* out_weight);
RADICLE_API void radicle_landscape_blend_free(radicle_land_blend* blend);
RADICLE_API radicle_status radicle_landscape_layers(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_land_layers** out_layers);
RADICLE_API void radicle_landscape_layers_free(radicle_land_layers* layers);

typedef enum radicle_land_mutation_status {
  RADICLE_LAND_MUTATION_UPDATED = 0,
  RADICLE_LAND_MUTATION_UNCHANGED = 1,
  RADICLE_LAND_MUTATION_RECORD_MISSING = 2,
  RADICLE_LAND_MUTATION_RECORD_MISMATCH = 3,
  RADICLE_LAND_MUTATION_SUBRECORD_MISSING = 4,
  RADICLE_LAND_MUTATION_SIZE_MISMATCH = 5,
  RADICLE_LAND_MUTATION_VALUE_INVALID = 6,
  RADICLE_LAND_MUTATION_HEIGHT_UNREPRESENTABLE = 7,
  RADICLE_LAND_MUTATION_HEIGHT_OUT_OF_RANGE = 8,
  RADICLE_LAND_MUTATION_TEXTURE_MISSING = 9,
  RADICLE_LAND_MUTATION_TEXTURE_MISMATCH = 10,
  RADICLE_LAND_MUTATION_QUADRANT_INVALID = 11,
  RADICLE_LAND_MUTATION_INDEX_OUT_OF_RANGE = 12
} radicle_land_mutation_status;

RADICLE_API radicle_status radicle_plugin_set_land_heights(radicle_plugin* plugin, radicle_form_key key,
                                                           const float* heights, size_t count,
                                                           radicle_land_mutation_status* out_status);
RADICLE_API radicle_status radicle_plugin_set_land_normals(radicle_plugin* plugin, radicle_form_key key,
                                                           const uint8_t* normals, size_t count,
                                                           radicle_land_mutation_status* out_status);
RADICLE_API radicle_status radicle_plugin_set_land_vertex_colors(radicle_plugin* plugin, radicle_form_key key,
                                                                 const uint8_t* colors, size_t count,
                                                                 radicle_land_mutation_status* out_status);
RADICLE_API radicle_status radicle_plugin_set_land_base_texture(radicle_plugin* plugin, radicle_form_key key,
                                                                uint8_t quadrant, radicle_form_key texture,
                                                                uint16_t layer,
                                                                radicle_land_mutation_status* out_status);
RADICLE_API radicle_status radicle_plugin_add_land_alpha_layer(radicle_plugin* plugin, radicle_form_key key,
                                                               uint8_t quadrant, radicle_form_key texture,
                                                               uint16_t layer, const float* alpha,
                                                               size_t count, size_t* out_index,
                                                               radicle_land_mutation_status* out_status);
RADICLE_API radicle_status radicle_plugin_remove_land_alpha_layer(radicle_plugin* plugin, radicle_form_key key,
                                                                  size_t index,
                                                                  radicle_land_mutation_status* out_status);

#ifdef __cplusplus
}
#endif
