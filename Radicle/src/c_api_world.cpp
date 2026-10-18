#include "c_api_env_internal.hpp"
#include "fo4/c_api_records.h"
#include "fo4/environment/world.hpp"
#include "environment/default_water.hpp"

#include <cstdlib>
#include <optional>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

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

radicle_status copy_form_keys(const radicle_env& env,
                              const std::vector<fo4::FormKey>& keys,
                              radicle_form_key** out_keys,
                              size_t* out_count) {
  *out_keys = nullptr;
  *out_count = 0;
  if (keys.empty()) return RADICLE_STATUS_OK;
  auto* array = static_cast<radicle_form_key*>(std::malloc(keys.size() * sizeof(radicle_form_key)));
  if (array == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate form key array");
  for (size_t index = 0; index < keys.size(); ++index) {
    array[index] = to_c_key(env, std::optional<fo4::FormKey>{keys[index]});
  }
  *out_keys = array;
  *out_count = keys.size();
  return RADICLE_STATUS_OK;
}

radicle_status cell_link_list(const radicle_env* env,
                              const radicle_record* record,
                              std::vector<fo4::FormKey> fo4::env::CellData::*list,
                              radicle_form_key** out_keys,
                              size_t* out_count,
                              const char* what) {
  clear_error();
  if (env == nullptr || record == nullptr || out_keys == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, out_keys and out_count are required");
  }
  *out_keys = nullptr;
  *out_count = 0;
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded(what, [&] {
    const auto data = fo4::env::decode_cell(env->value, *to_ref(record));
    return copy_form_keys(*env, data.*list, out_keys, out_count);
  });
}

}  // namespace

extern "C" {

radicle_status radicle_water_resolve(const radicle_env* env, const radicle_record* record,
                                     radicle_water_selection* out_selection) {
  clear_error();
  if (env == nullptr || record == nullptr || out_selection == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_selection are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_selection = {};
  return guarded("unknown exception while resolving water", [&] {
    const auto* ref = to_ref(record);
    const auto water = fo4::env::resolve_water(env->value, *ref);
    out_selection->water = water.uses_default_water ? radicle_form_key{fo4::env::k_default_water_plugin, fo4::env::k_default_water_local}
                                                   : to_c_key(*env, water.water);
    out_selection->record = to_handle(water.record);
    out_selection->uses_default_water = water.uses_default_water ? 1 : 0;
    set_optional(water.height, out_selection->has_height, out_selection->height);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_worldspace_get_info(const radicle_env* env,
                                           const radicle_record* record,
                                           radicle_worldspace_info* out_info) {
  clear_error();
  if (env == nullptr || record == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_info are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
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
    info.lod_water = to_c_key(*env, data.lod_water);
    set_optional(data.lod_water_height, info.has_lod_water_height, info.lod_water_height);
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
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
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
      info.has_lighting = 1;
      info.lighting = to_c_lighting(*data.lighting);
      info.lighting_tail = to_c_lighting_tail(*data.lighting);
    }
    info.lighting_template = to_c_key(*env, data.lighting_template);
    set_optional(data.water_height, info.has_water_height, info.water_height);
    info.image_space = to_c_key(*env, data.image_space);
    info.encounter_zone = to_c_key(*env, data.encounter_zone);
    info.location = to_c_key(*env, data.location);
    info.worldspace = to_c_key(*env, data.worldspace);
    info.water = to_c_key(*env, data.water);
    set_optional(data.previs_timestamp, info.has_previs_timestamp, info.previs_timestamp);
    set_optional(data.precombined_timestamp, info.has_precombined_timestamp,
                 info.precombined_timestamp);
    info.acoustic_space = to_c_key(*env, data.acoustic_space);
    info.music_type = to_c_key(*env, data.music_type);
    info.sky_weather_region = to_c_key(*env, data.sky_weather_region);
    info.god_rays = to_c_key(*env, data.god_rays);
    info.lock_list = to_c_key(*env, data.lock_list);
    info.owner = to_c_key(*env, data.owner);
    set_optional(data.owner_rank, info.has_owner_rank, info.owner_rank);
    if (data.exterior_lod) {
      info.has_exterior_lod = 1;
      info.exterior_lod_worldspace =
          to_c_key(*env, std::optional<fo4::FormKey>{data.exterior_lod->worldspace});
      for (size_t axis = 0; axis < 3; ++axis) {
        info.exterior_lod_offset[axis] = data.exterior_lod->offset[axis];
      }
    }
    set_optional(data.water_current_count, info.has_water_current_count, info.water_current_count);
    info.water_environment_map = stored_text(*env, data.water_environment_map);
    *out_info = info;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_cell_get_regions(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_form_key** out_keys,
                                        size_t* out_count) {
  return cell_link_list(env, record, &fo4::env::CellData::regions, out_keys, out_count,
                        "unknown exception while decoding CELL regions");
}

radicle_status radicle_cell_get_previs_references(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_form_key** out_keys,
                                                  size_t* out_count) {
  return cell_link_list(env, record, &fo4::env::CellData::previs_references, out_keys, out_count,
                        "unknown exception while decoding CELL previs references");
}

void radicle_free_form_keys(radicle_form_key* keys) {
  clear_error();
  std::free(static_cast<void*>(keys));
}

radicle_status radicle_cell_get_water_currents(const radicle_env* env,
                                               const radicle_record* record,
                                               radicle_water_current** out_currents,
                                               size_t* out_count) {
  clear_error();
  if (env == nullptr || record == nullptr || out_currents == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, record, out_currents and out_count are required");
  }
  *out_currents = nullptr;
  *out_count = 0;
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while decoding CELL water currents", [&] {
    const auto data = fo4::env::decode_cell(env->value, *to_ref(record));
    if (data.water_currents.empty()) return RADICLE_STATUS_OK;
    auto* array = static_cast<radicle_water_current*>(
        std::malloc(data.water_currents.size() * sizeof(radicle_water_current)));
    if (array == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate water current array");
    }
    for (size_t index = 0; index < data.water_currents.size(); ++index) {
      const auto& current = data.water_currents[index];
      array[index].velocity[0] = current.velocity[0];
      array[index].velocity[1] = current.velocity[1];
      array[index].velocity[2] = current.velocity[2];
      array[index].unnamed = current.unnamed;
    }
    *out_currents = array;
    *out_count = data.water_currents.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_free_water_currents(radicle_water_current* currents) {
  clear_error();
  std::free(static_cast<void*>(currents));
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
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
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
