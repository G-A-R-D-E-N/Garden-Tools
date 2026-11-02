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

RADICLE_API void radicle_free_floats(float* values);
RADICLE_API void radicle_free_triangles(uint16_t* values);
RADICLE_API void radicle_free_node_refs(uint32_t* values);

#ifdef __cplusplus
}
#endif
