#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_nif_anim radicle_nif_anim;

/* Invariant: cycle is 0 loop, 1 reverse, 2 clamp, the nif.xml CycleType values. */
typedef struct radicle_nif_anim_sequence {
  uint32_t block;
  float weight;
  int32_t text_keys_ref;
  uint32_t cycle;
  float frequency;
  float start_time;
  float stop_time;
  int32_t manager_ref;
  size_t controlled_count;
  size_t anim_note_count;
} radicle_nif_anim_sequence;

/* Invariant: cumulative is 1 when the NiControllerManager is cumulative, else 0; sequences points at sequence_count NiControllerSequence block indexes owned by the radicle_nif_anim and valid until radicle_nif_anim_destroy, and is NULL when sequence_count is 0. */
typedef struct radicle_nif_anim_manager {
  uint32_t block;
  int32_t cumulative;
  const uint32_t* sequences;
  size_t sequence_count;
} radicle_nif_anim_manager;

typedef struct radicle_nif_anim_controlled {
  int32_t interpolator_ref;
  int32_t controller_ref;
  uint8_t priority;
} radicle_nif_anim_controlled;

typedef struct radicle_nif_anim_pose {
  float translation[3];
  float rotation[4];
  float scale;
} radicle_nif_anim_pose;

typedef struct radicle_nif_anim_text_key {
  float time;
} radicle_nif_anim_text_key;

typedef struct radicle_nif_anim_graph {
  uint32_t block;
  uint8_t controls_base_skeleton;
} radicle_nif_anim_graph;

/* Invariant: components is 1 for NiFloatInterpolator and 3 for NiPoint3Interpolator; interpolation is the nif.xml KeyType, 0 with no keys. */
typedef struct radicle_nif_anim_value_key_info {
  uint32_t components;
  uint32_t interpolation;
  int32_t data_ref;
  float pose[3];
  size_t key_count;
} radicle_nif_anim_value_key_info;

/* Invariant: tbc is tension, continuity, bias in file order. A float key fills value[0]; forward and backward are set only for quadratic keys and tbc only for TBC keys, else zero. */
typedef struct radicle_nif_anim_value_key {
  float time;
  float value[3];
  float forward[3];
  float backward[3];
  float tbc[3];
} radicle_nif_anim_value_key;

/* Requires NIF bytes; corrupt animation answers PARSE_ERROR and a missing block answers NOT_FOUND. */
RADICLE_API radicle_status radicle_nif_anim_read(const uint8_t* bytes, size_t size, radicle_nif_anim** out_anim);
RADICLE_API void radicle_nif_anim_destroy(radicle_nif_anim* anim);

RADICLE_API radicle_status radicle_nif_anim_manager_count(const radicle_nif_anim* anim, size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_read_manager(const radicle_nif_anim* anim,
                                                          size_t index,
                                                          radicle_nif_anim_manager* out_manager);

RADICLE_API radicle_status radicle_nif_anim_sequence_count(const radicle_nif_anim* anim, size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_sequence_name(const radicle_nif_anim* anim,
                                                          size_t index,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_read_sequence(const radicle_nif_anim* anim,
                                                          size_t index,
                                                          radicle_nif_anim_sequence* out_sequence);
RADICLE_API radicle_status radicle_nif_anim_sequence_accum_root(const radicle_nif_anim* anim,
                                                                size_t index,
                                                                uint8_t** out_bytes,
                                                                size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_controlled_count(const radicle_nif_anim* anim,
                                                             size_t index,
                                                             size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_read_controlled(const radicle_nif_anim* anim,
                                                            size_t index,
                                                            size_t slot,
                                                            radicle_nif_anim_controlled* out_controlled);
RADICLE_API radicle_status radicle_nif_anim_controlled_node(const radicle_nif_anim* anim,
                                                             size_t index,
                                                             size_t slot,
                                                             uint8_t** out_bytes,
                                                             size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_controlled_controller_type(const radicle_nif_anim* anim,
                                                                       size_t index,
                                                                       size_t slot,
                                                                       uint8_t** out_bytes,
                                                                       size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_controlled_interpolator_id(const radicle_nif_anim* anim,
                                                                       size_t index,
                                                                       size_t slot,
                                                                       uint8_t** out_bytes,
                                                                       size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_text_key_count(const radicle_nif_anim* anim,
                                                           uint32_t block,
                                                           size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_read_text_key(const radicle_nif_anim* anim,
                                                          uint32_t block,
                                                          size_t index,
                                                          radicle_nif_anim_text_key* out_key);
RADICLE_API radicle_status radicle_nif_anim_text_key_text(const radicle_nif_anim* anim,
                                                          uint32_t block,
                                                          size_t index,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_graph_count(const radicle_nif_anim* anim, size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_read_graph(const radicle_nif_anim* anim,
                                                       size_t index,
                                                       radicle_nif_anim_graph* out_graph);
RADICLE_API radicle_status radicle_nif_anim_graph_file(const radicle_nif_anim* anim,
                                                       size_t index,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);
/* Requires an interpolator block (NiFloatInterpolator or NiPoint3Interpolator) from a controlled block's interpolator_ref; any other block answers PARSE_ERROR. */
RADICLE_API radicle_status radicle_nif_anim_read_value_keys(const radicle_nif_anim* anim,
                                                            uint32_t interpolator_block,
                                                            radicle_nif_anim_value_key_info* out_info);
RADICLE_API radicle_status radicle_nif_anim_read_value_key(const radicle_nif_anim* anim,
                                                           uint32_t interpolator_block,
                                                           size_t index,
                                                           radicle_nif_anim_value_key* out_key);
RADICLE_API radicle_status radicle_nif_anim_evaluate_count(const radicle_nif_anim* anim,
                                                           uint32_t sequence_block,
                                                           float time,
                                                           size_t* out_count);
RADICLE_API radicle_status radicle_nif_anim_evaluate_node(const radicle_nif_anim* anim,
                                                          uint32_t sequence_block,
                                                          float time,
                                                          size_t index,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_nif_anim_evaluate_pose(const radicle_nif_anim* anim,
                                                          uint32_t sequence_block,
                                                          float time,
                                                          size_t index,
                                                          radicle_nif_anim_pose* out_pose);

#ifdef __cplusplus
}
#endif
