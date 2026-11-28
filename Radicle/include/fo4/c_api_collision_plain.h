#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_nif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_collision_plain radicle_collision_plain;

typedef enum radicle_collision_shape_kind {
  RADICLE_COLLISION_SHAPE_BOX = 1,
  RADICLE_COLLISION_SHAPE_SPHERE = 2,
  RADICLE_COLLISION_SHAPE_CAPSULE = 3,
  RADICLE_COLLISION_SHAPE_CONVEX_HULL = 4,
  RADICLE_COLLISION_SHAPE_TRIANGLE_MESH = 5
} radicle_collision_shape_kind;

/* Invariant: positions are engine axes (Havok x, y, z become x, z, -y) in metres; basis is row-major. */
typedef struct radicle_collision_plain_body {
  uint32_t collision_index;
  uint32_t body;
  uint32_t layer;
  uint8_t dynamic;
  uint8_t trigger_type;
  uint8_t has_mass;
  float mass;
  uint8_t has_center_of_mass;
  float center_of_mass[3];
  float origin[3];
  float basis[9];
  size_t first_shape;
  size_t shape_count;
  uint32_t skipped_shapes;
} radicle_collision_plain_body;

/* Constraint: origin and basis place the shape in its body's frame; a sphere's centre is already in origin (from point_a), so a host must not add the body origin to it again; box half_extents run along the basis columns; radius of a sphere or capsule is the whole rounding. */
typedef struct radicle_collision_plain_shape {
  uint32_t collision_index;
  uint32_t body;
  uint32_t kind;
  uint32_t material;
  float origin[3];
  float basis[9];
  float half_extents[3];
  float radius;
  float point_a[3];
  float point_b[3];
  float margin;
  size_t point_count;
  size_t triangle_count;
} radicle_collision_plain_shape;

/* Requires a radicle_nif; a system that does not decode is counted, not fatal. system is the index of radicle_nif_collision_*, body the Body ID of radicle_nif_collision_body_id. */
RADICLE_API radicle_status radicle_nif_collision_plain(const radicle_nif* nif, radicle_collision_plain** out_plain);
RADICLE_API void radicle_collision_plain_destroy(radicle_collision_plain* plain);

RADICLE_API radicle_status radicle_collision_plain_body_count(const radicle_collision_plain* plain, size_t* out_count);
RADICLE_API radicle_status radicle_collision_plain_read_body(const radicle_collision_plain* plain,
                                                             size_t index,
                                                             radicle_collision_plain_body* out_body);
RADICLE_API radicle_status radicle_collision_plain_shape_count(const radicle_collision_plain* plain, size_t* out_count);
RADICLE_API radicle_status radicle_collision_plain_read_shape(const radicle_collision_plain* plain,
                                                              size_t index,
                                                              radicle_collision_plain_shape* out_shape);
/* Invariant: a hull or mesh answers 3 * point_count floats; every other kind answers none. Release with radicle_free_floats. */
RADICLE_API radicle_status radicle_collision_plain_shape_points(const radicle_collision_plain* plain,
                                                                size_t index,
                                                                float** out_values,
                                                                size_t* out_count);
/* Invariant: a mesh answers 3 * triangle_count corners; every other kind answers none. Release with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_collision_plain_shape_indices(const radicle_collision_plain* plain,
                                                                 size_t index,
                                                                 uint32_t** out_values,
                                                                 size_t* out_count);
/* Invariant: a mesh answers one material per triangle; every other kind answers none. Release with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_collision_plain_shape_triangle_materials(const radicle_collision_plain* plain,
                                                                            size_t index,
                                                                            uint32_t** out_values,
                                                                            size_t* out_count);
/* Invariant: the first bhkNPCollisionObject (block order) naming the body's system and Body ID; out_owner_block is its block, or -1 with identity outputs when none names the body (the body is still listed). out_target_node is the owner's target node index (-1 for a target that is not a node), the transform is radicle_nif_collision_owner_transform of that node: engine axes, metres, column-vector row-major rotation, scale separate. */
RADICLE_API radicle_status radicle_collision_plain_body_owner(const radicle_collision_plain* plain,
                                                              size_t index,
                                                              int32_t* out_owner_block,
                                                              int32_t* out_target_node,
                                                              float* out_translation,
                                                              float* out_rotation,
                                                              float* out_scale);
/* Constraint: skipped counts shapes the plain kinds cannot hold; undecodable counts systems that did not decode. */
RADICLE_API radicle_status radicle_collision_plain_counts(const radicle_collision_plain* plain,
                                                          uint32_t* out_skipped_shapes,
                                                          uint32_t* out_undecodable_systems);

#ifdef __cplusplus
}
#endif
