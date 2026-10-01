#include "c_api_env_internal.hpp"
#include "fo4/environment/base_records.hpp"

#include <memory>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct ClimateHolder {
  radicle_climate view{};
  fo4::env::ClimateData data;
  std::vector<radicle_climate_weather> weathers;
};

struct WeatherHolder {
  radicle_weather_info view{};
  fo4::env::WeatherData data;
};

struct CollectionHolder {
  radicle_static_collection view{};
  std::vector<fo4::env::StaticCollectionPart> data;
  std::vector<std::vector<radicle_static_placement>> placements;
  std::vector<radicle_static_part> parts;
};

const char* c_text(const std::optional<std::string_view>& value) {
  return value ? value->data() : nullptr;
}

radicle_status require(const radicle_env* env, const radicle_record* record, const void* out) {
  if (env == nullptr || record == nullptr || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and an output are required");
  }
  return RADICLE_STATUS_OK;
}

template <class T>
const T* data_or_null(const std::vector<T>& values) {
  return values.empty() ? nullptr : values.data();
}

}  // namespace

extern "C" {

radicle_status radicle_env_plugin_records(const radicle_env* env,
                                          size_t plugin_index,
                                          const radicle_record*** out_records,
                                          size_t* out_count) {
  clear_error();
  if (env == nullptr || out_records == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_records and out_count are required");
  }
  *out_records = nullptr;
  *out_count = 0;
  if (plugin_index >= env->value.plugins().size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin index is out of range");
  }
  return guarded("unknown exception while listing plugin records",
                 [&] { return copy_records(env->value.plugin_records(plugin_index), out_records, out_count); });
}

radicle_status radicle_light_get_info(const radicle_env* env, const radicle_record* record, radicle_light_info* out_info) {
  clear_error();
  if (const auto status = require(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding LIGH", [&] {
    const auto data = fo4::env::decode_light(env->value, *to_ref(record));
    radicle_light_info info{};
    if (data.data) {
      const auto& d = *data.data;
      info.has_data = 1;
      info.time = d.time;
      info.radius = d.radius;
      info.color = to_c_color(d.color);
      info.flags = d.flags;
      info.falloff_exponent = d.falloff_exponent;
      info.fov = d.fov;
      info.near_clip = d.near_clip;
      info.flicker_period = d.flicker_period;
      info.flicker_intensity_amplitude = d.flicker_intensity_amplitude;
      info.flicker_movement_amplitude = d.flicker_movement_amplitude;
      info.constant = d.constant;
      info.scalar = d.scalar;
      set_optional(d.exponent, info.has_exponent, info.exponent);
      set_optional(d.god_rays_near_clip, info.has_god_rays_near_clip, info.god_rays_near_clip);
      set_optional(d.value, info.has_value, info.value);
      set_optional(d.weight, info.has_weight, info.weight);
    }
    set_optional(data.fade, info.has_fade, info.fade);
    info.gobo = c_text(data.gobo);
    info.lens = to_c_key(*env, data.lens);
    info.god_rays = to_c_key(*env, data.god_rays);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_bendable_spline_get_info(const radicle_env* env,
                                                const radicle_record* record,
                                                radicle_bendable_spline_info* out_info) {
  clear_error();
  if (const auto status = require(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding BNDS", [&] {
    const auto data = fo4::env::decode_bendable_spline(env->value, *to_ref(record));
    radicle_bendable_spline_info info{};
    set_optional(data.default_tiles, info.has_default_tiles, info.default_tiles);
    set_optional(data.default_slices, info.has_default_slices, info.default_slices);
    if (data.tiles_relative_to_length) {
      info.has_tiles_relative_to_length = 1;
      info.tiles_relative_to_length = *data.tiles_relative_to_length ? 1 : 0;
    }
    if (data.default_color) {
      info.has_default_color = 1;
      for (std::size_t i = 0; i < 3; ++i) info.default_color[i] = (*data.default_color)[i];
    }
    set_optional(data.wind_sensibility, info.has_wind_sensibility, info.wind_sensibility);
    set_optional(data.wind_flexibility, info.has_wind_flexibility, info.wind_flexibility);
    info.texture = to_c_key(*env, data.texture);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_layer_get_parent(const radicle_env* env, const radicle_record* record, radicle_form_key* out_parent) {
  clear_error();
  if (const auto status = require(env, record, out_parent); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding LAYR", [&] {
    *out_parent = to_c_key(*env, fo4::env::decode_layer_parent(env->value, *to_ref(record)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_lighting_template_get_info(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_lighting_template_info* out_info) {
  clear_error();
  if (const auto status = require(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding LGTM", [&] {
    const auto data = fo4::env::decode_lighting_template(env->value, *to_ref(record));
    radicle_lighting_template_info info{};
    if (data.lighting) {
      info.has_lighting = 1;
      info.lighting = to_c_lighting(*data.lighting);
    }
    info.god_rays = to_c_key(*env, data.god_rays);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_climate_decode(const radicle_env* env, const radicle_record* record, radicle_climate** out_climate) {
  clear_error();
  if (const auto status = require(env, record, out_climate); status != RADICLE_STATUS_OK) return status;
  *out_climate = nullptr;
  return guarded("unknown exception while decoding CLMT", [&] {
    auto holder = std::make_unique<ClimateHolder>();
    holder->data = fo4::env::decode_climate(env->value, *to_ref(record));
    for (const auto& weather : holder->data.weathers) {
      holder->weathers.push_back({to_c_key(*env, weather.weather), weather.chance, to_c_key(*env, weather.global)});
    }
    auto& v = holder->view;
    v.weathers = data_or_null(holder->weathers);
    v.weather_count = holder->weathers.size();
    v.sun_texture = c_text(holder->data.sun_texture);
    v.sun_glare_texture = c_text(holder->data.sun_glare_texture);
    v.internal = holder.get();
    *out_climate = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_climate_free(radicle_climate* climate) {
  clear_error();
  if (climate != nullptr) delete static_cast<ClimateHolder*>(climate->internal);
}

radicle_status radicle_weather_decode(const radicle_env* env,
                                      const radicle_record* record,
                                      radicle_weather_info** out_weather) {
  clear_error();
  if (const auto status = require(env, record, out_weather); status != RADICLE_STATUS_OK) return status;
  *out_weather = nullptr;
  return guarded("unknown exception while decoding WTHR", [&] {
    auto holder = std::make_unique<WeatherHolder>();
    holder->data = fo4::env::decode_weather(env->value, *to_ref(record));
    auto& v = holder->view;
    if (const auto& colors = holder->data.colors) {
      v.colors = reinterpret_cast<const uint8_t*>(colors->raw.data());
      v.color_groups = colors->groups;
      v.color_times = colors->times;
    }
    v.fog_distances = data_or_null(holder->data.fog_distances);
    v.fog_distance_count = holder->data.fog_distances.size();
    v.internal = holder.get();
    *out_weather = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_weather_free(radicle_weather_info* weather) {
  clear_error();
  if (weather != nullptr) delete static_cast<WeatherHolder*>(weather->internal);
}

radicle_status radicle_static_collection_decode(const radicle_env* env,
                                                const radicle_record* record,
                                                radicle_static_collection** out_collection) {
  clear_error();
  if (const auto status = require(env, record, out_collection); status != RADICLE_STATUS_OK) return status;
  *out_collection = nullptr;
  return guarded("unknown exception while decoding SCOL", [&] {
    auto holder = std::make_unique<CollectionHolder>();
    holder->data = fo4::env::decode_static_collection(env->value, *to_ref(record));
    holder->placements.reserve(holder->data.size());
    for (const auto& part : holder->data) {
      auto& placements = holder->placements.emplace_back();
      for (const auto& p : part.placements) {
        placements.push_back({{p.position[0], p.position[1], p.position[2]},
                              {p.rotation[0], p.rotation[1], p.rotation[2]},
                              p.scale});
      }
      holder->parts.push_back({to_c_key(*env, part.static_record), data_or_null(placements), placements.size()});
    }
    auto& v = holder->view;
    v.parts = data_or_null(holder->parts);
    v.part_count = holder->parts.size();
    v.internal = holder.get();
    *out_collection = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_static_collection_free(radicle_static_collection* collection) {
  clear_error();
  if (collection != nullptr) delete static_cast<CollectionHolder*>(collection->internal);
}

}  // extern "C"
