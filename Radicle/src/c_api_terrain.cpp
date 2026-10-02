#include "c_api_env_internal.hpp"
#include "fo4/environment/terrain.hpp"

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
    *out_path = path->data();
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
