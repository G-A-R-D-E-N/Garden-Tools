#include "c_api_env_internal.hpp"
#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"

#include <memory>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct GrassHolder { radicle_grass_links view{}; std::vector<radicle_form_key> keys; };

radicle_harvest_info harvest(const radicle_env& env, const fo4::env::HarvestData& h) {
  radicle_harvest_info v{};
  v.ingredient = to_c_key(env, h.ingredient); v.sound = to_c_key(env, h.sound);
  if (h.seasons) { v.has_seasons = 1; std::copy(h.seasons->begin(), h.seasons->end(), v.seasons); }
  return v;
}

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required"); }

radicle_status foreign() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment"); }

}

extern "C" {

radicle_status radicle_grass_get_info(const radicle_env* env, const radicle_record* record, radicle_grass_info* out_info) {
  clear_error();
  if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
  return guarded("unknown exception while decoding GRAS", [&] {
    const auto data = fo4::env::decode_grass(env->value, *to_ref(record)); radicle_grass_info v{};
    if (data) {
      v.has_data = 1; v.density = data->density; v.min_slope = data->min_slope; v.max_slope = data->max_slope;
      v.units_from_water = data->units_from_water; v.water_type = data->water_type;
      v.position_range = data->position_range; v.height_range = data->height_range;
      v.color_range = data->color_range; v.wave_period = data->wave_period; v.flags = data->flags;
    }
    *out_info = v; return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_tree_get_info(const radicle_env* env, const radicle_record* record, radicle_tree_info* out_info) {
  clear_error();
  if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
  return guarded("unknown exception while decoding TREE", [&] {
    const auto data = fo4::env::decode_tree(env->value, *to_ref(record)); radicle_tree_info v{};
    v.harvest = harvest(*env, data.harvest);
    if (data.wind) { v.has_wind = 1; std::copy(data.wind->begin(), data.wind->end(), v.wind); }
    *out_info = v; return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_flora_get_info(const radicle_env* env, const radicle_record* record, radicle_flora_info* out_info) {
  clear_error();
  if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
  return guarded("unknown exception while decoding FLOR", [&] {
    const auto data = fo4::env::decode_flora(env->value, *to_ref(record)); radicle_flora_info v{};
    v.harvest = harvest(*env, data.harvest); set_optional(data.flags, v.has_flags, v.flags);
    *out_info = v; return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_landscape_grasses_decode(const radicle_env* env, const radicle_record* record,
                                               const radicle_grass_links** out_links) {
  clear_error();
  if (out_links) *out_links = nullptr;
  if (!env || !record || !out_links) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
  return guarded("unknown exception while decoding LTEX grasses", [&] {
    const auto data = fo4::env::decode_landscape_grasses(env->value, *to_ref(record));
    auto h = std::make_unique<GrassHolder>();
    for (const auto& key : data) h->keys.push_back(to_c_key(*env, key));
    h->view.grasses = h->keys.data(); h->view.grass_count = h->keys.size();
    h->view.internal = h.get(); *out_links = &h.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_landscape_grasses_free(const radicle_grass_links* links) {
  if (links) delete static_cast<GrassHolder*>(links->internal);
}

}
