#include "fo4/c_api_nif.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;

template <typename Body>
radicle_status guarded(const char* what, Body body) {
  try {
    return body();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::parse_error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

// Constraint: one malloc-and-copy must serve both names and collision payloads.
template <typename Bytes>
radicle_status copy_bytes(const Bytes& value, uint8_t** out_bytes, size_t* out_size) {
  if (!out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (value.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate a NIF buffer");
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

radicle_status copy_floats(const std::vector<float>& values, float** out_values, size_t* out_count) {
  if (!out_values || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_values and out_count are required");
  }
  *out_values = nullptr;
  *out_count = 0;
  if (values.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<float*>(std::malloc(values.size() * sizeof(float)));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate NIF floats");
  std::memcpy(buffer, values.data(), values.size() * sizeof(float));
  *out_values = buffer;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

radicle_status copy_indices(const std::vector<std::uint16_t>& values, uint16_t** out_values, size_t* out_count) {
  if (!out_values || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_values and out_count are required");
  }
  *out_values = nullptr;
  *out_count = 0;
  if (values.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint16_t*>(std::malloc(values.size() * sizeof(uint16_t)));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate NIF triangle indices");
  std::memcpy(buffer, values.data(), values.size() * sizeof(uint16_t));
  *out_values = buffer;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

radicle_status copy_node_refs(const std::vector<std::uint32_t>& values, uint32_t** out_values, size_t* out_count) {
  if (!out_values || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_values and out_count are required");
  }
  *out_values = nullptr;
  *out_count = 0;
  if (values.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint32_t*>(std::malloc(values.size() * sizeof(uint32_t)));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate NIF node references");
  std::memcpy(buffer, values.data(), values.size() * sizeof(uint32_t));
  *out_values = buffer;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

radicle_status copy_byte_values(const std::vector<std::uint8_t>& values, uint8_t** out_values, size_t* out_count) {
  if (!out_values || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_values and out_count are required");
  }
  *out_values = nullptr;
  *out_count = 0;
  if (values.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint8_t*>(std::malloc(values.size()));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate NIF bytes");
  std::memcpy(buffer, values.data(), values.size());
  *out_values = buffer;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

const fo4::nif::Shape* find_shape(const radicle_nif* nif, size_t index, radicle_status& status) {
  if (!nif) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif is required");
    return nullptr;
  }
  if (index >= nif->shapes.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "shape index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &nif->shapes[index];
}

const fo4::nif::CollisionBlob* find_collision(const radicle_nif* nif, size_t index, radicle_status& status) {
  if (!nif) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif is required");
    return nullptr;
  }
  if (index >= nif->collisions.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "collision index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &nif->collisions[index];
}

const fo4::nif::Node* find_node(const radicle_nif* nif, size_t index, radicle_status& status) {
  if (!nif) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif is required");
    return nullptr;
  }
  if (index >= nif->file.nodes().size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "node index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &nif->file.nodes()[index];
}

}

extern "C" {

radicle_status radicle_nif_read(const uint8_t* bytes, size_t size, radicle_nif** out_nif) {
  clear_error();
  if (out_nif) *out_nif = nullptr;
  if (!bytes || size == 0 || !out_nif) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_nif are required");
  }
  return guarded("unknown exception while reading a NIF", [&] {
    auto handle = std::make_unique<radicle_nif>();
    handle->file =
        fo4::nif::read_nif(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    handle->shapes = fo4::nif::read_shapes(handle->file);
    handle->collisions = fo4::nif::read_collision_blobs(handle->file);
    *out_nif = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_nif_destroy(radicle_nif* nif) {
  clear_error();
  delete nif;
}

radicle_status radicle_nif_node_count(const radicle_nif* nif, size_t* out_count) {
  clear_error();
  if (!nif || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_count are required");
  *out_count = nif->file.nodes().size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_node_block(const radicle_nif* nif, size_t index, uint32_t* out_block) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  if (!out_block) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_block is required");
  *out_block = node->block;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_node_type(const radicle_nif* nif, size_t index, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  return copy_bytes(node->type, out_bytes, out_size);
}

radicle_status radicle_nif_node_name(const radicle_nif* nif, size_t index, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  return copy_bytes(node->name, out_bytes, out_size);
}

radicle_status radicle_nif_node_parent(const radicle_nif* nif, size_t index, int32_t* out_parent) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  if (!out_parent) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_parent is required");
  *out_parent = node->parent;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_node_children(const radicle_nif* nif,
                                         size_t index,
                                         uint32_t** out_values,
                                         size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  return copy_node_refs(node->children, out_values, out_count);
}

radicle_status radicle_nif_node_transform(const radicle_nif* nif,
                                          size_t index,
                                          float* out_translation,
                                          float* out_rotation,
                                          float* out_scale) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* node = find_node(nif, index, status);
  if (!node) return status;
  if (!out_translation || !out_rotation || !out_scale) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_translation, out_rotation and out_scale are required");
  }
  std::memcpy(out_translation, node->local.t, sizeof node->local.t);
  std::memcpy(out_rotation, node->local.r, sizeof node->local.r);
  *out_scale = node->local.s;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_node_for_block(const radicle_nif* nif, uint32_t block, int32_t* out_node) {
  clear_error();
  if (!nif || !out_node) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_node are required");
  *out_node = nif->file.node_for_block(block);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_count(const radicle_nif* nif, size_t* out_count) {
  clear_error();
  if (!nif || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_count are required");
  *out_count = nif->shapes.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_name(const radicle_nif* nif, size_t index, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_bytes(shape->name, out_bytes, out_size);
}

radicle_status radicle_nif_shape_transform(const radicle_nif* nif,
                                           size_t index,
                                           float* out_translation,
                                           float* out_rotation,
                                           float* out_scale) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_translation || !out_rotation || !out_scale) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_translation, out_rotation and out_scale are required");
  }
  return guarded("unknown exception while composing a shape transform", [&] {
    const auto transform = fo4::nif::shape_to_global(nif->file, shape->block);
    std::memcpy(out_translation, transform.t, sizeof transform.t);
    std::memcpy(out_rotation, transform.r, sizeof transform.r);
    *out_scale = transform.s;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_shape_parent_node(const radicle_nif* nif, size_t index, int32_t* out_node) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_node) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_node is required");
  *out_node = shape->parent_node;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_refs(const radicle_nif* nif,
                                      size_t index,
                                      int32_t* out_skin_ref,
                                      int32_t* out_shader_ref,
                                      int32_t* out_alpha_ref) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_skin_ref || !out_shader_ref || !out_alpha_ref) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_skin_ref, out_shader_ref and out_alpha_ref are required");
  }
  *out_skin_ref = shape->skin_ref;
  *out_shader_ref = shape->shader_ref;
  *out_alpha_ref = shape->alpha_ref;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_warning(const radicle_nif* nif,
                                         size_t index,
                                         uint8_t** out_bytes,
                                         size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_bytes(shape->warning, out_bytes, out_size);
}

radicle_status radicle_nif_shape_vertex_count(const radicle_nif* nif, size_t index, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = shape->verts.size() / 3;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_positions(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->verts, out_values, out_count);
}

radicle_status radicle_nif_shape_normals(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->normals, out_values, out_count);
}

radicle_status radicle_nif_shape_tangents(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->tangents, out_values, out_count);
}

radicle_status radicle_nif_shape_bitangents(const radicle_nif* nif,
                                            size_t index,
                                            float** out_values,
                                            size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->bitangents, out_values, out_count);
}

radicle_status radicle_nif_shape_uvs(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->uvs, out_values, out_count);
}

radicle_status radicle_nif_shape_colors(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->colors, out_values, out_count);
}

radicle_status radicle_nif_shape_triangles(const radicle_nif* nif,
                                           size_t index,
                                           uint16_t** out_values,
                                           size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_indices(shape->tris, out_values, out_count);
}

radicle_status radicle_nif_shape_material(const radicle_nif* nif,
                                          size_t index,
                                          uint8_t** out_bytes,
                                          size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_bytes(shape->material, out_bytes, out_size);
}

radicle_status radicle_nif_shape_texture_count(const radicle_nif* nif, size_t index, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = shape->textures.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_texture(const radicle_nif* nif,
                                         size_t index,
                                         size_t slot,
                                         uint8_t** out_bytes,
                                         size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (slot >= shape->textures.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "texture slot is out of range");
  }
  return copy_bytes(shape->textures[slot], out_bytes, out_size);
}

radicle_status radicle_nif_shape_skinned(const radicle_nif* nif, size_t index, int32_t* out_skinned) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_skinned) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_skinned is required");
  *out_skinned = shape->weights.empty() ? 0 : 1;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_weights(const radicle_nif* nif, size_t index, float** out_values, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_floats(shape->weights, out_values, out_count);
}

radicle_status radicle_nif_shape_bone_indices(const radicle_nif* nif,
                                              size_t index,
                                              uint8_t** out_values,
                                              size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  return copy_byte_values(shape->bone_indices, out_values, out_count);
}

radicle_status radicle_nif_shape_bone_count(const radicle_nif* nif, size_t index, size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  *out_count = shape->bones.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_shape_bone_name(const radicle_nif* nif,
                                           size_t index,
                                           size_t bone,
                                           uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (bone >= shape->bones.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bone index is out of range");
  return copy_bytes(shape->bones[bone].name, out_bytes, out_size);
}

radicle_status radicle_nif_shape_bone_transform(const radicle_nif* nif,
                                                size_t index,
                                                size_t bone,
                                                float* out_center,
                                                float* out_radius,
                                                float* out_rotation,
                                                float* out_translation,
                                                float* out_scale) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(nif, index, status);
  if (!shape) return status;
  if (!out_center || !out_radius || !out_rotation || !out_translation || !out_scale) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "every bone transform output is required");
  }
  if (bone >= shape->bones.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bone index is out of range");
  const auto& entry = shape->bones[bone];
  std::memcpy(out_center, entry.center, sizeof entry.center);
  *out_radius = entry.radius;
  std::memcpy(out_rotation, entry.rotation, sizeof entry.rotation);
  std::memcpy(out_translation, entry.translation, sizeof entry.translation);
  *out_scale = entry.scale;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_count(const radicle_nif* nif, size_t* out_count) {
  clear_error();
  if (!nif || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_count are required");
  *out_count = nif->collisions.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_block(const radicle_nif* nif, size_t index, uint32_t* out_block) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_block) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_block is required");
  *out_block = blob->block;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_type(const radicle_nif* nif,
                                          size_t index,
                                          uint8_t** out_bytes,
                                          size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  return copy_bytes(blob->type, out_bytes, out_size);
}

radicle_status radicle_nif_collision_bytes(const radicle_nif* nif,
                                           size_t index,
                                           uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  return copy_bytes(blob->bytes, out_bytes, out_size);
}

radicle_status radicle_nif_collision_owner_block(const radicle_nif* nif,
                                                 size_t index,
                                                 int32_t* out_block) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_block) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_block is required");
  *out_block = blob->owner;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_flags(const radicle_nif* nif, size_t index, uint16_t* out_flags) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_flags) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_flags is required");
  *out_flags = blob->flags;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_system_ref(const radicle_nif* nif, size_t index, int32_t* out_block) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_block) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_block is required");
  *out_block = blob->system_ref;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_body_id(const radicle_nif* nif,
                                             size_t index,
                                             uint32_t* out_body_id) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_body_id) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_body_id is required");
  *out_body_id = blob->body_id;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_target_block(const radicle_nif* nif,
                                                  size_t index,
                                                  int32_t* out_block) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_block) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_block is required");
  *out_block = blob->target;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_nif_collision_target_node(const radicle_nif* nif, size_t index, int32_t* out_node) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* blob = find_collision(nif, index, status);
  if (!blob) return status;
  if (!out_node) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_node is required");
  *out_node = blob->target_node;
  return RADICLE_STATUS_OK;
}

void radicle_free_floats(float* values) {
  clear_error();
  std::free(values);
}

void radicle_free_triangles(uint16_t* values) {
  clear_error();
  std::free(values);
}

void radicle_free_node_refs(uint32_t* values) {
  clear_error();
  std::free(values);
}

}
