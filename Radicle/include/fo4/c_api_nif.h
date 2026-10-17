#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only NIF scene graph, geometry, skin and shader access. A radicle_nif
 * handle is opened from the file's bytes, because assets arrive through
 * GameData and the Radicle asset resolver rather than from a path.
 *
 * Arrays and strings returned by this API are owned by the caller: floats with
 * radicle_free_floats, triangle indices with radicle_free_triangles, node
 * references with radicle_free_node_refs, and every byte array (names, type
 * names, material paths, texture paths, warnings, bone indices) with
 * radicle_free_bytes. An empty result returns OK with a NULL pointer and a
 * count of zero, never an error.
 *
 * Thread safety: every accessor on a radicle_nif handle is read-only and safe
 * from several threads at once. radicle_nif_destroy must not race any other
 * call on the same handle. Handles are independent.
 */
typedef struct radicle_nif radicle_nif;

RADICLE_API radicle_status radicle_nif_read(const uint8_t* bytes, size_t size, radicle_nif** out_nif);
RADICLE_API void radicle_nif_destroy(radicle_nif* nif);

/*
 * The scene graph. Nodes are the node blocks the reader understands (NiNode and
 * the other NiNode-derived block types; see docs/NIF.md), in block order. A
 * node's parent is the node list index of its parent node, or -1 when it is a
 * root; node_parent is -1 for a root node and for a node whose parent block is
 * not itself a node, which the reader does not produce. Children are the block
 * indices the file stores, shapes included, so a caller maps each one through
 * radicle_nif_node_for_block to find out whether it is a node.
 */
RADICLE_API radicle_status radicle_nif_node_count(const radicle_nif* nif, size_t* out_count);
/* The node's block index inside the file. */
RADICLE_API radicle_status radicle_nif_node_block(const radicle_nif* nif, size_t index, uint32_t* out_block);
/* The node's block type name, such as "NiNode" or "BSFadeNode". */
RADICLE_API radicle_status radicle_nif_node_type(const radicle_nif* nif,
                                                 size_t index,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size);
/* The node's NiObjectNET name, empty when the file stores none. */
RADICLE_API radicle_status radicle_nif_node_name(const radicle_nif* nif,
                                                 size_t index,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size);
/* The parent node's node list index, or -1 when the node is a root. */
RADICLE_API radicle_status radicle_nif_node_parent(const radicle_nif* nif, size_t index, int32_t* out_parent);
/* The node's children as block indices, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_nif_node_children(const radicle_nif* nif,
                                                     size_t index,
                                                     uint32_t** out_values,
                                                     size_t* out_count);
/* The node's local transform: 3 translation floats, 9 row-major rotation floats
   and 1 scale float, in the order the file stores them. */
RADICLE_API radicle_status radicle_nif_node_transform(const radicle_nif* nif,
                                                      size_t index,
                                                      float* out_translation,
                                                      float* out_rotation,
                                                      float* out_scale);
/* The node list index of a block, or -1 when that block is not a node. */
RADICLE_API radicle_status radicle_nif_node_for_block(const radicle_nif* nif, uint32_t block, int32_t* out_node);

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

/* The node list index of the nearest node ancestor of the shape, or -1 when it
   has none. This is the node whose ancestors drive the shape. */
RADICLE_API radicle_status radicle_nif_shape_parent_node(const radicle_nif* nif, size_t index, int32_t* out_node);

/* Block references the shape stores: skin, shader property and alpha property.
   -1 means the shape stores no reference. */
RADICLE_API radicle_status radicle_nif_shape_refs(const radicle_nif* nif,
                                                  size_t index,
                                                  int32_t* out_skin_ref,
                                                  int32_t* out_shader_ref,
                                                  int32_t* out_alpha_ref);

/* Why a part of the shape was left empty: an empty string when every part read,
   otherwise the reason, and the affected part is empty. A shader block that
   does not parse leaves the material and the texture slots empty and records a
   warning instead of failing the file. */
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
RADICLE_API void radicle_free_node_refs(uint32_t* values);

#ifdef __cplusplus
}
#endif
