#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_grass_info {
  int32_t has_data;
  uint8_t density;
  uint8_t min_slope;
  uint8_t max_slope;
  uint16_t units_from_water;
  uint32_t water_type;
  float position_range;
  float height_range;
  float color_range;
  float wave_period;
  uint8_t flags;
} radicle_grass_info;

typedef struct radicle_harvest_info {
  radicle_form_key ingredient;
  radicle_form_key sound;
  int32_t has_seasons;
  uint8_t seasons[4];
} radicle_harvest_info;

typedef struct radicle_tree_info {
  radicle_harvest_info harvest;
  int32_t has_wind;
  float wind[12];
} radicle_tree_info;

typedef struct radicle_flora_info {
  radicle_harvest_info harvest;
  int32_t has_flags;
  uint16_t flags;
} radicle_flora_info;

typedef struct radicle_grass_links {
  const radicle_form_key* grasses;
  size_t grass_count;
  void* internal;
} radicle_grass_links;

RADICLE_API radicle_status radicle_grass_get_info(const radicle_env* env, const radicle_record* record, radicle_grass_info* out_info);
RADICLE_API radicle_status radicle_tree_get_info(const radicle_env* env, const radicle_record* record, radicle_tree_info* out_info);
RADICLE_API radicle_status radicle_flora_get_info(const radicle_env* env, const radicle_record* record, radicle_flora_info* out_info);
RADICLE_API radicle_status radicle_landscape_grasses_decode(const radicle_env* env, const radicle_record* record, const radicle_grass_links** out_links);
RADICLE_API void radicle_landscape_grasses_free(const radicle_grass_links* links);

#ifdef __cplusplus
}
#endif
