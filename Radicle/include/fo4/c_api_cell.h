#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exterior blocks require 32x32 cells and sub-blocks 8x8 cells, floored for negative coordinates. */
typedef struct radicle_exterior_cell_layout {
  int32_t cell_x;
  int32_t cell_y;
  int32_t block_x;
  int32_t block_y;
  int32_t subblock_x;
  int32_t subblock_y;
} radicle_exterior_cell_layout;

/* flags must keep bit 0 clear; it defaults to the shipped exterior value 0x0002. */
typedef struct radicle_exterior_cell_creation {
  radicle_form_key worldspace;
  int32_t x;
  int32_t y;
  const char* editor_id;
  uint16_t flags;
} radicle_exterior_cell_creation;

/* Created cells require an existing worldspace or an editor ID; the created key is returned in out_key. */
RADICLE_API radicle_status radicle_exterior_cell_layout_for(int32_t x,
                                                            int32_t y,
                                                            radicle_exterior_cell_layout* out_layout);
RADICLE_API radicle_status radicle_exterior_cell_coordinate_valid(int32_t coordinate,
                                                                  int32_t* out_valid);
RADICLE_API radicle_status radicle_plugin_create_exterior_cell(radicle_plugin* plugin,
                                                               const radicle_exterior_cell_creation* creation,
                                                               radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_create_interior_cell(radicle_plugin* plugin,
                                                               const char* editor_id,
                                                               radicle_form_key* out_key);
/* A search requires the worldspace plugin to be loaded; an absent cell reports RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_find_exterior_cell(const radicle_env* env,
                                                          radicle_form_key worldspace,
                                                          int32_t x,
                                                          int32_t y,
                                                          const radicle_record** out_record);
RADICLE_API radicle_status radicle_env_find_persistent_exterior_cell(const radicle_env* env,
                                                                     radicle_form_key worldspace,
                                                                     const radicle_record** out_record);

typedef struct radicle_cell_lighting_tail {
  int32_t has_near_height;
  float near_height_mid;
  float near_height_range;
  int32_t has_high_fog_colors;
  radicle_byte_color fog_color_high_near;
  radicle_byte_color fog_color_high_far;
  int32_t has_high_density_scale;
  float high_density_scale;
  int32_t has_fog_scales;
  float fog_near_scale;
  float fog_far_scale;
  float fog_high_near_scale;
  float fog_high_far_scale;
  int32_t has_far_height;
  float far_height_mid;
  float far_height_range;
} radicle_cell_lighting_tail;

typedef struct radicle_cell_info {
  int32_t has_flags;
  uint16_t flags;
  int32_t has_grid;
  int32_t grid_x;
  int32_t grid_y;
  int32_t has_land_flags;
  uint8_t land_flags;
  int32_t has_lighting;
  radicle_cell_lighting lighting;
  radicle_form_key lighting_template;
  int32_t has_water_height;
  float water_height;
  radicle_form_key image_space;
  radicle_form_key encounter_zone;
  radicle_form_key location;
  radicle_form_key worldspace;
  radicle_form_key water;
  radicle_cell_lighting_tail lighting_tail;
  int32_t has_previs_timestamp;
  uint16_t previs_timestamp;
  int32_t has_precombined_timestamp;
  uint16_t precombined_timestamp;
  radicle_form_key acoustic_space;
  radicle_form_key music_type;
  radicle_form_key sky_weather_region;
  radicle_form_key god_rays;
  radicle_form_key lock_list;
  radicle_form_key owner;
  int32_t has_owner_rank;
  int32_t owner_rank;
  int32_t has_exterior_lod;
  radicle_form_key exterior_lod_worldspace;
  float exterior_lod_offset[3];
  int32_t has_water_current_count;
  uint32_t water_current_count;
  const char* water_environment_map;
} radicle_cell_info;

typedef struct radicle_water_current {
  float velocity[3];
  float unnamed;
} radicle_water_current;

typedef struct radicle_cell_child {
  const radicle_record* record;
  int32_t persistent;
} radicle_cell_child;

RADICLE_API radicle_status radicle_cell_get_info(const radicle_env* env,
                                                 const radicle_record* record,
                                                 radicle_cell_info* out_info);
/* XCLR regions; the caller must free the array with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_cell_get_regions(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_form_key** out_keys,
                                                    size_t* out_count);
/* XPRI previs reference index; the caller must free the array with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_cell_get_previs_references(const radicle_env* env,
                                                              const radicle_record* record,
                                                              radicle_form_key** out_keys,
                                                              size_t* out_count);
RADICLE_API void radicle_free_form_keys(radicle_form_key* keys);
/* XWCU water currents; the caller must free the array with radicle_free_water_currents. */
RADICLE_API radicle_status radicle_cell_get_water_currents(const radicle_env* env,
                                                           const radicle_record* record,
                                                           radicle_water_current** out_currents,
                                                           size_t* out_count);
RADICLE_API void radicle_free_water_currents(radicle_water_current* currents);

/* Persistent top CELL from the highest-priority plugin; an absent cell must report RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_worldspace_top_cell(const radicle_env* env,
                                                           radicle_form_key worldspace,
                                                           const radicle_record** out_record);
/* Block/sub-block CELLs, each from the highest plugin listing it; the ordering is ascending plugin then on-disk. */
RADICLE_API radicle_status radicle_env_worldspace_cells(const radicle_env* env,
                                                        radicle_form_key worldspace,
                                                        const radicle_record*** out_records,
                                                        size_t* out_count);
/* Winning CELLs; the winning version must sit in an interior sub-block. */
RADICLE_API radicle_status radicle_env_interior_cells(const radicle_env* env,
                                                      const radicle_record*** out_records,
                                                      size_t* out_count);
/* Children merged low to high priority; the caller must free them with radicle_free_cell_children. */
RADICLE_API radicle_status radicle_env_cell_children(const radicle_env* env,
                                                     radicle_form_key cell,
                                                     radicle_cell_child** out_children,
                                                     size_t* out_count);
RADICLE_API void radicle_free_cell_children(radicle_cell_child* children);
/* LAND child of the highest-priority CELL version holding one; a cell without one must report RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_cell_landscape(const radicle_env* env,
                                                      radicle_form_key cell,
                                                      const radicle_record** out_record);

#ifdef __cplusplus
}
#endif
