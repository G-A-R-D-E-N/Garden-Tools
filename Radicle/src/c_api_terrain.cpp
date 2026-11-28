#include "c_api_env_internal.hpp"
#include "fo4/c_api_terrain.h"
#include "fo4/environment/terrain.hpp"
#include "fo4/environment/terrain_writer.hpp"

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

struct LandLayersHolder {
  radicle_land_layers view{};
  std::vector<fo4::env::LandLayerGrid> decoded;
  std::vector<radicle_land_layer_alpha> layers;
};

const uint8_t* bytes_or_null(std::span<const std::byte> bytes) {
  return bytes.empty() ? nullptr : reinterpret_cast<const uint8_t*>(bytes.data());
}

radicle_status land_status_result(fo4::LandMutationStatus status, radicle_land_mutation_status* out_status) {
  switch (status) {
    case fo4::LandMutationStatus::updated:
      *out_status = RADICLE_LAND_MUTATION_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::unchanged:
      *out_status = RADICLE_LAND_MUTATION_UNCHANGED;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::record_missing:
      *out_status = RADICLE_LAND_MUTATION_RECORD_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::record_mismatch:
      *out_status = RADICLE_LAND_MUTATION_RECORD_MISMATCH;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::subrecord_missing:
      *out_status = RADICLE_LAND_MUTATION_SUBRECORD_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::size_mismatch:
      *out_status = RADICLE_LAND_MUTATION_SIZE_MISMATCH;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::value_invalid:
      *out_status = RADICLE_LAND_MUTATION_VALUE_INVALID;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::height_unrepresentable:
      *out_status = RADICLE_LAND_MUTATION_HEIGHT_UNREPRESENTABLE;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::height_out_of_range:
      *out_status = RADICLE_LAND_MUTATION_HEIGHT_OUT_OF_RANGE;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::texture_missing:
      *out_status = RADICLE_LAND_MUTATION_TEXTURE_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::texture_mismatch:
      *out_status = RADICLE_LAND_MUTATION_TEXTURE_MISMATCH;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::quadrant_invalid:
      *out_status = RADICLE_LAND_MUTATION_QUADRANT_INVALID;
      return RADICLE_STATUS_OK;
    case fo4::LandMutationStatus::index_out_of_range:
      *out_status = RADICLE_LAND_MUTATION_INDEX_OUT_OF_RANGE;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected land mutation status");
}

std::optional<fo4::FormKey> land_texture_key(radicle_form_key texture) {
  if (texture.plugin == nullptr) return fo4::FormKey{};
  return to_form_key(texture);
}

}  // namespace

static_assert(RADICLE_LAND_GRID_SIZE == fo4::env::k_land_grid,
              "the C grid size must match the decoded LAND grid");

namespace {

radicle_status grid_cell(int32_t row, int32_t column) {
  if (row < 0 || row >= RADICLE_LAND_GRID_SIZE || column < 0 || column >= RADICLE_LAND_GRID_SIZE) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "row and column must be in 0..RADICLE_LAND_GRID_SIZE-1");
  }
  return RADICLE_STATUS_OK;
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

radicle_status radicle_land_height_at(const radicle_land_heights* heights,
                                      int32_t row,
                                      int32_t column,
                                      float* out_height) {
  clear_error();
  if (heights == nullptr || out_height == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "heights and out_height are required");
  }
  if (const auto status = grid_cell(row, column); status != RADICLE_STATUS_OK) return status;
  if (!heights->has_heights) {
    return fail(RADICLE_STATUS_NOT_FOUND, "land record has no height grid");
  }
  *out_height = heights->heights[row * RADICLE_LAND_GRID_SIZE + column];
  return RADICLE_STATUS_OK;
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

radicle_status radicle_land_blend_weight_at(const radicle_land_blend_layer* layer,
                                            int32_t row,
                                            int32_t column,
                                            float* out_weight) {
  clear_error();
  if (layer == nullptr || out_weight == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "layer and out_weight are required");
  }
  if (const auto status = grid_cell(row, column); status != RADICLE_STATUS_OK) return status;
  if (layer->weights == nullptr) {
    return fail(RADICLE_STATUS_NOT_FOUND, "blend layer has no weights");
  }
  *out_weight = layer->weights[row * RADICLE_LAND_GRID_SIZE + column];
  return RADICLE_STATUS_OK;
}

void radicle_landscape_blend_free(radicle_land_blend* blend) {
  clear_error();
  if (blend == nullptr) return;
  delete static_cast<LandBlendHolder*>(blend->internal);
}

radicle_status radicle_plugin_set_land_heights(radicle_plugin* plugin, radicle_form_key key,
                                               const float* heights, size_t count,
                                               radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  if (plugin == nullptr || out_status == nullptr || !record || (heights == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, out_status and the height array are required");
  }
  return guarded("unknown exception while writing LAND heights", [&] {
    return land_status_result(fo4::set_land_heights(plugin->value, *record, std::span<const float>(heights, count)),
                              out_status);
  });
}

radicle_status radicle_plugin_set_land_normals(radicle_plugin* plugin, radicle_form_key key,
                                               const uint8_t* normals, size_t count,
                                               radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  if (plugin == nullptr || out_status == nullptr || !record || (normals == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, out_status and the normal array are required");
  }
  return guarded("unknown exception while writing LAND normals", [&] {
    const auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(normals), count);
    return land_status_result(fo4::set_land_normals(plugin->value, *record, bytes), out_status);
  });
}

radicle_status radicle_plugin_set_land_vertex_colors(radicle_plugin* plugin, radicle_form_key key,
                                                     const uint8_t* colors, size_t count,
                                                     radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  if (plugin == nullptr || out_status == nullptr || !record || (colors == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, out_status and the color array are required");
  }
  return guarded("unknown exception while writing LAND vertex colors", [&] {
    const auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(colors), count);
    return land_status_result(fo4::set_land_vertex_colors(plugin->value, *record, bytes), out_status);
  });
}

radicle_status radicle_plugin_set_land_base_texture(radicle_plugin* plugin, radicle_form_key key,
                                                    uint8_t quadrant, radicle_form_key texture,
                                                    uint16_t layer,
                                                    radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  const auto texture_key = land_texture_key(texture);
  if (plugin == nullptr || out_status == nullptr || !record || !texture_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, valid record and texture keys and out_status are required");
  }
  return guarded("unknown exception while writing a LAND base texture", [&] {
    try {
      return land_status_result(
          fo4::set_land_base_texture(plugin->value, *record, quadrant, *texture_key, layer), out_status);
    } catch (const fo4::FormIdError& error) {
      // Invariant: a texture whose plugin is not a master of this plugin is an argument error.
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_add_land_alpha_layer(radicle_plugin* plugin, radicle_form_key key,
                                                   uint8_t quadrant, radicle_form_key texture,
                                                   uint16_t layer, const float* alpha, size_t count,
                                                   size_t* out_index,
                                                   radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  const auto texture_key = land_texture_key(texture);
  if (plugin == nullptr || out_status == nullptr || !record || !texture_key || (alpha == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, valid record and texture keys, the alpha array and out_status are required");
  }
  return guarded("unknown exception while adding a LAND alpha layer", [&] {
    std::size_t index = 0;
    try {
      const auto status = fo4::add_land_alpha_layer(plugin->value, *record, quadrant, *texture_key, layer,
                                                    std::span<const float>(alpha, count), &index);
      if (const auto result = land_status_result(status, out_status); result != RADICLE_STATUS_OK) return result;
      if (status == fo4::LandMutationStatus::updated && out_index != nullptr) *out_index = index;
      return RADICLE_STATUS_OK;
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_land_alpha_layer(radicle_plugin* plugin, radicle_form_key key,
                                                      size_t index,
                                                      radicle_land_mutation_status* out_status) {
  clear_error();
  const auto record = to_form_key(key);
  if (plugin == nullptr || out_status == nullptr || !record) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and out_status are required");
  }
  return guarded("unknown exception while removing a LAND alpha layer", [&] {
    return land_status_result(fo4::remove_land_alpha_layer(plugin->value, *record, index), out_status);
  });
}

radicle_status radicle_landscape_layers(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_land_layers** out_layers) {
  clear_error();
  if (env == nullptr || record == nullptr || out_layers == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_layers are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_layers = nullptr;
  return guarded("unknown exception while decoding LAND layers", [&] {
    auto holder = std::make_unique<LandLayersHolder>();
    holder->decoded = fo4::env::decode_land_layers(fo4::env::decode_landscape(env->value, *to_ref(record)));
    for (const auto& layer : holder->decoded) {
      holder->layers.push_back({layer.base ? 1 : 0, to_c_key(*env, layer.texture), layer.quadrant, layer.layer,
                                layer.alpha ? layer.alpha->data() : nullptr});
    }
    holder->view.layers = holder->layers.empty() ? nullptr : holder->layers.data();
    holder->view.layer_count = holder->layers.size();
    holder->view.internal = holder.get();
    *out_layers = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_landscape_layers_free(radicle_land_layers* layers) {
  clear_error();
  if (layers == nullptr) return;
  delete static_cast<LandLayersHolder*>(layers->internal);
}

}  // extern "C"
