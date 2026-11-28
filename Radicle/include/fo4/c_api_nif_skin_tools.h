#pragma once

#include "fo4/c_api_nif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: skin weights are four slots per vertex, uint8_t bones[4 * n] and float weights[4 * n], and a slot of weight 0 is empty. Results are new arrays (bones released with radicle_free_bytes, weights with radicle_free_floats) and the inputs are never written. Output pointers and counts are zeroed on failure. */

/* Requires one position (3 floats) per vertex and triangles only for mode 3; mode is 0 add, 1 subtract, 2 replace, 3 smooth, falloff is 0 smoothstep, 1 linear, 2 constant, and bone is 0 to 255. */
/* Invariant: out_count is 4 * vertex_count; locked bones keep their exact weights. */
RADICLE_API radicle_status radicle_nif_skin_brush(const float* positions,
                                                  size_t vertex_count,
                                                  const uint16_t* triangles,
                                                  size_t triangle_index_count,
                                                  const uint8_t* bones,
                                                  const float* weights,
                                                  uint32_t mode,
                                                  uint32_t falloff,
                                                  float center_x,
                                                  float center_y,
                                                  float center_z,
                                                  float radius,
                                                  float strength,
                                                  uint32_t bone,
                                                  float target,
                                                  const uint8_t* locked_bones,
                                                  size_t locked_count,
                                                  uint8_t** out_bones,
                                                  float** out_weights,
                                                  size_t* out_count,
                                                  size_t* out_in_reach,
                                                  size_t* out_changed,
                                                  size_t* out_skipped,
                                                  float* out_max_weight_change);

/* Requires a skinned tri-shape block index (BSTriShape, BSSubIndexTriShape, BSMeshLODTriShape or BSDynamicTriShape). */
/* Invariant: bones and weights hold 4 * out_vertex_count entries each, one four-slot record per vertex in vertex order, released with radicle_free_bytes and radicle_free_floats; weights are the half-precision values the file stores. */
RADICLE_API radicle_status radicle_nif_skin_read(const radicle_nif* nif,
                                                 uint32_t block,
                                                 uint8_t** out_bones,
                                                 float** out_weights,
                                                 size_t* out_vertex_count);

/* Requires a skinned tri-shape block index and one four-slot record per vertex of that shape; every weight must be finite, at least 0, and at most 65504, and a vertex that held weight must keep some. */
/* Invariant: out_bytes is a new NIF released with radicle_free_bytes that differs from the input only in the weight and bone bytes of that shape's vertices (the input again when no record changes); a refused edit returns RADICLE_STATUS_INVALID_ARGUMENT and writes nothing. */
RADICLE_API radicle_status radicle_nif_skin_write(const radicle_nif* nif,
                                                  uint32_t block,
                                                  const uint8_t* bones,
                                                  const float* weights,
                                                  size_t vertex_count,
                                                  uint8_t** out_bytes,
                                                  size_t* out_size);

/* Requires two NIF buffers whose shapes sit in one world frame and two skinned tri-shape block indexes (not shape-list indexes); method is 0 nearest vertex or 1 barycentric, and max_distance 0 means no limit. */
/* Invariant: out_bytes is the target NIF with the target shape's weights replaced, released with radicle_free_bytes, and it is the target unchanged when nothing transferred. */
RADICLE_API radicle_status radicle_nif_skin_transfer(const uint8_t* source_nif,
                                                     size_t source_size,
                                                     uint32_t source_block,
                                                     const uint8_t* target_nif,
                                                     size_t target_size,
                                                     uint32_t target_block,
                                                     uint32_t method,
                                                     float max_distance,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size,
                                                     size_t* out_transferred,
                                                     size_t* out_out_of_reach,
                                                     size_t* out_unmapped);

/* Requires a tri-shape block index. A remap record is four slots like the skin weights; a block of -1 means the shape owns no such block. */
/* Invariant: a new-bone entry is a raw 208-byte blob, name in the first 16 bytes NUL padded and 16 floats of matrix at byte 144, so out_new_bones holds 208 * out_new_bone_count bytes released with radicle_free_bytes; bones and weights hold 4 * out_record_count entries. */
RADICLE_API radicle_status radicle_nif_remap_read(const radicle_nif* nif,
                                                  uint32_t block,
                                                  int32_t* out_remap_block,
                                                  int32_t* out_new_bones_block,
                                                  uint8_t** out_bones,
                                                  float** out_weights,
                                                  size_t* out_record_count,
                                                  uint8_t** out_new_bones,
                                                  size_t* out_new_bone_count);

/* Invariant: the checks report and never refuse; a remap_block of -1 skips the record count check, and the legal bone indexes are skin_bone_count plus new_bone_count, and a new_bone_count past 256 is refused. Messages are released with radicle_free_string_array. */
RADICLE_API radicle_status radicle_nif_remap_validate(int32_t remap_block,
                                                      const uint8_t* bones,
                                                      const float* weights,
                                                      size_t record_count,
                                                      size_t new_bone_count,
                                                      size_t skin_bone_count,
                                                      size_t vertex_count,
                                                      char*** out_messages,
                                                      size_t* out_count);

/* Requires a tri-shape block index that owns no remap block; a non-empty record list adds the remap block (one record per vertex), a non-empty new-bone list adds the new-bones block. */
/* Invariant: a block the shape already owns is refused with RADICLE_STATUS_INVALID_ARGUMENT, never replaced; out_bytes is a new NIF released with radicle_free_bytes. */
RADICLE_API radicle_status radicle_nif_remap_create(const radicle_nif* nif,
                                                    uint32_t block,
                                                    const uint8_t* bones,
                                                    const float* weights,
                                                    size_t record_count,
                                                    const uint8_t* new_bones,
                                                    size_t new_bone_count,
                                                    uint8_t** out_bytes,
                                                    size_t* out_size);

/* Requires a shape that already owns the remap block, one record per vertex and weights between 0 and 1, and an owned new-bones block when new_bone_count is not 0. */
/* Invariant: remap_block and new_bones_block are the values radicle_nif_remap_read returned; an owned new-bones block is left untouched when new_bone_count is 0 and new_bones_block is -1. */
RADICLE_API radicle_status radicle_nif_remap_write(const radicle_nif* nif,
                                                   uint32_t block,
                                                   int32_t remap_block,
                                                   int32_t new_bones_block,
                                                   const uint8_t* bones,
                                                   const float* weights,
                                                   size_t record_count,
                                                   const uint8_t* new_bones,
                                                   size_t new_bone_count,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

#ifdef __cplusplus
}
#endif
