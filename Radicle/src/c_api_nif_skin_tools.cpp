#include "fo4/c_api_nif_skin_tools.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/customization_remap.hpp"
#include "fo4/nif/weight_brush.hpp"
#include "fo4/nif/weight_transfer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
namespace nif = fo4::nif;

constexpr const char* k_args = "the skin arrays, counts and every output are required";
constexpr const char* k_alloc = "failed to allocate the skin tool result";
constexpr CopyMessages k_copy_messages{k_args, k_alloc};
constexpr std::size_t k_slots = nif::k_max_influences_per_vertex;
static_assert(k_slots == 4, "the C ABI lays skin weights out as four slots per vertex");
static_assert(nif::k_remap_new_bone_bytes == 208, "the C ABI documents 208-byte new-bone entries");

template <typename T>
bool missing(const T* values, std::size_t count) {
  return count != 0 && values == nullptr;
}

bool too_many(std::size_t count) { return count > std::numeric_limits<std::uint32_t>::max(); }

template <typename T>
std::span<const T> view(const T* values, std::size_t count) {
  return count == 0 ? std::span<const T>() : std::span<const T>(values, count);
}

std::span<const std::byte> bytes_of(const std::uint8_t* values, std::size_t count) {
  return {reinterpret_cast<const std::byte*>(values), count};
}

std::vector<nif::VertexWeights> unpack_weights(const std::uint8_t* bones, const float* weights, std::size_t count) {
  std::vector<nif::VertexWeights> vertices(count);
  for (std::size_t v = 0; v < count; ++v) {
    for (std::size_t k = 0; k < k_slots; ++k) {
      vertices[v].bone[k] = bones[k_slots * v + k];
      vertices[v].weight[k] = weights[k_slots * v + k];
    }
  }
  return vertices;
}

template <typename Item>
void pack_slots(const std::vector<Item>& items, std::vector<std::uint8_t>& bones, std::vector<float>& weights) {
  bones.reserve(k_slots * items.size());
  weights.reserve(k_slots * items.size());
  for (const auto& item : items) {
    for (std::size_t k = 0; k < k_slots; ++k) {
      bones.push_back(item.bone[k]);
      weights.push_back(item.weight[k]);
    }
  }
}

radicle_status copy_slots(const std::vector<std::uint8_t>& bones, const std::vector<float>& weights,
                          std::uint8_t** out_bones, float** out_weights, std::size_t* out_count) {
  std::size_t count = 0;
  auto status = copy_out(bones, out_bones, out_count, k_copy_messages);
  if (status != RADICLE_STATUS_OK) return status;
  status = copy_out(weights, out_weights, &count, k_copy_messages);
  if (status != RADICLE_STATUS_OK) {
    std::free(*out_bones);
    *out_bones = nullptr;
    *out_count = 0;
  }
  return status;
}

nif::CustomizationRemap build_remap(std::int32_t remap_block, std::int32_t new_bones_block, const std::uint8_t* bones,
                                    const float* weights, std::size_t record_count, const std::uint8_t* new_bones,
                                    std::size_t new_bone_count) {
  nif::CustomizationRemap remap;
  remap.remap_block = remap_block;
  remap.new_bones_block = new_bones_block;
  remap.records.resize(record_count);
  for (std::size_t v = 0; v < record_count; ++v) {
    for (std::size_t k = 0; k < k_slots; ++k) {
      remap.records[v].bone[k] = bones[k_slots * v + k];
      remap.records[v].weight[k] = weights[k_slots * v + k];
    }
  }
  remap.new_bones.resize(new_bone_count);
  for (std::size_t i = 0; i < new_bone_count; ++i) {
    std::memcpy(remap.new_bones[i].raw.data(), new_bones + i * nif::k_remap_new_bone_bytes, nif::k_remap_new_bone_bytes);
  }
  return remap;
}

bool remap_args_missing(const std::uint8_t* bones, const float* weights, std::size_t record_count,
                        const std::uint8_t* new_bones, std::size_t new_bone_count) {
  return too_many(record_count) || too_many(new_bone_count) || missing(bones, k_slots * record_count) ||
         missing(weights, k_slots * record_count) || missing(new_bones, new_bone_count * nif::k_remap_new_bone_bytes);
}

}  // namespace

extern "C" {

radicle_status radicle_nif_skin_brush(const float* positions,
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
                                      float* out_max_weight_change) {
  clear_error();
  if (out_bones) *out_bones = nullptr;
  if (out_weights) *out_weights = nullptr;
  if (out_count) *out_count = 0;
  if (out_in_reach) *out_in_reach = 0;
  if (out_changed) *out_changed = 0;
  if (out_skipped) *out_skipped = 0;
  if (out_max_weight_change) *out_max_weight_change = 0.0f;
  if (!out_bones || !out_weights || !out_count || !out_in_reach || !out_changed || !out_skipped ||
      !out_max_weight_change || too_many(vertex_count) || too_many(triangle_index_count) ||
      missing(positions, 3 * vertex_count) || missing(triangles, triangle_index_count) ||
      missing(bones, k_slots * vertex_count) || missing(weights, k_slots * vertex_count) ||
      missing(locked_bones, locked_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  if (mode > static_cast<std::uint32_t>(nif::BrushMode::smooth)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the brush mode must be 0, 1, 2 or 3");
  }
  if (falloff > static_cast<std::uint32_t>(nif::BrushFalloff::constant)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the brush falloff must be 0, 1 or 2");
  }
  return guard_exceptions("unknown exception while painting skin weights", [&] {
    nif::WeightBrush brush;
    brush.mode = static_cast<nif::BrushMode>(mode);
    brush.falloff = static_cast<nif::BrushFalloff>(falloff);
    brush.center = {center_x, center_y, center_z};
    brush.radius = radius;
    brush.strength = strength;
    brush.bone = bone;
    brush.target = target;
    std::vector<nif::Point> points(vertex_count);
    for (std::size_t v = 0; v < vertex_count; ++v) points[v] = {positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]};
    auto vertices = unpack_weights(bones, weights, vertex_count);
    const auto report = nif::apply_weight_brush(points, view(triangles, triangle_index_count), vertices, brush,
                                                view(locked_bones, locked_count));
    std::vector<std::uint8_t> out_bone_slots;
    std::vector<float> out_weight_slots;
    pack_slots(vertices, out_bone_slots, out_weight_slots);
    const auto status = copy_slots(out_bone_slots, out_weight_slots, out_bones, out_weights, out_count);
    if (status != RADICLE_STATUS_OK) return status;
    *out_in_reach = report.in_reach;
    *out_changed = report.changed;
    *out_skipped = report.skipped;
    *out_max_weight_change = report.max_weight_change;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_skin_read(const radicle_nif* nif_handle,
                                     uint32_t block,
                                     uint8_t** out_bones,
                                     float** out_weights,
                                     size_t* out_vertex_count) {
  clear_error();
  if (out_bones) *out_bones = nullptr;
  if (out_weights) *out_weights = nullptr;
  if (out_vertex_count) *out_vertex_count = 0;
  if (!nif_handle || !out_bones || !out_weights || !out_vertex_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the NIF and every output are required");
  }
  return guard_exceptions("unknown exception while reading skin weights", [&] {
    const auto vertices = nif::read_shape_weights(nif_handle->file, block);
    std::vector<std::uint8_t> bone_slots;
    std::vector<float> weight_slots;
    pack_slots(vertices, bone_slots, weight_slots);
    const auto status = copy_slots(bone_slots, weight_slots, out_bones, out_weights, out_vertex_count);
    if (status != RADICLE_STATUS_OK) return status;
    *out_vertex_count = vertices.size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_skin_write(const radicle_nif* nif_handle,
                                      uint32_t block,
                                      const uint8_t* bones,
                                      const float* weights,
                                      size_t vertex_count,
                                      uint8_t** out_bytes,
                                      size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!nif_handle || !out_bytes || !out_size || too_many(vertex_count) || missing(bones, k_slots * vertex_count) ||
      missing(weights, k_slots * vertex_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while writing skin weights", [&] {
    const auto vertices = unpack_weights(bones, weights, vertex_count);
    return copy_out(nif::write_shape_weights(nif_handle->file, block, vertices), out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_skin_transfer(const uint8_t* source_nif,
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
                                         size_t* out_unmapped) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (out_transferred) *out_transferred = 0;
  if (out_out_of_reach) *out_out_of_reach = 0;
  if (out_unmapped) *out_unmapped = 0;
  if (!source_nif || source_size == 0 || !target_nif || target_size == 0 || !out_bytes || !out_size ||
      !out_transferred || !out_out_of_reach || !out_unmapped) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "both NIF buffers and every output are required");
  }
  if (method > static_cast<std::uint32_t>(nif::TransferMethod::barycentric)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the transfer method must be 0 or 1");
  }
  return guard_exceptions("unknown exception while transferring skin weights", [&] {
    nif::WeightTransferOptions options;
    options.method = static_cast<nif::TransferMethod>(method);
    options.max_distance = max_distance;
    nif::WeightTransferReport report;
    const auto bytes = nif::transfer_shape_weights(bytes_of(source_nif, source_size), source_block,
                                                   bytes_of(target_nif, target_size), target_block, options, &report);
    const auto status = copy_out(bytes, out_bytes, out_size, k_copy_messages);
    if (status != RADICLE_STATUS_OK) return status;
    *out_transferred = report.transferred;
    *out_out_of_reach = report.out_of_reach;
    *out_unmapped = report.unmapped;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_remap_read(const radicle_nif* nif_handle,
                                      uint32_t block,
                                      int32_t* out_remap_block,
                                      int32_t* out_new_bones_block,
                                      uint8_t** out_bones,
                                      float** out_weights,
                                      size_t* out_record_count,
                                      uint8_t** out_new_bones,
                                      size_t* out_new_bone_count) {
  clear_error();
  if (out_remap_block) *out_remap_block = -1;
  if (out_new_bones_block) *out_new_bones_block = -1;
  if (out_bones) *out_bones = nullptr;
  if (out_weights) *out_weights = nullptr;
  if (out_record_count) *out_record_count = 0;
  if (out_new_bones) *out_new_bones = nullptr;
  if (out_new_bone_count) *out_new_bone_count = 0;
  if (!nif_handle || !out_remap_block || !out_new_bones_block || !out_bones || !out_weights || !out_record_count ||
      !out_new_bones || !out_new_bone_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the NIF and every output are required");
  }
  return guard_exceptions("unknown exception while reading the customization remap", [&] {
    const auto remap = nif::read_customization_remap(nif_handle->file, block);
    std::vector<std::uint8_t> bone_slots;
    std::vector<float> weight_slots;
    pack_slots(remap.records, bone_slots, weight_slots);
    std::vector<std::uint8_t> raw;
    raw.reserve(remap.new_bones.size() * nif::k_remap_new_bone_bytes);
    for (const auto& entry : remap.new_bones) {
      for (const auto value : entry.raw) raw.push_back(static_cast<std::uint8_t>(value));
    }
    auto status = copy_slots(bone_slots, weight_slots, out_bones, out_weights, out_record_count);
    if (status != RADICLE_STATUS_OK) return status;
    *out_record_count = remap.records.size();
    std::size_t raw_count = 0;
    status = copy_out(raw, out_new_bones, &raw_count, k_copy_messages);
    if (status != RADICLE_STATUS_OK) {
      std::free(*out_bones);
      std::free(*out_weights);
      *out_bones = nullptr;
      *out_weights = nullptr;
      *out_record_count = 0;
      return status;
    }
    *out_new_bone_count = remap.new_bones.size();
    *out_remap_block = remap.remap_block;
    *out_new_bones_block = remap.new_bones_block;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_remap_validate(int32_t remap_block,
                                          const uint8_t* bones,
                                          const float* weights,
                                          size_t record_count,
                                          size_t new_bone_count,
                                          size_t skin_bone_count,
                                          size_t vertex_count,
                                          char*** out_messages,
                                          size_t* out_count) {
  clear_error();
  if (out_messages) *out_messages = nullptr;
  if (out_count) *out_count = 0;
  if (!out_messages || !out_count || too_many(record_count) || missing(bones, k_slots * record_count) ||
      missing(weights, k_slots * record_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  if (new_bone_count > 256) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a remap record cannot index more than 256 bones");
  return guard_exceptions("unknown exception while validating the customization remap", [&] {
    auto remap = build_remap(remap_block, -1, bones, weights, record_count, nullptr, 0);
    remap.new_bones.resize(new_bone_count);
    return copy_strings(nif::validate_customization_remap(remap, skin_bone_count, vertex_count), out_messages, out_count);
  });
}

radicle_status radicle_nif_remap_create(const radicle_nif* nif_handle,
                                        uint32_t block,
                                        const uint8_t* bones,
                                        const float* weights,
                                        size_t record_count,
                                        const uint8_t* new_bones,
                                        size_t new_bone_count,
                                        uint8_t** out_bytes,
                                        size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!nif_handle || !out_bytes || !out_size || remap_args_missing(bones, weights, record_count, new_bones, new_bone_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while creating the customization remap", [&] {
    const auto remap = build_remap(-1, -1, bones, weights, record_count, new_bones, new_bone_count);
    return copy_out(nif::create_customization_remap(nif_handle->file, block, remap), out_bytes, out_size, k_copy_messages);
  });
}

radicle_status radicle_nif_remap_write(const radicle_nif* nif_handle,
                                       uint32_t block,
                                       int32_t remap_block,
                                       int32_t new_bones_block,
                                       const uint8_t* bones,
                                       const float* weights,
                                       size_t record_count,
                                       const uint8_t* new_bones,
                                       size_t new_bone_count,
                                       uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!nif_handle || !out_bytes || !out_size || remap_args_missing(bones, weights, record_count, new_bones, new_bone_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while writing the customization remap", [&] {
    const auto remap = build_remap(remap_block, new_bones_block, bones, weights, record_count, new_bones, new_bone_count);
    return copy_out(nif::write_customization_remap(nif_handle->file, block, remap), out_bytes, out_size, k_copy_messages);
  });
}

}  // extern "C"
