#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_hkx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Requires a behaviour packfile read by radicle_hkx_read; node, generator and effect indexes are valid or -1, event ids and variable indexes are raw file values. */
typedef struct radicle_hkx_behavior_summary {
  int32_t root_node;
  size_t event_count;
  size_t variable_count;
  size_t node_count;
  size_t state_machine_count;
  size_t state_count;
  size_t transition_count;
  size_t blender_count;
  size_t blend_child_count;
  size_t clip_count;
  size_t binding_count;
  size_t diagnostic_count;
} radicle_hkx_behavior_summary;

typedef struct radicle_hkx_behavior_event_info {
  uint32_t flags;
} radicle_hkx_behavior_event_info;

/* Invariant: type is the hkbVariableInfo enum (0 bool, 1 int8, 2 int16, 3 int32, 4 real, 5 pointer, 6 vector3, 7 vector4, 8 quaternion); word is the 32-bit initial value, int_value and real_value are two views of it, and vector is set only when has_vector is 1. */
typedef struct radicle_hkx_behavior_variable_info {
  int32_t type;
  int32_t role;
  uint32_t role_flags;
  uint32_t word;
  int32_t int_value;
  float real_value;
  int32_t has_vector;
  float vector[4];
} radicle_hkx_behavior_variable_info;

/* Invariant: object indexes the packfile's object list; kind is 0 other, 1 state machine, 2 blender, 3 clip, 4 manual selector, 5 layer generator, 6 pose matching, 7 modifier generator, 8 state tagging, 9 cyclic blend transition, 10 offset animation, 11 behavior graph swap, 12 bone switch, 13 behavior reference, 14 gamebryo sequence, 15 dynamic animation tagging or 16 reference pose; kind_index indexes the table of kinds 1 to 3 and is 0 for every other kind. */
typedef struct radicle_hkx_behavior_node_info {
  size_t object;
  int32_t kind;
  size_t kind_index;
} radicle_hkx_behavior_node_info;

/* Invariant: the states and wildcard transitions are contiguous runs of the state and transition tables. */
typedef struct radicle_hkx_behavior_state_machine_info {
  size_t node;
  int32_t start_state_id;
  int32_t change_event_id;
  int32_t return_to_previous_event_id;
  int32_t random_transition_event_id;
  int32_t next_higher_event_id;
  int32_t next_lower_event_id;
  int32_t sync_variable_index;
  int32_t start_state_mode;
  int32_t self_transition_mode;
  size_t first_state;
  size_t state_count;
  size_t first_wildcard;
  size_t wildcard_count;
} radicle_hkx_behavior_state_machine_info;

typedef struct radicle_hkx_behavior_state_info {
  size_t state_machine;
  int32_t state_id;
  float probability;
  int32_t enable;
  int32_t generator_node;
  size_t first_transition;
  size_t transition_count;
} radicle_hkx_behavior_state_info;

/* Invariant: state is the owning state's index in the state table, -1 for a state machine wildcard; condition_kind is 0 none, 1 expression, 2 string or 3 another class, and only kinds 1 and 2 carry condition text. */
typedef struct radicle_hkx_behavior_transition_info {
  size_t state_machine;
  int32_t state;
  int32_t event_id;
  int32_t to_state_id;
  int32_t from_nested_state_id;
  int32_t to_nested_state_id;
  int32_t priority;
  uint32_t flags;
  int32_t trigger_enter_event_id;
  int32_t trigger_exit_event_id;
  float trigger_enter_time;
  float trigger_exit_time;
  int32_t initiate_enter_event_id;
  int32_t initiate_exit_event_id;
  float initiate_enter_time;
  float initiate_exit_time;
  int32_t effect_node;
  int32_t condition_kind;
} radicle_hkx_behavior_transition_info;

/* Invariant: resolved is 1 only when the file alone fixes every child's contribution: no binding drives the blend parameter or a weight, no bone weights, no flag beyond parametric, no subtractive last child. */
typedef struct radicle_hkx_behavior_blender_info {
  size_t node;
  float blend_parameter;
  float min_cyclic_blend_parameter;
  float max_cyclic_blend_parameter;
  int32_t index_of_sync_master_child;
  int32_t flags;
  int32_t parametric;
  int32_t subtract_last_child;
  int32_t blend_parameter_variable;
  int32_t resolved;
  size_t first_child;
  size_t child_count;
} radicle_hkx_behavior_blender_info;

/* Invariant: weight_variable is the variable bound to the weight, -1 when none or when the binding is a character property; contribution is the static share and is only meaningful when the blender is resolved. */
typedef struct radicle_hkx_behavior_blend_child_info {
  size_t blender;
  int32_t generator_node;
  float weight;
  float world_from_model_weight;
  int32_t has_bone_weights;
  int32_t weight_variable;
  float contribution;
} radicle_hkx_behavior_blend_child_info;

typedef struct radicle_hkx_behavior_clip_info {
  size_t node;
  int32_t mode;
  uint32_t flags;
  float start_time;
  float playback_speed;
  float crop_start;
  float crop_end;
  float enforced_duration;
  size_t trigger_count;
} radicle_hkx_behavior_clip_info;

typedef struct radicle_hkx_behavior_clip_trigger_info {
  float local_time;
  int32_t event_id;
  int32_t relative_to_end_of_clip;
  int32_t acyclic;
  int32_t is_annotation;
} radicle_hkx_behavior_clip_trigger_info;

/* Invariant: node indexes the node table and equals a transition's effect_node; flags, end_mode and blend_curve are the file's raw hkbBlendingTransitionEffect values; a file with no such effect has zero rows. */
typedef struct radicle_hkx_behavior_effect_info {
  size_t node;
  float duration;
  float start_fraction;
  uint32_t flags;
  int32_t end_mode;
  int32_t blend_curve;
} radicle_hkx_behavior_effect_info;

/* Invariant: owner_kind is 1 state machine, 2 state, 3 blender, 4 blend child or 5 clip, and owner_index indexes that table; binding_type is the file's value (0 variable, 1 character property). */
typedef struct radicle_hkx_behavior_binding_info {
  int32_t owner_kind;
  size_t owner_index;
  int32_t variable_index;
  int32_t bit_index;
  int32_t binding_type;
} radicle_hkx_behavior_binding_info;

/* Invariant: parent and child index the node table; role is 1 wrapped default generator, 2 offset clip, 3 selector entry, 4 layer, 5 bone switch child or 6 pose matching child; slot is the entry's index in its parent's list (0 for a single pointer); weight is the stored weight for roles 4 and 6 and 0 otherwise; the edges of one parent are contiguous in slot order. */
typedef struct radicle_hkx_behavior_edge_info {
  size_t parent;
  int32_t child;
  int32_t role;
  int32_t slot;
  float weight;
  int32_t has_bone_weights;
} radicle_hkx_behavior_edge_info;

RADICLE_API radicle_status radicle_hkx_behavior_read_summary(const radicle_hkx* hkx,
                                                             radicle_hkx_behavior_summary* out_summary);
RADICLE_API radicle_status radicle_hkx_behavior_graph_name(const radicle_hkx* hkx,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);
RADICLE_API radicle_status radicle_hkx_behavior_diagnostic(const radicle_hkx* hkx,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_event_read_info(const radicle_hkx* hkx,
                                                                size_t index,
                                                                radicle_hkx_behavior_event_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_event_name(const radicle_hkx* hkx,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_variable_read_info(const radicle_hkx* hkx,
                                                                   size_t index,
                                                                   radicle_hkx_behavior_variable_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_variable_name(const radicle_hkx* hkx,
                                                              size_t index,
                                                              uint8_t** out_bytes,
                                                              size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_node_read_info(const radicle_hkx* hkx,
                                                               size_t index,
                                                               radicle_hkx_behavior_node_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_node_name(const radicle_hkx* hkx,
                                                          size_t index,
                                                          uint8_t** out_bytes,
                                                          size_t* out_size);
RADICLE_API radicle_status radicle_hkx_behavior_node_class(const radicle_hkx* hkx,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_state_machine_read_info(
    const radicle_hkx* hkx, size_t index, radicle_hkx_behavior_state_machine_info* out_info);

RADICLE_API radicle_status radicle_hkx_behavior_state_read_info(const radicle_hkx* hkx,
                                                                size_t index,
                                                                radicle_hkx_behavior_state_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_state_name(const radicle_hkx* hkx,
                                                           size_t index,
                                                           uint8_t** out_bytes,
                                                           size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_transition_read_info(const radicle_hkx* hkx,
                                                                     size_t index,
                                                                     radicle_hkx_behavior_transition_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_transition_condition(const radicle_hkx* hkx,
                                                                     size_t index,
                                                                     uint8_t** out_bytes,
                                                                     size_t* out_size);
RADICLE_API radicle_status radicle_hkx_behavior_transition_condition_class(const radicle_hkx* hkx,
                                                                           size_t index,
                                                                           uint8_t** out_bytes,
                                                                           size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_blender_read_info(const radicle_hkx* hkx,
                                                                  size_t index,
                                                                  radicle_hkx_behavior_blender_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_blend_child_read_info(const radicle_hkx* hkx,
                                                                      size_t index,
                                                                      radicle_hkx_behavior_blend_child_info* out_info);

RADICLE_API radicle_status radicle_hkx_behavior_clip_read_info(const radicle_hkx* hkx,
                                                               size_t index,
                                                               radicle_hkx_behavior_clip_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_clip_animation(const radicle_hkx* hkx,
                                                               size_t index,
                                                               uint8_t** out_bytes,
                                                               size_t* out_size);
RADICLE_API radicle_status radicle_hkx_behavior_clip_bundle(const radicle_hkx* hkx,
                                                            size_t index,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);
RADICLE_API radicle_status radicle_hkx_behavior_clip_trigger_read_info(
    const radicle_hkx* hkx, size_t clip, size_t trigger, radicle_hkx_behavior_clip_trigger_info* out_info);

RADICLE_API radicle_status radicle_hkx_behavior_effect_count(const radicle_hkx* hkx, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_behavior_effect_read_info(const radicle_hkx* hkx,
                                                                 size_t index,
                                                                 radicle_hkx_behavior_effect_info* out_info);

RADICLE_API radicle_status radicle_hkx_behavior_binding_read_info(const radicle_hkx* hkx,
                                                                  size_t index,
                                                                  radicle_hkx_behavior_binding_info* out_info);
RADICLE_API radicle_status radicle_hkx_behavior_binding_member_path(const radicle_hkx* hkx,
                                                                    size_t index,
                                                                    uint8_t** out_bytes,
                                                                    size_t* out_size);

RADICLE_API radicle_status radicle_hkx_behavior_edge_count(const radicle_hkx* hkx, size_t* out_count);
RADICLE_API radicle_status radicle_hkx_behavior_edge_read_info(const radicle_hkx* hkx,
                                                               size_t index,
                                                               radicle_hkx_behavior_edge_info* out_info);

#ifdef __cplusplus
}
#endif
