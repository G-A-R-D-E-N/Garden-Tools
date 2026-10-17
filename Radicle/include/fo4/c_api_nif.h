#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only NIF geometry, skin and shader access. A radicle_nif handle is
 * opened from the file's bytes, because assets arrive through GameData and the
 * Radicle asset resolver rather than from a path.
 *
 * Arrays and strings returned by this API are owned by the caller: floats with
 * radicle_free_floats, triangle indices with radicle_free_triangles, and every
 * byte array (names, material paths, texture paths, bone indices) with
 * radicle_free_bytes. An empty result returns OK with a NULL pointer and a
 * count of zero, never an error.
 */
typedef struct radicle_nif radicle_nif;

RADICLE_API radicle_status radicle_nif_read(const uint8_t* bytes, size_t size, radicle_nif** out_nif);
RADICLE_API void radicle_nif_destroy(radicle_nif* nif);

RADICLE_API radicle_status radicle_nif_shape_count(const radicle_nif* nif, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_name(const radicle_nif* nif,
                                                  size_t index,
                                                  uint8_t** out_bytes,
                                                  size_t* out_size);

/* Shape-to-global transform: 3 translation floats, 9 row-major rotation floats
   and 1 scale float, in the order the file stores them. */
RADICLE_API radicle_status radicle_nif_shape_transform(const radicle_nif* nif,
                                                       size_t index,
                                                       float* out_translation,
                                                       float* out_rotation,
                                                       float* out_scale);

/* Block references the shape stores: skin, shader property and alpha property.
   -1 means the shape stores no reference. */
RADICLE_API radicle_status radicle_nif_shape_refs(const radicle_nif* nif,
                                                  size_t index,
                                                  int32_t* out_skin_ref,
                                                  int32_t* out_shader_ref,
                                                  int32_t* out_alpha_ref);

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
/* Present only when the shape stores both normals and tangents. */
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

/* The shader property's own name: its material file path when the shape uses a
   BGSM or BGEM, and empty when it does not. */
RADICLE_API radicle_status radicle_nif_shape_material(const radicle_nif* nif,
                                                      size_t index,
                                                      uint8_t** out_bytes,
                                                      size_t* out_size);
/* Slots as BSShaderTextureSet stores them: 0 diffuse, 1 normal, 2 glow and so on. */
RADICLE_API radicle_status radicle_nif_shape_texture_count(const radicle_nif* nif, size_t index, size_t* out_count);
RADICLE_API radicle_status radicle_nif_shape_texture(const radicle_nif* nif,
                                                     size_t index,
                                                     size_t slot,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size);

/* 1 when the shape's vertex descriptor carries skin weights, 0 otherwise. */
RADICLE_API radicle_status radicle_nif_shape_skinned(const radicle_nif* nif, size_t index, int32_t* out_skinned);
/* Four floats per vertex. */
RADICLE_API radicle_status radicle_nif_shape_weights(const radicle_nif* nif,
                                                     size_t index,
                                                     float** out_values,
                                                     size_t* out_count);
/* Four bone indices per vertex, indexing the shape's bone list. */
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
/* Bind transform: bounding-sphere centre and radius, 9 row-major rotation
   floats, 3 translation floats and 1 scale float, in the order the file stores
   them. The scale multiplies the rotation. */
RADICLE_API radicle_status radicle_nif_shape_bone_transform(const radicle_nif* nif,
                                                            size_t index,
                                                            size_t bone,
                                                            float* out_center,
                                                            float* out_radius,
                                                            float* out_rotation,
                                                            float* out_translation,
                                                            float* out_scale);

RADICLE_API void radicle_free_floats(float* values);
RADICLE_API void radicle_free_triangles(uint16_t* values);

#ifdef __cplusplus
}
#endif
