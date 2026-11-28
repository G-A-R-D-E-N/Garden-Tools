#include "fo4/c_api_hkx_behavior.h"

#include "c_api_hkx_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/hkx/behavior_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using fo4::hkx::HkxGraphModel;
using radicle_hkx_detail::need;

constexpr CopyMessages k_string{"out_bytes and out_size are required", "failed to allocate a behaviour string"};

const HkxGraphModel* model_of(const radicle_hkx* hkx, radicle_status& status) {
  return need(hkx, &radicle_hkx::graph, "behavior graph", status);
}

template <typename Row>
const Row* row_of(const radicle_hkx* hkx,
                  std::vector<Row> HkxGraphModel::*table,
                  size_t index,
                  const char* what,
                  radicle_status& status) {
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return nullptr;
  const std::vector<Row>& rows = model->*table;
  if (index >= rows.size()) {
    std::string message = std::string(what) + " index is out of range";
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    return nullptr;
  }
  return &rows[index];
}

template <typename Row, typename Info, typename Fill>
radicle_status read_row(const radicle_hkx* hkx,
                        std::vector<Row> HkxGraphModel::*table,
                        size_t index,
                        const char* what,
                        Info* out_info,
                        Fill fill) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const Row* row = row_of(hkx, table, index, what, status);
  if (!row) return status;
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  Info info{};
  fill(*row, info);
  *out_info = info;
  return RADICLE_STATUS_OK;
}

template <typename Row, typename Pick>
radicle_status read_text(const radicle_hkx* hkx,
                         std::vector<Row> HkxGraphModel::*table,
                         size_t index,
                         const char* what,
                         uint8_t** out_bytes,
                         size_t* out_size,
                         Pick pick) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const Row* row = row_of(hkx, table, index, what, status);
  if (!row) return status;
  return copy_out(pick(*row), out_bytes, out_size, k_string);
}

}  // namespace

radicle_status radicle_hkx_behavior_read_summary(const radicle_hkx* hkx, radicle_hkx_behavior_summary* out_summary) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return status;
  if (!out_summary) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_summary is required");
  radicle_hkx_behavior_summary out{};
  out.root_node = model->root_node;
  out.event_count = model->events.size();
  out.variable_count = model->variables.size();
  out.node_count = model->nodes.size();
  out.state_machine_count = model->state_machines.size();
  out.state_count = model->states.size();
  out.transition_count = model->transitions.size();
  out.blender_count = model->blenders.size();
  out.blend_child_count = model->blend_children.size();
  out.clip_count = model->clips.size();
  out.binding_count = model->bindings.size();
  out.diagnostic_count = model->diagnostics.size();
  *out_summary = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_behavior_graph_name(const radicle_hkx* hkx, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return status;
  return copy_out(model->graph_name, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_behavior_diagnostic(const radicle_hkx* hkx,
                                               size_t index,
                                               uint8_t** out_bytes,
                                               size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return status;
  if (index >= model->diagnostics.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "diagnostic index is out of range");
  return copy_out(model->diagnostics[index], out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_behavior_event_read_info(const radicle_hkx* hkx,
                                                    size_t index,
                                                    radicle_hkx_behavior_event_info* out_info) {
  return read_row(hkx, &HkxGraphModel::events, index, "event", out_info,
                  [](const fo4::hkx::HkxGraphEvent& row, radicle_hkx_behavior_event_info& info) {
                    info.flags = row.flags;
                  });
}

radicle_status radicle_hkx_behavior_event_name(const radicle_hkx* hkx,
                                               size_t index,
                                               uint8_t** out_bytes,
                                               size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::events, index, "event", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphEvent& row) -> const std::string& { return row.name; });
}

radicle_status radicle_hkx_behavior_variable_read_info(const radicle_hkx* hkx,
                                                       size_t index,
                                                       radicle_hkx_behavior_variable_info* out_info) {
  return read_row(hkx, &HkxGraphModel::variables, index, "variable", out_info,
                  [](const fo4::hkx::HkxGraphVariable& row, radicle_hkx_behavior_variable_info& info) {
                    info.type = row.type;
                    info.role = row.role;
                    info.role_flags = row.role_flags;
                    info.word = row.word;
                    info.int_value = row.int_value;
                    info.real_value = row.real_value;
                    info.has_vector = row.has_vector ? 1 : 0;
                    for (size_t lane = 0; lane < 4; ++lane) info.vector[lane] = row.vector[lane];
                  });
}

radicle_status radicle_hkx_behavior_variable_name(const radicle_hkx* hkx,
                                                  size_t index,
                                                  uint8_t** out_bytes,
                                                  size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::variables, index, "variable", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphVariable& row) -> const std::string& { return row.name; });
}

radicle_status radicle_hkx_behavior_node_read_info(const radicle_hkx* hkx,
                                                   size_t index,
                                                   radicle_hkx_behavior_node_info* out_info) {
  return read_row(hkx, &HkxGraphModel::nodes, index, "node", out_info,
                  [](const fo4::hkx::HkxGraphNode& row, radicle_hkx_behavior_node_info& info) {
                    info.object = row.object;
                    info.kind = static_cast<int32_t>(row.kind);
                    info.kind_index = row.kind_index;
                  });
}

radicle_status radicle_hkx_behavior_node_name(const radicle_hkx* hkx,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::nodes, index, "node", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphNode& row) -> const std::string& { return row.name; });
}

radicle_status radicle_hkx_behavior_node_class(const radicle_hkx* hkx,
                                               size_t index,
                                               uint8_t** out_bytes,
                                               size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::nodes, index, "node", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphNode& row) -> const std::string& { return row.class_name; });
}

radicle_status radicle_hkx_behavior_state_machine_read_info(const radicle_hkx* hkx,
                                                            size_t index,
                                                            radicle_hkx_behavior_state_machine_info* out_info) {
  return read_row(hkx, &HkxGraphModel::state_machines, index, "state machine", out_info,
                  [](const fo4::hkx::HkxGraphStateMachine& row, radicle_hkx_behavior_state_machine_info& info) {
                    info.node = row.node;
                    info.start_state_id = row.start_state_id;
                    info.change_event_id = row.change_event_id;
                    info.return_to_previous_event_id = row.return_to_previous_event_id;
                    info.random_transition_event_id = row.random_transition_event_id;
                    info.next_higher_event_id = row.next_higher_event_id;
                    info.next_lower_event_id = row.next_lower_event_id;
                    info.sync_variable_index = row.sync_variable_index;
                    info.start_state_mode = row.start_state_mode;
                    info.self_transition_mode = row.self_transition_mode;
                    info.first_state = row.first_state;
                    info.state_count = row.state_count;
                    info.first_wildcard = row.first_wildcard;
                    info.wildcard_count = row.wildcard_count;
                  });
}

radicle_status radicle_hkx_behavior_state_read_info(const radicle_hkx* hkx,
                                                    size_t index,
                                                    radicle_hkx_behavior_state_info* out_info) {
  return read_row(hkx, &HkxGraphModel::states, index, "state", out_info,
                  [](const fo4::hkx::HkxGraphState& row, radicle_hkx_behavior_state_info& info) {
                    info.state_machine = row.state_machine;
                    info.state_id = row.state_id;
                    info.probability = row.probability;
                    info.enable = row.enable ? 1 : 0;
                    info.generator_node = row.generator_node;
                    info.first_transition = row.first_transition;
                    info.transition_count = row.transition_count;
                  });
}

radicle_status radicle_hkx_behavior_state_name(const radicle_hkx* hkx,
                                               size_t index,
                                               uint8_t** out_bytes,
                                               size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::states, index, "state", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphState& row) -> const std::string& { return row.name; });
}

radicle_status radicle_hkx_behavior_transition_read_info(const radicle_hkx* hkx,
                                                         size_t index,
                                                         radicle_hkx_behavior_transition_info* out_info) {
  return read_row(hkx, &HkxGraphModel::transitions, index, "transition", out_info,
                  [](const fo4::hkx::HkxGraphTransition& row, radicle_hkx_behavior_transition_info& info) {
                    info.state_machine = row.state_machine;
                    info.state = row.state;
                    info.event_id = row.event_id;
                    info.to_state_id = row.to_state_id;
                    info.from_nested_state_id = row.from_nested_state_id;
                    info.to_nested_state_id = row.to_nested_state_id;
                    info.priority = row.priority;
                    info.flags = row.flags;
                    info.trigger_enter_event_id = row.trigger_interval.enter_event_id;
                    info.trigger_exit_event_id = row.trigger_interval.exit_event_id;
                    info.trigger_enter_time = row.trigger_interval.enter_time;
                    info.trigger_exit_time = row.trigger_interval.exit_time;
                    info.initiate_enter_event_id = row.initiate_interval.enter_event_id;
                    info.initiate_exit_event_id = row.initiate_interval.exit_event_id;
                    info.initiate_enter_time = row.initiate_interval.enter_time;
                    info.initiate_exit_time = row.initiate_interval.exit_time;
                    info.effect_node = row.effect_node;
                    info.condition_kind = static_cast<int32_t>(row.condition_kind);
                  });
}

radicle_status radicle_hkx_behavior_transition_condition(const radicle_hkx* hkx,
                                                         size_t index,
                                                         uint8_t** out_bytes,
                                                         size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::transitions, index, "transition", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphTransition& row) -> const std::string& { return row.condition; });
}

radicle_status radicle_hkx_behavior_transition_condition_class(const radicle_hkx* hkx,
                                                               size_t index,
                                                               uint8_t** out_bytes,
                                                               size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::transitions, index, "transition", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphTransition& row) -> const std::string& { return row.condition_class; });
}

radicle_status radicle_hkx_behavior_blender_read_info(const radicle_hkx* hkx,
                                                      size_t index,
                                                      radicle_hkx_behavior_blender_info* out_info) {
  return read_row(hkx, &HkxGraphModel::blenders, index, "blender", out_info,
                  [](const fo4::hkx::HkxGraphBlender& row, radicle_hkx_behavior_blender_info& info) {
                    info.node = row.node;
                    info.blend_parameter = row.blend_parameter;
                    info.min_cyclic_blend_parameter = row.min_cyclic_blend_parameter;
                    info.max_cyclic_blend_parameter = row.max_cyclic_blend_parameter;
                    info.index_of_sync_master_child = row.index_of_sync_master_child;
                    info.flags = row.flags;
                    info.parametric = row.parametric ? 1 : 0;
                    info.subtract_last_child = row.subtract_last_child ? 1 : 0;
                    info.blend_parameter_variable = row.blend_parameter_variable;
                    info.resolved = row.resolved ? 1 : 0;
                    info.first_child = row.first_child;
                    info.child_count = row.child_count;
                  });
}

radicle_status radicle_hkx_behavior_blend_child_read_info(const radicle_hkx* hkx,
                                                          size_t index,
                                                          radicle_hkx_behavior_blend_child_info* out_info) {
  return read_row(hkx, &HkxGraphModel::blend_children, index, "blend child", out_info,
                  [](const fo4::hkx::HkxGraphBlendChild& row, radicle_hkx_behavior_blend_child_info& info) {
                    info.blender = row.blender;
                    info.generator_node = row.generator_node;
                    info.weight = row.weight;
                    info.world_from_model_weight = row.world_from_model_weight;
                    info.has_bone_weights = row.has_bone_weights ? 1 : 0;
                    info.weight_variable = row.weight_variable;
                    info.contribution = row.contribution;
                  });
}

radicle_status radicle_hkx_behavior_clip_read_info(const radicle_hkx* hkx,
                                                   size_t index,
                                                   radicle_hkx_behavior_clip_info* out_info) {
  return read_row(hkx, &HkxGraphModel::clips, index, "clip", out_info,
                  [](const fo4::hkx::HkxGraphClip& row, radicle_hkx_behavior_clip_info& info) {
                    info.node = row.node;
                    info.mode = row.mode;
                    info.flags = row.flags;
                    info.start_time = row.start_time;
                    info.playback_speed = row.playback_speed;
                    info.crop_start = row.crop_start;
                    info.crop_end = row.crop_end;
                    info.enforced_duration = row.enforced_duration;
                    info.trigger_count = row.triggers.size();
                  });
}

radicle_status radicle_hkx_behavior_clip_animation(const radicle_hkx* hkx,
                                                   size_t index,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::clips, index, "clip", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphClip& row) -> const std::string& { return row.animation; });
}

radicle_status radicle_hkx_behavior_clip_bundle(const radicle_hkx* hkx,
                                                size_t index,
                                                uint8_t** out_bytes,
                                                size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::clips, index, "clip", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphClip& row) -> const std::string& { return row.animation_bundle; });
}

radicle_status radicle_hkx_behavior_clip_trigger_read_info(const radicle_hkx* hkx,
                                                           size_t clip,
                                                           size_t trigger,
                                                           radicle_hkx_behavior_clip_trigger_info* out_info) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* row = row_of(hkx, &HkxGraphModel::clips, clip, "clip", status);
  if (!row) return status;
  if (trigger >= row->triggers.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "trigger index is out of range");
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  const fo4::hkx::HkxGraphClipTrigger& source = row->triggers[trigger];
  radicle_hkx_behavior_clip_trigger_info info{};
  info.local_time = source.local_time;
  info.event_id = source.event_id;
  info.relative_to_end_of_clip = source.relative_to_end_of_clip ? 1 : 0;
  info.acyclic = source.acyclic ? 1 : 0;
  info.is_annotation = source.is_annotation ? 1 : 0;
  *out_info = info;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_behavior_effect_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = model->effects.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_behavior_effect_read_info(const radicle_hkx* hkx,
                                                     size_t index,
                                                     radicle_hkx_behavior_effect_info* out_info) {
  return read_row(hkx, &HkxGraphModel::effects, index, "effect", out_info,
                  [](const fo4::hkx::HkxGraphEffect& row, radicle_hkx_behavior_effect_info& info) {
                    info.node = row.node;
                    info.duration = row.duration;
                    info.start_fraction = row.start_fraction;
                    info.flags = row.flags;
                    info.end_mode = row.end_mode;
                    info.blend_curve = row.blend_curve;
                  });
}

radicle_status radicle_hkx_behavior_binding_read_info(const radicle_hkx* hkx,
                                                      size_t index,
                                                      radicle_hkx_behavior_binding_info* out_info) {
  return read_row(hkx, &HkxGraphModel::bindings, index, "binding", out_info,
                  [](const fo4::hkx::HkxGraphBinding& row, radicle_hkx_behavior_binding_info& info) {
                    info.owner_kind = static_cast<int32_t>(row.owner_kind);
                    info.owner_index = row.owner_index;
                    info.variable_index = row.variable_index;
                    info.bit_index = row.bit_index;
                    info.binding_type = row.binding_type;
                  });
}

radicle_status radicle_hkx_behavior_edge_count(const radicle_hkx* hkx, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const HkxGraphModel* model = model_of(hkx, status);
  if (!model) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = model->edges.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_behavior_edge_read_info(const radicle_hkx* hkx,
                                                   size_t index,
                                                   radicle_hkx_behavior_edge_info* out_info) {
  return read_row(hkx, &HkxGraphModel::edges, index, "edge", out_info,
                  [](const fo4::hkx::HkxGraphEdge& row, radicle_hkx_behavior_edge_info& info) {
                    info.parent = row.parent;
                    info.child = row.child;
                    info.role = static_cast<int32_t>(row.role);
                    info.slot = row.slot;
                    info.weight = row.weight;
                    info.has_bone_weights = row.has_bone_weights ? 1 : 0;
                  });
}

radicle_status radicle_hkx_behavior_binding_member_path(const radicle_hkx* hkx,
                                                        size_t index,
                                                        uint8_t** out_bytes,
                                                        size_t* out_size) {
  return read_text(hkx, &HkxGraphModel::bindings, index, "binding", out_bytes, out_size,
                   [](const fo4::hkx::HkxGraphBinding& row) -> const std::string& { return row.member_path; });
}
