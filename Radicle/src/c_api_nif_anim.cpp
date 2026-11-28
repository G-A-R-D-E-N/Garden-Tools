#include "fo4/c_api_nif_anim.h"

#include "c_api_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/anim.hpp"
#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;

constexpr CopyMessages k_copy_messages{"out_bytes and out_size are required", "failed to allocate a NIF string"};

}  // namespace

struct radicle_nif_anim {
  fo4::nif::NifFile file;
  std::vector<fo4::nif::AnimSequence> sequences;
  std::vector<fo4::nif::AnimManager> managers;
  std::vector<fo4::nif::AnimBehaviourGraph> graphs;
};

namespace {

const fo4::nif::AnimSequence* find_sequence(const radicle_nif_anim* anim, size_t index, radicle_status& status) {
  if (!anim) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
    return nullptr;
  }
  if (index >= anim->sequences.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "sequence index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &anim->sequences[index];
}

const fo4::nif::AnimManager* find_manager(const radicle_nif_anim* anim, size_t index, radicle_status& status) {
  if (!anim) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
    return nullptr;
  }
  if (index >= anim->managers.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "manager index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &anim->managers[index];
}

}  // namespace

extern "C" {

radicle_status radicle_nif_anim_read(const uint8_t* bytes, size_t size, radicle_nif_anim** out_anim) {
  clear_error();
  if (out_anim) *out_anim = nullptr;
  if (!bytes || size == 0 || !out_anim) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_anim are required");
  }
  return guard_exceptions("unknown exception while reading NIF animation", [&] {
    auto file = fo4::nif::read_nif(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    auto handle = std::make_unique<radicle_nif_anim>();
    handle->file = std::move(file);
    handle->sequences = fo4::nif::read_anim_sequences(handle->file);
    handle->managers = fo4::nif::read_anim_managers(handle->file);
    for (std::uint32_t i = 0; i < handle->file.blocks().size(); ++i) {
      if (handle->file.blocks()[i].type == "BSBehaviorGraphExtraData") {
        handle->graphs.push_back(fo4::nif::read_behaviour_graph(handle->file, i));
      }
    }
    *out_anim = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_nif_anim_destroy(radicle_nif_anim* anim) {
  clear_error();
  delete anim;
}

radicle_status radicle_nif_anim_manager_count(const radicle_nif_anim* anim, size_t* out_count) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = anim->managers.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_read_manager(const radicle_nif_anim* anim, size_t index,
                                               radicle_nif_anim_manager* out_manager) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* manager = find_manager(anim, index, status);
  if (!manager) return status;
  if (!out_manager) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_manager is required");
  radicle_nif_anim_manager out{};
  out.block = manager->block;
  out.cumulative = manager->cumulative ? 1 : 0;
  out.sequences = manager->sequences.empty() ? nullptr : manager->sequences.data();
  out.sequence_count = manager->sequences.size();
  *out_manager = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_sequence_count(const radicle_nif_anim* anim, size_t* out_count) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = anim->sequences.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_sequence_name(const radicle_nif_anim* anim, size_t index, uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  return guard_exceptions("unknown exception while reading a sequence name", [&] { return copy_out(seq->name, out_bytes, out_size, k_copy_messages); });
}

radicle_status radicle_nif_anim_read_sequence(const radicle_nif_anim* anim, size_t index,
                                              radicle_nif_anim_sequence* out_sequence) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (!out_sequence) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_sequence is required");
  radicle_nif_anim_sequence out{};
  out.block = seq->block;
  out.weight = seq->weight;
  out.text_keys_ref = seq->text_keys_ref;
  out.cycle = static_cast<uint32_t>(seq->cycle);
  out.frequency = seq->frequency;
  out.start_time = seq->start_time;
  out.stop_time = seq->stop_time;
  out.manager_ref = seq->manager_ref;
  out.controlled_count = seq->controlled.size();
  out.anim_note_count = seq->anim_note_refs.size();
  *out_sequence = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_sequence_accum_root(const radicle_nif_anim* anim, size_t index, uint8_t** out_bytes,
                                                    size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  return guard_exceptions("unknown exception while reading accum root", [&] { return copy_out(seq->accum_root_name, out_bytes, out_size, k_copy_messages); });
}

radicle_status radicle_nif_anim_controlled_count(const radicle_nif_anim* anim, size_t index, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = seq->controlled.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_read_controlled(const radicle_nif_anim* anim, size_t index, size_t slot,
                                                radicle_nif_anim_controlled* out_controlled) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (slot >= seq->controlled.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "slot is out of range");
  if (!out_controlled) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_controlled is required");
  radicle_nif_anim_controlled out{};
  out.interpolator_ref = seq->controlled[slot].interpolator_ref;
  out.controller_ref = seq->controlled[slot].controller_ref;
  out.priority = seq->controlled[slot].priority;
  *out_controlled = out;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_controlled_node(const radicle_nif_anim* anim, size_t index, size_t slot,
                                                uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (slot >= seq->controlled.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "slot is out of range");
  return guard_exceptions("unknown exception while reading a controlled node", [&] {
    return copy_out(seq->controlled[slot].node_name, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_controlled_controller_type(const radicle_nif_anim* anim, size_t index, size_t slot,
                                                           uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (slot >= seq->controlled.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "slot is out of range");
  return guard_exceptions("unknown exception while reading a controller type", [&] {
    return copy_out(seq->controlled[slot].controller_type, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_controlled_interpolator_id(const radicle_nif_anim* anim, size_t index, size_t slot,
                                                           uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* seq = find_sequence(anim, index, status);
  if (!seq) return status;
  if (slot >= seq->controlled.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "slot is out of range");
  return guard_exceptions("unknown exception while reading an interpolator id", [&] {
    return copy_out(seq->controlled[slot].interpolator_id, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_text_key_count(const radicle_nif_anim* anim, uint32_t block, size_t* out_count) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  return guard_exceptions("unknown exception while counting text keys", [&] {
    *out_count = fo4::nif::read_text_keys(anim->file, block).size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_anim_read_text_key(const radicle_nif_anim* anim, uint32_t block, size_t index,
                                              radicle_nif_anim_text_key* out_key) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_key is required");
  return guard_exceptions("unknown exception while reading a text key", [&] {
    const auto keys = fo4::nif::read_text_keys(anim->file, block);
    if (index >= keys.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
    out_key->time = keys[index].time;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_anim_text_key_text(const radicle_nif_anim* anim, uint32_t block, size_t index,
                                              uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  return guard_exceptions("unknown exception while reading text key text", [&] {
    const auto keys = fo4::nif::read_text_keys(anim->file, block);
    if (index >= keys.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
    return copy_out(keys[index].text, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_graph_count(const radicle_nif_anim* anim, size_t* out_count) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = anim->graphs.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_read_graph(const radicle_nif_anim* anim, size_t index, radicle_nif_anim_graph* out_graph) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (index >= anim->graphs.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
  if (!out_graph) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_graph is required");
  out_graph->block = anim->graphs[index].block;
  out_graph->controls_base_skeleton = anim->graphs[index].controls_base_skeleton ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_anim_graph_file(const radicle_nif_anim* anim, size_t index, uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (index >= anim->graphs.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
  return guard_exceptions("unknown exception while reading a graph file", [&] {
    return copy_out(anim->graphs[index].graph_file, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_read_value_keys(const radicle_nif_anim* anim, uint32_t interpolator_block,
                                                radicle_nif_anim_value_key_info* out_info) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  return guard_exceptions("unknown exception while reading value keys", [&] {
    const auto keys = fo4::nif::read_value_keys(anim->file, interpolator_block);
    radicle_nif_anim_value_key_info out{};
    out.components = keys.components;
    out.interpolation = keys.interpolation;
    out.data_ref = keys.data_ref;
    std::memcpy(out.pose, keys.pose, sizeof out.pose);
    out.key_count = keys.keys.size();
    *out_info = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_anim_read_value_key(const radicle_nif_anim* anim, uint32_t interpolator_block, size_t index,
                                               radicle_nif_anim_value_key* out_key) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_key is required");
  return guard_exceptions("unknown exception while reading a value key", [&] {
    const auto keys = fo4::nif::read_value_keys(anim->file, interpolator_block);
    if (index >= keys.keys.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
    const auto& key = keys.keys[index];
    radicle_nif_anim_value_key out{};
    out.time = key.time;
    std::memcpy(out.value, key.value, sizeof out.value);
    std::memcpy(out.forward, key.forward, sizeof out.forward);
    std::memcpy(out.backward, key.backward, sizeof out.backward);
    std::memcpy(out.tbc, key.tbc, sizeof out.tbc);
    *out_key = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_anim_evaluate_count(const radicle_nif_anim* anim, uint32_t sequence_block, float time,
                                               size_t* out_count) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  return guard_exceptions("unknown exception while evaluating", [&] {
    *out_count = fo4::nif::evaluate_sequence(anim->file, sequence_block, time).size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_anim_evaluate_node(const radicle_nif_anim* anim, uint32_t sequence_block, float time,
                                              size_t index, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  return guard_exceptions("unknown exception while evaluating a node name", [&] {
    const auto poses = fo4::nif::evaluate_sequence(anim->file, sequence_block, time);
    if (index >= poses.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
    return copy_out(poses[index].node_name, out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_anim_evaluate_pose(const radicle_nif_anim* anim, uint32_t sequence_block, float time,
                                              size_t index, radicle_nif_anim_pose* out_pose) {
  clear_error();
  if (!anim) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "anim is required");
  if (!out_pose) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_pose is required");
  return guard_exceptions("unknown exception while evaluating a pose", [&] {
    const auto poses = fo4::nif::evaluate_sequence(anim->file, sequence_block, time);
    if (index >= poses.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "index is out of range");
    radicle_nif_anim_pose out{};
    out.translation[0] = poses[index].pose.translation[0];
    out.translation[1] = poses[index].pose.translation[1];
    out.translation[2] = poses[index].pose.translation[2];
    out.rotation[0] = poses[index].pose.rotation[0];
    out.rotation[1] = poses[index].pose.rotation[1];
    out.rotation[2] = poses[index].pose.rotation[2];
    out.rotation[3] = poses[index].pose.rotation[3];
    out.scale = poses[index].pose.scale;
    *out_pose = out;
    return RADICLE_STATUS_OK;
  });
}

}
