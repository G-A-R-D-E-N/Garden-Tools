#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_light_info {
  int32_t has_data;
  int32_t time;
  uint32_t radius;
  radicle_byte_color color;
  uint32_t flags;
  float falloff_exponent;
  float fov;
  float near_clip;
  float flicker_period;
  float flicker_intensity_amplitude;
  float flicker_movement_amplitude;
  float constant;
  float scalar;
  int32_t has_exponent;
  float exponent;
  int32_t has_god_rays_near_clip;
  float god_rays_near_clip;
  int32_t has_value;
  uint32_t value;
  int32_t has_weight;
  float weight;
  int32_t has_fade;
  float fade;
  const char* gobo;
  radicle_form_key lens;
  radicle_form_key god_rays;
} radicle_light_info;

typedef struct radicle_add_on_node_info {
  int32_t has_node_index;
  uint32_t node_index;
  radicle_form_key light;
  const char* model;
  int32_t has_dnam;
  uint16_t master_particle_cap;
  uint16_t flags;
} radicle_add_on_node_info;

typedef struct radicle_bendable_spline_info {
  int32_t has_default_tiles;
  float default_tiles;
  int32_t has_default_slices;
  uint16_t default_slices;
  int32_t has_tiles_relative_to_length;
  int32_t tiles_relative_to_length;
  int32_t has_default_color;
  float default_color[3];
  int32_t has_wind_sensibility;
  float wind_sensibility;
  int32_t has_wind_flexibility;
  float wind_flexibility;
  radicle_form_key texture;
} radicle_bendable_spline_info;

typedef struct radicle_climate_weather {
  radicle_form_key weather;
  int32_t chance;
  radicle_form_key global;
} radicle_climate_weather;

typedef struct radicle_climate_timing {
  uint8_t sunrise_begin;
  uint8_t sunrise_end;
  uint8_t sunset_begin;
  uint8_t sunset_end;
  uint8_t volatility;
  uint8_t moons_phase_length;
  float sunrise_begin_hours;
  float sunrise_end_hours;
  float sunset_begin_hours;
  float sunset_end_hours;
  uint8_t days_per_phase;
  int32_t uses_secunda;
  int32_t uses_masser;
} radicle_climate_timing;

typedef struct radicle_climate {
  const radicle_climate_weather* weathers;
  size_t weather_count;
  const char* sun_texture;
  const char* sun_glare_texture;
  int32_t has_timing;
  radicle_climate_timing timing;
  void* internal;
} radicle_climate;

typedef struct radicle_weather_parameters {
  uint8_t wind_speed;
  uint8_t transition_delta;
  uint8_t sun_glare;
  uint8_t sun_damage;
  uint8_t precipitation_begin;
  uint8_t precipitation_end;
  uint8_t lightning_begin;
  uint8_t lightning_end;
  uint8_t lightning_frequency;
  uint8_t flags;
  radicle_byte_color lightning_color;
  uint8_t visual_effect_begin;
  uint8_t visual_effect_end;
  uint8_t wind_direction;
  uint8_t wind_direction_range;
  int32_t has_wind_turbulence;
  uint8_t wind_turbulence;
} radicle_weather_parameters;

typedef struct radicle_weather_info {
  const uint8_t* colors;
  size_t color_groups;
  size_t color_times;
  const float* fog_distances;
  size_t fog_distance_count;
  int32_t has_parameters;
  radicle_weather_parameters parameters;
  void* internal;
} radicle_weather_info;

typedef struct radicle_static_placement {
  float position[3];
  float rotation[3];
  float scale;
} radicle_static_placement;

typedef struct radicle_static_part {
  radicle_form_key static_record;
  const radicle_static_placement* placements;
  size_t placement_count;
} radicle_static_part;

typedef struct radicle_static_collection {
  const radicle_static_part* parts;
  size_t part_count;
  void* internal;
} radicle_static_collection;

/* STAT MNAM; a slot is NULL when the record has fewer slots or the slot holds no mesh. */
typedef struct radicle_static_distant_lod {
  size_t level_count;
  const char* meshes[4];
} radicle_static_distant_lod;

typedef struct radicle_water_info {
  int32_t has_flags;
  uint8_t flags;
  int32_t has_visual;
  float fog_depth_amount;
  radicle_byte_color fog_shallow_color;
  radicle_byte_color fog_deep_color;
  float fog_color_shallow_range;
  float fog_color_deep_range;
  float fog_shallow_alpha;
  float fog_deep_alpha;
  float fog_alpha_shallow_range;
  float fog_alpha_deep_range;
  radicle_byte_color fog_underwater_color;
  float fog_underwater_amount;
  float fog_underwater_near;
  float fog_underwater_far;
  float normal_magnitude;
  float shallow_normal_falloff;
  float deep_normal_falloff;
  float reflectivity_amount;
  float fresnel_amount;
  float surface_effect_falloff;
  float displacement_force;
  float displacement_velocity;
  float displacement_falloff;
  float displacement_dampener;
  float displacement_starting_size;
  radicle_byte_color reflection_color;
  float sun_specular_power;
  float sun_specular_magnitude;
  float sun_sparkle_power;
  float sun_sparkle_magnitude;
  float interior_specular_radius;
  float interior_specular_brightness;
  float interior_specular_power;
  float noise_wind_direction[3];
  float noise_wind_speed[3];
  float noise_amplitude_scale[3];
  float noise_uv_scale[3];
  float noise_falloff[3];
  int32_t has_silt_amount;
  float silt_amount;
  int32_t has_silt_light_color;
  radicle_byte_color silt_light_color;
  int32_t has_silt_dark_color;
  radicle_byte_color silt_dark_color;
  int32_t has_screen_space_reflections;
  int32_t screen_space_reflections;
  const char* noise_textures[3];
  int32_t has_linear_velocity;
  float linear_velocity[3];
  int32_t has_angular_velocity;
  float angular_velocity[3];
} radicle_water_info;

typedef struct radicle_water_selection {
  radicle_form_key water;
  const radicle_record* record;
  int32_t uses_default_water;
  int32_t has_height;
  float height;
} radicle_water_selection;

typedef struct radicle_lighting_template_info {
  int32_t has_lighting;
  radicle_cell_lighting lighting;
  radicle_form_key god_rays;
} radicle_lighting_template_info;

RADICLE_API radicle_status radicle_light_get_info(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_light_info* out_info);
RADICLE_API radicle_status radicle_add_on_node_get_info(const radicle_env* env,
                                                        const radicle_record* record,
                                                        radicle_add_on_node_info* out_info);
RADICLE_API radicle_status radicle_bendable_spline_get_info(const radicle_env* env,
                                                            const radicle_record* record,
                                                            radicle_bendable_spline_info* out_info);
RADICLE_API radicle_status radicle_layer_get_parent(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_form_key* out_parent);
RADICLE_API radicle_status radicle_lighting_template_get_info(const radicle_env* env,
                                                              const radicle_record* record,
                                                              radicle_lighting_template_info* out_info);
RADICLE_API radicle_status radicle_climate_decode(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_climate** out_climate);
RADICLE_API void radicle_climate_free(radicle_climate* climate);
typedef struct radicle_climate_timing_edit {
  uint8_t sunrise_begin;
  uint8_t sunrise_end;
  uint8_t sunset_begin;
  uint8_t sunset_end;
  uint8_t volatility;
  uint8_t moons_phase_length;
} radicle_climate_timing_edit;

RADICLE_API radicle_status radicle_plugin_set_climate_timing(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             const radicle_climate_timing_edit* timing);
RADICLE_API radicle_status radicle_plugin_set_climate_weathers(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               const radicle_climate_weather* weathers,
                                                               size_t weather_count);
RADICLE_API radicle_status radicle_plugin_set_weather_colors(radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             uint8_t group,
                                                             const radicle_byte_color* times,
                                                             size_t time_count);
RADICLE_API radicle_status radicle_plugin_set_weather_fog(radicle_plugin* plugin,
                                                          radicle_form_key key,
                                                          const float* distances,
                                                          size_t distance_count);
RADICLE_API radicle_status radicle_plugin_set_weather_transition(radicle_plugin* plugin,
                                                                 radicle_form_key key,
                                                                 uint8_t transition_delta);
RADICLE_API radicle_status radicle_climate_moon_phase(uint8_t moons_phase_length,
                                                      uint32_t days_passed,
                                                      int32_t* out_has_phase,
                                                      uint8_t* out_phase);
RADICLE_API radicle_status radicle_climate_volatility_hold_hours(uint8_t volatility,
                                                                 float multiplier,
                                                                 float duration_min,
                                                                 float duration_max,
                                                                 float* out_hours);
RADICLE_API radicle_status radicle_weather_transition_hours(uint8_t transition_delta,
                                                            float min_hours,
                                                            float max_hours,
                                                            float* out_hours);
RADICLE_API radicle_status radicle_weather_decode(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_weather_info** out_weather);
RADICLE_API void radicle_weather_free(radicle_weather_info* weather);
RADICLE_API radicle_status radicle_static_collection_decode(const radicle_env* env,
                                                            const radicle_record* record,
                                                            radicle_static_collection** out_collection);
RADICLE_API void radicle_static_collection_free(radicle_static_collection* collection);
RADICLE_API radicle_status radicle_static_get_distant_lod(const radicle_env* env,
                                                          const radicle_record* record,
                                                          radicle_static_distant_lod* out_lod);
RADICLE_API radicle_status radicle_activator_get_water_type(const radicle_env* env,
                                                            const radicle_record* record,
                                                            radicle_form_key* out_water);
RADICLE_API radicle_status radicle_water_get_info(const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_water_info* out_info);
RADICLE_API radicle_status radicle_water_resolve(const radicle_env* env,
                                                 const radicle_record* record,
                                                 radicle_water_selection* out_selection);

RADICLE_API radicle_status radicle_record_payload_info(
    const radicle_env* env, const radicle_record* record,
    uint16_t* out_form_version, size_t* out_subrecord_count);
RADICLE_API radicle_status radicle_record_copy_subrecord(
    const radicle_env* env, const radicle_record* record, size_t index,
    uint8_t* out_signature, uint8_t** out_bytes, size_t* out_size,
    int32_t* out_extended_size);
RADICLE_API radicle_status radicle_record_resolve_form_id(
    const radicle_env* env, const radicle_record* record, uint32_t raw_form_id,
    radicle_form_key* out_key);
RADICLE_API radicle_status radicle_record_topic_children(
    const radicle_env* env, const radicle_record* record,
    const radicle_record*** out_records, size_t* out_count);

#ifdef __cplusplus
}
#endif
