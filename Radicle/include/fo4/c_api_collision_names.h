#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_collision_names radicle_collision_names;

/* Constraint: layers come from winning COLL Index and Name, materials from winning MATT editor IDs hashed with the material CRC; the table copies them and does not keep env. */
RADICLE_API radicle_status radicle_collision_names_build(const radicle_env* env, radicle_collision_names** out_names);
RADICLE_API void radicle_collision_names_destroy(radicle_collision_names* names);

/* Invariant: an id with no name is OK with *out_name NULL; a name stays valid until radicle_collision_names_destroy. */
RADICLE_API radicle_status radicle_collision_names_layer(const radicle_collision_names* names,
                                                         uint32_t layer,
                                                         const char** out_name);
RADICLE_API radicle_status radicle_collision_names_material(const radicle_collision_names* names,
                                                            uint32_t material,
                                                            const char** out_name);

/* Invariant: *out_flag is 1 when the layer's winning COLL\CollidesWith list is empty (absent or zero length) and no other named winning COLL lists that layer, 0 otherwise; a gap, an unnamed layer and an index past the table are OK with 0. The flag is data-defined: on Fallout4.esm only L_NAVCUT (49) is flagged, so it is not a set of layers a host designs as geometry-free. */
RADICLE_API radicle_status radicle_collision_names_layer_collides_with_nothing(const radicle_collision_names* names,
                                                                               uint32_t layer,
                                                                               int32_t* out_flag);

/* Invariant: *out_layers is the layer's winning COLL\CollidesWith list resolved to the indexes of the named winning COLL records, ascending and unique, with *out_count entries owned by the table until radicle_collision_names_destroy; a layer with an empty, absent or unreadable list, a gap, an unnamed layer and an index past the table are OK with *out_layers NULL and *out_count 0, and a list entry naming no named winning COLL record is dropped. The list is symmetric on Fallout4.esm, and it does not give the engine's frozen no-collide layers: only L_NAVCUT (49) has an empty list there. */
RADICLE_API radicle_status radicle_collision_names_layer_collides_with(const radicle_collision_names* names,
                                                                       uint32_t layer,
                                                                       const uint32_t** out_layers,
                                                                       size_t* out_count);

/* Invariant: layers is one past the highest named layer index, so layer ids below it may still be unnamed. */
RADICLE_API radicle_status radicle_collision_names_counts(const radicle_collision_names* names,
                                                          size_t* out_layers,
                                                          size_t* out_materials);

#ifdef __cplusplus
}
#endif
