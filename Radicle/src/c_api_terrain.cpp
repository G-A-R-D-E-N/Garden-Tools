#include "c_api_env_internal.hpp"
#include "fo4/c_api_terrain.h"
#include "fo4/environment/terrain.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct LandscapeHolder {
  radicle_landscape view{};
  fo4::env::LandscapeData data;
  std::vector<radicle_land_layer> layers;
};

struct LandBlendHolder {
  radicle_land_blend view{};
  fo4::env::LandVertexWeights blend;
  std::vector<radicle_land_blend_layer> layers;
};

const uint8_t* bytes_or_null(std::span<const std::byte> bytes) {
  return bytes.empty() ? nullptr : reinterpret_cast<const uint8_t*>(bytes.data());
}

}  // namespace

extern "C" {

radicle_status radicle_landscape_decode(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_landscape** out_landscape) {
  clear_error();
  if (env == nullptr || record == nullptr || out_landscape == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_landscape are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_landscape = nullptr;
  return guarded("unknown exception while decoding LAND", [&] {
    auto holder = std::make_unique<LandscapeHolder>();
    holder->data = fo4::env::decode_landscape(env->value, *to_ref(record));
    const auto& d = holder->data;
    auto& v = holder->view;
    if (d.flags) {
      v.has_flags = 1;
      v.flags = *d.flags;
    }
    if (d.height_offset) {
      v.has_heights = 1;
      v.height_offset = *d.height_offset;
    }
    v.heights = bytes_or_null(d.heights);
    v.normals = bytes_or_null(d.normals);
    v.colors = bytes_or_null(d.colors);
    for (const auto& layer : d.layers) {
      holder->layers.push_back({layer.base ? 1 : 0, to_c_key(*env, layer.texture), layer.quadrant,
                                layer.layer, bytes_or_null(layer.alpha), layer.alpha.size()});
    }
    v.layers = holder->layers.empty() ? nullptr : holder->layers.data();
    v.layer_count = holder->layers.size();
    v.internal = holder.get();
    *out_landscape = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_landscape_free(radicle_landscape* landscape) {
  clear_error();
  if (landscape == nullptr) return;
  delete static_cast<LandscapeHolder*>(landscape->internal);
}

radicle_status radicle_landscape_texture_get_set(const radicle_env* env,
                                                 const radicle_record* record,
                                                 radicle_form_key* out_texture_set) {
  clear_error();
  if (env == nullptr || record == nullptr || out_texture_set == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_texture_set are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while decoding LTEX", [&] {
    const auto set = fo4::env::decode_landscape_texture_set(env->value, *to_ref(record));
    *out_texture_set = to_c_key(*env, set);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_texture_set_get_info(const radicle_env* env,
                                            const radicle_record* record,
                                            radicle_texture_set_info* out_info) {
  clear_error();
  if (env == nullptr || record == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_info are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while decoding TXST", [&] {
    const auto data = fo4::env::decode_texture_set(env->value, *to_ref(record));
    radicle_texture_set_info info{};
    for (std::size_t slot = 0; slot < 8; ++slot) {
      info.textures[slot] = stored_text(*env, data.textures[slot]);
    }
    info.material = stored_text(*env, data.material);
    if (data.flags) {
      info.has_flags = 1;
      info.flags = *data.flags;
    }
    if (data.decal) {
      info.has_decal = 1;
      const std::optional<float>* members[] = {&data.decal->min_width, &data.decal->max_width,
                                               &data.decal->min_height, &data.decal->max_height,
                                               &data.decal->depth, &data.decal->shininess};
      for (const auto* member : members) {
        if (!member->has_value()) break;
        info.decal[info.decal_count++] = **member;
      }
    }
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_landscape_texture_diffuse(const radicle_env* env,
                                                     radicle_form_key landscape_texture,
                                                     const char** out_path) {
  clear_error();
  if (env == nullptr || out_path == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_path are required");
  }
  *out_path = nullptr;
  return guarded("unknown exception while resolving landscape texture", [&] {
    const auto key = to_form_key(landscape_texture);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    const auto path = fo4::env::landscape_texture_diffuse(env->value, *key);
    if (!path) return fail(RADICLE_STATUS_NOT_FOUND, "landscape texture has no diffuse path");
    *out_path = stored_text(*env, *path);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_landscape_heights(const radicle_env* env,
                                         const radicle_record* record,
                                         radicle_land_heights* out_heights) {
  clear_error();
  if (env == nullptr || record == nullptr || out_heights == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_heights are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_heights = radicle_land_heights{};
  return guarded("unknown exception while decoding LAND heights", [&] {
    const auto land = fo4::env::decode_landscape(env->value, *to_ref(record));
    radicle_land_heights out{};
    if (const auto grid = fo4::env::decode_land_heights(land)) {
      out.has_heights = 1;
      out.base = grid->base;
      std::copy(grid->heights.begin(), grid->heights.end(), out.heights);
    }
    *out_heights = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_landscape_blend(const radicle_env* env,
                                       const radicle_record* record,
                                       radicle_land_blend** out_blend) {
  clear_error();
  if (env == nullptr || record == nullptr || out_blend == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_blend are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_blend = nullptr;
  return guarded("unknown exception while blending LAND layers", [&] {
    auto holder = std::make_unique<LandBlendHolder>();
    holder->blend = fo4::env::blend_cell_weights(fo4::env::decode_landscape(env->value, *to_ref(record)));
    for (std::size_t quadrant = 0; quadrant < 4; ++quadrant) {
      holder->view.base[quadrant] = to_c_key(*env, holder->blend.base[quadrant]);
    }
    for (const auto& [texture, weights] : holder->blend.layers) {
      holder->layers.push_back({to_c_key(*env, texture), weights.data()});
    }
    holder->view.layers = holder->layers.empty() ? nullptr : holder->layers.data();
    holder->view.layer_count = holder->layers.size();
    holder->view.internal = holder.get();
    *out_blend = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_landscape_blend_free(radicle_land_blend* blend) {
  clear_error();
  if (blend == nullptr) return;
  delete static_cast<LandBlendHolder*>(blend->internal);
}

}  // extern "C"
