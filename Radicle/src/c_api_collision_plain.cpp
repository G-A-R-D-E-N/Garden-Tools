#include "fo4/c_api_collision_plain.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/nif/collision_plain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

struct radicle_collision_plain {
  fo4::nif::PlainCollision value;
};

namespace {

using namespace radicle_c_detail;

const fo4::nif::PlainShape* find_shape(const radicle_collision_plain* plain, size_t index, radicle_status& status) {
  if (!plain) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision is required");
    return nullptr;
  }
  if (index >= plain->value.shapes.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "collision shape index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &plain->value.shapes[index];
}

constexpr CopyMessages k_float_messages{"out_values and out_count are required", "failed to allocate collision points"};
constexpr CopyMessages k_index_messages{"out_values and out_count are required",
                                        "failed to allocate collision indices"};

}  // namespace

extern "C" {

radicle_status radicle_nif_collision_plain(const radicle_nif* nif, radicle_collision_plain** out_plain) {
  clear_error();
  if (out_plain) *out_plain = nullptr;
  if (!nif || !out_plain) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_plain are required");
  return guard_exceptions("unknown exception while building plain collision", [&] {
    auto handle = std::make_unique<radicle_collision_plain>();
    handle->value = fo4::nif::build_plain_collision(nif->collisions);
    fo4::nif::attach_collision_owners(handle->value, nif->file, nif->collisions);
    *out_plain = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_collision_plain_destroy(radicle_collision_plain* plain) {
  clear_error();
  delete plain;
}

radicle_status radicle_collision_plain_body_count(const radicle_collision_plain* plain, size_t* out_count) {
  clear_error();
  if (!plain || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision and out_count are required");
  *out_count = plain->value.bodies.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_plain_read_body(const radicle_collision_plain* plain,
                                                 size_t index,
                                                 radicle_collision_plain_body* out_body) {
  clear_error();
  if (!plain || !out_body) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision and out_body are required");
  if (index >= plain->value.bodies.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "collision body index is out of range");
  }
  const auto& body = plain->value.bodies[index];
  *out_body = radicle_collision_plain_body{};
  out_body->collision_index = body.system;
  out_body->body = body.body;
  out_body->layer = body.layer;
  out_body->dynamic = body.dynamic ? 1 : 0;
  out_body->trigger_type = body.trigger_type;
  out_body->has_mass = body.has_mass ? 1 : 0;
  out_body->mass = body.mass;
  out_body->has_center_of_mass = body.has_center_of_mass ? 1 : 0;
  std::copy(body.center_of_mass.begin(), body.center_of_mass.end(), out_body->center_of_mass);
  std::copy(body.origin.begin(), body.origin.end(), out_body->origin);
  std::copy(body.basis.begin(), body.basis.end(), out_body->basis);
  out_body->first_shape = body.first_shape;
  out_body->shape_count = body.shape_count;
  out_body->skipped_shapes = body.skipped_shapes;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_plain_body_owner(const radicle_collision_plain* plain,
                                                  size_t index,
                                                  int32_t* out_owner_block,
                                                  int32_t* out_target_node,
                                                  float* out_translation,
                                                  float* out_rotation,
                                                  float* out_scale) {
  clear_error();
  if (!plain || !out_owner_block || !out_target_node || !out_translation || !out_rotation || !out_scale) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision and every output are required");
  }
  if (index >= plain->value.bodies.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "collision body index is out of range");
  }
  const auto& body = plain->value.bodies[index];
  *out_owner_block = body.has_owner ? body.owner_block : -1;
  *out_target_node = body.has_owner ? body.owner_target_node : -1;
  std::copy(body.owner_transform.translation.begin(), body.owner_transform.translation.end(), out_translation);
  std::copy(body.owner_transform.rotation.begin(), body.owner_transform.rotation.end(), out_rotation);
  *out_scale = body.owner_transform.scale;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_plain_shape_count(const radicle_collision_plain* plain, size_t* out_count) {
  clear_error();
  if (!plain || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision and out_count are required");
  *out_count = plain->value.shapes.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_plain_read_shape(const radicle_collision_plain* plain,
                                                  size_t index,
                                                  radicle_collision_plain_shape* out_shape) {
  clear_error();
  if (!out_shape) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_shape is required");
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(plain, index, status);
  if (!shape) return status;
  *out_shape = radicle_collision_plain_shape{};
  out_shape->collision_index = shape->system;
  out_shape->body = shape->body;
  out_shape->kind = static_cast<uint32_t>(shape->kind);
  out_shape->material = shape->material;
  std::copy(shape->origin.begin(), shape->origin.end(), out_shape->origin);
  std::copy(shape->basis.begin(), shape->basis.end(), out_shape->basis);
  std::copy(shape->half_extents.begin(), shape->half_extents.end(), out_shape->half_extents);
  out_shape->radius = shape->radius;
  std::copy(shape->point_a.begin(), shape->point_a.end(), out_shape->point_a);
  std::copy(shape->point_b.begin(), shape->point_b.end(), out_shape->point_b);
  out_shape->margin = shape->margin;
  out_shape->point_count = shape->points.size() / 3;
  out_shape->triangle_count = shape->indices.size() / 3;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_collision_plain_shape_points(const radicle_collision_plain* plain,
                                                    size_t index,
                                                    float** out_values,
                                                    size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(plain, index, status);
  if (!shape) return status;
  return copy_out(shape->points, out_values, out_count, k_float_messages);
}

radicle_status radicle_collision_plain_shape_indices(const radicle_collision_plain* plain,
                                                     size_t index,
                                                     uint32_t** out_values,
                                                     size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(plain, index, status);
  if (!shape) return status;
  return copy_out(shape->indices, out_values, out_count, k_index_messages);
}

radicle_status radicle_collision_plain_shape_triangle_materials(const radicle_collision_plain* plain,
                                                                size_t index,
                                                                uint32_t** out_values,
                                                                size_t* out_count) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = find_shape(plain, index, status);
  if (!shape) return status;
  return copy_out(shape->triangle_materials, out_values, out_count, k_index_messages);
}

radicle_status radicle_collision_plain_counts(const radicle_collision_plain* plain,
                                              uint32_t* out_skipped_shapes,
                                              uint32_t* out_undecodable_systems) {
  clear_error();
  if (!plain || !out_skipped_shapes || !out_undecodable_systems) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plain collision and both outputs are required");
  }
  *out_skipped_shapes = plain->value.skipped_shapes;
  *out_undecodable_systems = plain->value.undecodable_systems;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
