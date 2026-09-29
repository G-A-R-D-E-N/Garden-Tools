#include "c_api_env_internal.hpp"
#include "fo4/environment/world.hpp"

#include <cstdlib>
#include <optional>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_form_key to_c_key(const radicle_env& env, const std::optional<fo4::FormKey>& key) {
  if (!key || key->owner().empty()) return {nullptr, 0};
  const auto* owner = env.value.stable_owner(*key);
  if (owner == nullptr) throw fo4::env::EnvironmentError("FormKey owner is not in the environment");
  return {owner->filename().data(), key->local().value()};
}

radicle_byte_color to_c_color(const fo4::env::ByteColor& color) {
  return {color.red, color.green, color.blue};
}

template <class T>
void set_optional(const std::optional<T>& value, int32_t& has, T& out) {
  has = value ? 1 : 0;
  if (value) out = *value;
}

radicle_status resolve_one(const radicle_env* env,
                           radicle_form_key key,
                           const radicle_record** out_record,
                           const char* what,
                           const fo4::env::RecordRef* (*query)(const fo4::env::Environment&,
                                                               const fo4::FormKey&)) {
  if (env == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_record are required");
  }
  *out_record = nullptr;
  return guarded(what, [&] {
    const auto form_key = to_form_key(key);
    if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    const auto* ref = query(env->value, *form_key);
    if (ref == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "no matching record");
    *out_record = to_handle(ref);
    return RADICLE_STATUS_OK;
  });
}

}  // namespace

extern "C" {

radicle_status radicle_worldspace_get_info(const radicle_env* env,
                                           const radicle_record* record,
                                           radicle_worldspace_info* out_info) {
  clear_error();
  if (env == nullptr || record == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_info are required");
  }
  return guarded("unknown exception while decoding WRLD", [&] {
    const auto data = fo4::env::decode_worldspace(env->value, *to_ref(record));
    radicle_worldspace_info info{};
    info.parent = to_c_key(*env, data.parent);
    set_optional(data.parent_flags, info.has_parent_flags, info.parent_flags);
    info.climate = to_c_key(*env, data.climate);
    info.water = to_c_key(*env, data.water);
    info.lighting_template = to_c_key(*env, data.lighting_template);
    if (data.default_land_height && data.default_water_height) {
      info.has_land_defaults = 1;
      info.default_land_height = *data.default_land_height;
      info.default_water_height = *data.default_water_height;
    }
    if (data.bounds_min) {
      info.has_bounds_min = 1;
      info.bounds_min[0] = (*data.bounds_min)[0];
      info.bounds_min[1] = (*data.bounds_min)[1];
    }
    if (data.bounds_max) {
      info.has_bounds_max = 1;
      info.bounds_max[0] = (*data.bounds_max)[0];
      info.bounds_max[1] = (*data.bounds_max)[1];
    }
    set_optional(data.flags, info.has_flags, info.flags);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_cell_get_info(const radicle_env* env,
                                     const radicle_record* record,
                                     radicle_cell_info* out_info) {
  clear_error();
  if (env == nullptr || record == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_info are required");
  }
  return guarded("unknown exception while decoding CELL", [&] {
    const auto data = fo4::env::decode_cell(env->value, *to_ref(record));
    radicle_cell_info info{};
    set_optional(data.flags, info.has_flags, info.flags);
    if (data.grid) {
      info.has_grid = 1;
      info.grid_x = data.grid->x;
      info.grid_y = data.grid->y;
      set_optional(data.grid->land_flags, info.has_land_flags, info.land_flags);
    }
    if (data.lighting) {
      const auto& in = *data.lighting;
      auto& out = info.lighting;
      info.has_lighting = 1;
      out.ambient = to_c_color(in.ambient);
      out.directional = to_c_color(in.directional);
      out.fog_near_color = to_c_color(in.fog_near_color);
      out.fog_near = in.fog_near;
      out.fog_far = in.fog_far;
      out.directional_rotation_xy = in.directional_rotation_xy;
      out.directional_rotation_z = in.directional_rotation_z;
      out.directional_fade = in.directional_fade;
      out.fog_clip_distance = in.fog_clip_distance;
      out.fog_power = in.fog_power;
      if (in.fog_far_color) {
        out.has_fog_far_color = 1;
        out.fog_far_color = to_c_color(*in.fog_far_color);
      }
      set_optional(in.fog_max, out.has_fog_max, out.fog_max);
      if (in.light_fade_begin && in.light_fade_end) {
        out.has_light_fade = 1;
        out.light_fade_begin = *in.light_fade_begin;
        out.light_fade_end = *in.light_fade_end;
      }
      set_optional(in.inherits, out.has_inherits, out.inherits);
    }
    info.lighting_template = to_c_key(*env, data.lighting_template);
    set_optional(data.water_height, info.has_water_height, info.water_height);
    info.image_space = to_c_key(*env, data.image_space);
    info.encounter_zone = to_c_key(*env, data.encounter_zone);
    info.location = to_c_key(*env, data.location);
    info.worldspace = to_c_key(*env, data.worldspace);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_get_container(const radicle_env* env,
                                            const radicle_record* record,
                                            int32_t* out_group_type,
                                            radicle_form_key* out_worldspace,
                                            radicle_form_key* out_cell) {
  clear_error();
  if (env == nullptr || record == nullptr || out_group_type == nullptr ||
      out_worldspace == nullptr || out_cell == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "all arguments are required");
  }
  return guarded("unknown exception while reading record container", [&] {
    const auto* ref = to_ref(record);
    const auto world = to_c_key(*env, env->value.world_of(*ref));
    const auto cell = to_c_key(*env, env->value.cell_of(*ref));
    *out_group_type = static_cast<int32_t>(ref->group);
    *out_worldspace = world;
    *out_cell = cell;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_worldspace_top_cell(const radicle_env* env,
                                               radicle_form_key worldspace,
                                               const radicle_record** out_record) {
  clear_error();
  return resolve_one(env, worldspace, out_record, "unknown exception while finding top cell",
                     [](const fo4::env::Environment& value, const fo4::FormKey& key) {
                       return value.worldspace_top_cell(key);
                     });
}

radicle_status radicle_env_cell_landscape(const radicle_env* env,
                                          radicle_form_key cell,
                                          const radicle_record** out_record) {
  clear_error();
  return resolve_one(env, cell, out_record, "unknown exception while finding landscape",
                     [](const fo4::env::Environment& value, const fo4::FormKey& key) {
                       return value.cell_landscape(key);
                     });
}

radicle_status radicle_env_worldspace_cells(const radicle_env* env,
                                            radicle_form_key worldspace,
                                            const radicle_record*** out_records,
                                            size_t* out_count) {
  clear_error();
  if (env == nullptr || out_records == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_records and out_count are required");
  }
  *out_records = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing worldspace cells", [&] {
    const auto key = to_form_key(worldspace);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    return copy_records(env->value.worldspace_cells(*key), out_records, out_count);
  });
}

radicle_status radicle_env_interior_cells(const radicle_env* env,
                                          const radicle_record*** out_records,
                                          size_t* out_count) {
  clear_error();
  if (env == nullptr || out_records == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_records and out_count are required");
  }
  *out_records = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing interior cells", [&] {
    return copy_records(env->value.interior_cells(), out_records, out_count);
  });
}

radicle_status radicle_env_cell_children(const radicle_env* env,
                                         radicle_form_key cell,
                                         radicle_cell_child** out_children,
                                         size_t* out_count) {
  clear_error();
  if (env == nullptr || out_children == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_children and out_count are required");
  }
  *out_children = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing cell children", [&] {
    const auto key = to_form_key(cell);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    const auto children = env->value.cell_children(*key);
    if (children.empty()) return RADICLE_STATUS_OK;
    auto* array = static_cast<radicle_cell_child*>(
        std::malloc(children.size() * sizeof(radicle_cell_child)));
    if (array == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate cell child array");
    }
    for (size_t i = 0; i < children.size(); ++i) {
      array[i] = {to_handle(children[i].ref), children[i].persistent ? 1 : 0};
    }
    *out_children = array;
    *out_count = children.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_free_cell_children(radicle_cell_child* children) {
  clear_error();
  std::free(children);
}

}  // extern "C"
