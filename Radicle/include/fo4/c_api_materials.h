#pragma once

#include "fo4/c_api_env.h"
#include "fo4/c_api_material_file.h"
#include "fo4/c_api_nif.h"

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

typedef enum radicle_resolved_material_source {
  RADICLE_RESOLVED_MATERIAL_SOURCE_NIF_ONLY = 0,
  RADICLE_RESOLVED_MATERIAL_SOURCE_BGSM = 1,
  RADICLE_RESOLVED_MATERIAL_SOURCE_BGEM = 2
} radicle_resolved_material_source;

typedef enum radicle_resolved_alpha_mode {
  RADICLE_RESOLVED_ALPHA_MODE_OPAQUE = 0,
  RADICLE_RESOLVED_ALPHA_MODE_TEST = 1,
  RADICLE_RESOLVED_ALPHA_MODE_BLEND = 2
} radicle_resolved_alpha_mode;

/* Invariant: every pointer in the struct, and the strings behind it, belongs to the handle and lives until radicle_resolved_material_free. */
/* Invariant: textures holds texture_count (10) lowercase Data-relative paths in the lighting texture-set slot order, empty for an unused slot. */
/* Invariant: blend_src and blend_dst are the NiAlphaProperty blend enum, 0 one through 10 source alpha saturate. */
typedef struct radicle_resolved_material {
  radicle_resolved_material_source source;
  const char* nif_material_path;
  const char* material_path;
  const char* const* textures;
  size_t texture_count;
  radicle_resolved_alpha_mode alpha_mode;
  uint8_t alpha_threshold;
  uint8_t blend_src;
  uint8_t blend_dst;
  float alpha;
  int32_t two_sided;
  int32_t has_glow;
  int32_t has_wet;
  float uv_offset[2];
  float uv_scale[2];
  radicle_material_color3 emissive;
  float emissive_mult;
  int32_t swapped;
  int32_t has_color_remapping_index;
  float color_remapping_index;
  const char* const* warnings;
  size_t warning_count;
  void* internal;
} radicle_resolved_material;

/* Requires shape_index to name a shape of nif; an unreadable shader or material is a warning on the result, not a failure. */
RADICLE_API radicle_status radicle_material_resolve(const radicle_nif* nif,
                                                    size_t shape_index,
                                                    const radicle_assets* assets,
                                                    radicle_resolved_material** out_material);
/* Requires the same nif, shape_index and assets the material was resolved with; out_applied is 1 when a substitution matched and the material was re-resolved. */
RADICLE_API radicle_status radicle_resolved_material_apply_swap(radicle_resolved_material* material,
                                                                const radicle_nif* nif,
                                                                size_t shape_index,
                                                                const radicle_assets* assets,
                                                                const radicle_material_swap* swap,
                                                                int32_t* out_applied);
RADICLE_API void radicle_resolved_material_free(radicle_resolved_material* material);

#ifdef __cplusplus
}
#endif
