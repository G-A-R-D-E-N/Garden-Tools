#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_nif radicle_nif;

RADICLE_API radicle_status radicle_nif_read(const uint8_t* bytes, size_t size, radicle_nif** out_nif);
RADICLE_API void radicle_nif_destroy(radicle_nif* nif);

RADICLE_API radicle_status radicle_nif_node_count(const radicle_nif* nif, size_t* out_count);
RADICLE_API radicle_status radicle_nif_node_block(const radicle_nif* nif, size_t index, uint32_t* out_block);
RADICLE_API radicle_status radicle_nif_node_type(const radicle_nif* nif,
                                                 size_t index,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size);
RADICLE_API radicle_status radicle_nif_node_name(const radicle_nif* nif,
                                                 size_t index,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size);
RADICLE_API radicle_status radicle_nif_node_parent(const radicle_nif* nif, size_t index, int32_t* out_parent);
RADICLE_API radicle_status radicle_nif_node_children(const radicle_nif* nif,
                                                     size_t index,
                                                     uint32_t** out_values,
                                                     size_t* out_count);
RADICLE_API radicle_status radicle_nif_node_transform(const radicle_nif* nif,
                                                      size_t index,
                                                      float* out_translation,
                                                      float* out_rotation,
                                                      float* out_scale);
RADICLE_API radicle_status radicle_nif_node_for_block(const radicle_nif* nif, uint32_t block, int32_t* out_node);
RADICLE_API radicle_status radicle_nif_node_value(const radicle_nif* nif,
                                                  size_t index,
                                                  int32_t* out_has_value,
                                                  int32_t* out_value);

RADICLE_API radicle_status radicle_nif_shape_count(const radicle_nif* nif, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_name(const radicle_nif* nif,
                                                  size_t index,
                                                  uint8_t** out_bytes,
                                                  size_t* out_size);

RADICLE_API radicle_status radicle_nif_shape_transform(const radicle_nif* nif,
                                                       size_t index,
                                                       float* out_translation,
                                                       float* out_rotation,
                                                       float* out_scale);

RADICLE_API radicle_status radicle_nif_shape_parent_node(const radicle_nif* nif, size_t index, int32_t* out_node);

RADICLE_API radicle_status radicle_nif_shape_refs(const radicle_nif* nif,
                                                  size_t index,
                                                  int32_t* out_skin_ref,
                                                  int32_t* out_shader_ref,
                                                  int32_t* out_alpha_ref);

RADICLE_API radicle_status radicle_nif_shape_warning(const radicle_nif* nif,
                                                     size_t index,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size);

RADICLE_API radicle_status radicle_nif_shape_vertex_count(const radicle_nif* nif, size_t index, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_positions(const radicle_nif* nif,
                                                       size_t index,
                                                       float** out_values,
                                                       size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_normals(const radicle_nif* nif,
                                                     size_t index,
                                                     float** out_values,
                                                     size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_tangents(const radicle_nif* nif,
                                                      size_t index,
                                                      float** out_values,
                                                      size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_bitangents(const radicle_nif* nif,
                                                        size_t index,
                                                        float** out_values,
                                                        size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_uvs(const radicle_nif* nif,
                                                 size_t index,
                                                 float** out_values,
                                                 size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_colors(const radicle_nif* nif,
                                                    size_t index,
                                                    float** out_values,
                                                    size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_triangles(const radicle_nif* nif,
                                                       size_t index,
                                                       uint16_t** out_values,
                                                       size_t* out_count);

RADICLE_API radicle_status radicle_nif_shape_material(const radicle_nif* nif,
                                                      size_t index,
                                                      uint8_t** out_bytes,
                                                      size_t* out_size);
RADICLE_API radicle_status radicle_nif_shape_texture_count(const radicle_nif* nif, size_t index, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_texture(const radicle_nif* nif,
                                                     size_t index,
                                                     size_t slot,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size);

RADICLE_API radicle_status radicle_nif_shape_skinned(const radicle_nif* nif, size_t index, int32_t* out_skinned);
RADICLE_API radicle_status radicle_nif_shape_weights(const radicle_nif* nif,
                                                     size_t index,
                                                     float** out_values,
                                                     size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_bone_indices(const radicle_nif* nif,
                                                          size_t index,
                                                          uint8_t** out_values,
                                                          size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_bone_count(const radicle_nif* nif, size_t index, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_bone_name(const radicle_nif* nif,
                                                       size_t index,
                                                       size_t bone,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);
RADICLE_API radicle_status radicle_nif_shape_bone_transform(const radicle_nif* nif,
                                                            size_t index,
                                                            size_t bone,
                                                            float* out_center,
                                                            float* out_radius,
                                                            float* out_rotation,
                                                            float* out_translation,
                                                            float* out_scale);

RADICLE_API radicle_status radicle_nif_collision_count(const radicle_nif* nif, size_t* out_count);
RADICLE_API radicle_status radicle_nif_collision_block(const radicle_nif* nif, size_t index, uint32_t* out_block);
RADICLE_API radicle_status radicle_nif_collision_type(const radicle_nif* nif,
                                                      size_t index,
                                                      uint8_t** out_bytes,
                                                      size_t* out_size);
RADICLE_API radicle_status radicle_nif_collision_bytes(const radicle_nif* nif,
                                                       size_t index,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);
RADICLE_API radicle_status radicle_nif_collision_owner_block(const radicle_nif* nif,
                                                             size_t index,
                                                             int32_t* out_block);
RADICLE_API radicle_status radicle_nif_collision_flags(const radicle_nif* nif,
                                                       size_t index,
                                                       uint16_t* out_flags);
RADICLE_API radicle_status radicle_nif_collision_system_ref(const radicle_nif* nif,
                                                            size_t index,
                                                            int32_t* out_block);
RADICLE_API radicle_status radicle_nif_collision_body_id(const radicle_nif* nif,
                                                         size_t index,
                                                         uint32_t* out_body_id);
RADICLE_API radicle_status radicle_nif_collision_target_block(const radicle_nif* nif,
                                                              size_t index,
                                                              int32_t* out_block);
RADICLE_API radicle_status radicle_nif_collision_target_node(const radicle_nif* nif,
                                                             size_t index,
                                                             int32_t* out_node);

/* Invariant: a block index names the shader block, and its strings come back as caller-owned bytes. */
typedef struct radicle_nif_lighting_shader {
  uint32_t block;
  uint32_t shader_type;
  uint32_t flags1;
  uint32_t flags2;
  float uv_offset[2];
  float uv_scale[2];
  int32_t texture_set_ref;
  size_t texture_count;
  float emissive_color[3];
  float emissive_multiple;
  uint8_t has_root_material;
  uint32_t texture_clamp;
  float alpha;
  float refraction_strength;
  float smoothness;
  float specular_color[3];
  float specular_strength;
  float subsurface_rolloff;
  float rimlight_power;
  uint8_t has_backlight;
  float backlight_power;
  uint8_t has_grayscale_to_palette_scale;
  float grayscale_to_palette_scale;
  float fresnel_power;
  uint8_t has_wetness;
  float wetness[6];
  uint8_t has_env_map;
  float env_map_scale;
  uint8_t env_map_ssr;
  uint8_t env_map_wetness_ssr;
  uint8_t has_skin_tint;
  float skin_tint[4];
  uint8_t has_hair_tint;
  float hair_tint[3];
  uint8_t has_snow;
  float snow[4];
  uint8_t has_parallax_occ;
  float parallax_occ[2];
  uint8_t has_multi_layer_parallax;
  float multi_layer_parallax[5];
  uint8_t has_eye;
  float eye_cubemap_scale;
  float eye_left[3];
  float eye_right[3];
  size_t tail_bytes;
} radicle_nif_lighting_shader;

typedef struct radicle_nif_effect_shader {
  uint32_t block;
  uint32_t flags1;
  uint32_t flags2;
  float uv_offset[2];
  float uv_scale[2];
  uint8_t texture_clamp;
  uint8_t lighting_influence;
  uint8_t env_map_min_lod;
  uint8_t unused;
  float falloff_start_angle;
  float falloff_stop_angle;
  float falloff_start_opacity;
  float falloff_stop_opacity;
  float base_color[4];
  float base_color_scale;
  float soft_falloff_depth;
  size_t texture_count;
  float env_map_scale;
} radicle_nif_effect_shader;

typedef struct radicle_nif_alpha_property {
  uint32_t block;
  uint16_t flags;
  uint8_t threshold;
  /* Invariant: the five decoded fields are flags bits 0, 1-4, 5-8, 9 and 10-12. */
  uint8_t blend_enabled;
  uint8_t source_blend;
  uint8_t destination_blend;
  uint8_t test_enabled;
  uint8_t test_function;
} radicle_nif_alpha_property;

RADICLE_API radicle_status radicle_nif_read_lighting_shader(const radicle_nif* nif,
                                                       uint32_t block,
                                                       radicle_nif_lighting_shader* out_shader);
RADICLE_API radicle_status radicle_nif_lighting_shader_name(const radicle_nif* nif,
                                                            uint32_t block,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);
RADICLE_API radicle_status radicle_nif_lighting_shader_root_material(const radicle_nif* nif,
                                                                    uint32_t block,
                                                                    uint8_t** out_bytes,
                                                                    size_t* out_size);
RADICLE_API radicle_status radicle_nif_read_effect_shader(const radicle_nif* nif,
                                                     uint32_t block,
                                                     radicle_nif_effect_shader* out_shader);
RADICLE_API radicle_status radicle_nif_effect_shader_name(const radicle_nif* nif,
                                                          uint32_t block,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_nif_effect_shader_texture(const radicle_nif* nif,
                                                            uint32_t block,
                                                            size_t slot,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);
RADICLE_API radicle_status radicle_nif_read_alpha_property(const radicle_nif* nif,
                                                     uint32_t block,
                                                     radicle_nif_alpha_property* out_alpha);
RADICLE_API size_t radicle_nif_shader_flag_count(void);
/* Requires bit below the 64-entry flag word; past it the call answers RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_nif_shader_flag_name(uint32_t bit, uint8_t** out_bytes, size_t* out_size);

RADICLE_API void radicle_free_floats(float* values);
RADICLE_API void radicle_free_triangles(uint16_t* values);
RADICLE_API void radicle_free_node_refs(uint32_t* values);

#ifdef __cplusplus
}
#endif
