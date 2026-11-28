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

typedef struct radicle_light_flag_info {
  int32_t can_be_carried;
  int32_t flicker;
  int32_t off_by_default;
  int32_t pulse;
  int32_t shadow_spotlight;
  int32_t shadow_hemisphere;
  int32_t shadow_omnidirectional;
  int32_t nonshadow_spotlight;
  int32_t non_specular;
  int32_t attenuation_only;
  int32_t nonshadow_box;
  int32_t ignore_roughness;
  int32_t no_rim_lighting;
  int32_t ambient_only;
  int32_t is_spot;
  int32_t casts_shadows;
} radicle_light_flag_info;

typedef struct radicle_magic_effect {
  radicle_form_key effect;
  float magnitude;
  uint32_t area;
  uint32_t duration;
  int32_t conditional;
} radicle_magic_effect;

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

typedef struct radicle_weather_ambient {
  uint8_t directional[18];
  int32_t has_specular;
  radicle_byte_color specular;
  int32_t has_fresnel;
  float fresnel;
} radicle_weather_ambient;

typedef struct radicle_weather_sound {
  radicle_form_key sound;
  uint32_t type;
} radicle_weather_sound;

typedef struct radicle_weather_magic_slot {
  radicle_form_key spell;
  int32_t has_threshold;
  float threshold;
} radicle_weather_magic_slot;

typedef struct radicle_weather_info {
  const uint8_t* colors;
  size_t color_groups;
  size_t color_times;
  const float* fog_distances;
  size_t fog_distance_count;
  int32_t has_parameters;
  radicle_weather_parameters parameters;
  const char* const* cloud_textures;
  size_t cloud_texture_count;
  int32_t has_cloud_speed_x;
  uint8_t cloud_speed_x[32];
  int32_t has_cloud_speed_y;
  uint8_t cloud_speed_y[32];
  size_t cloud_color_layers;
  size_t cloud_color_times;
  const uint8_t* cloud_colors;
  const float* cloud_alphas;
  size_t cloud_alpha_count;
  int32_t has_disabled_cloud_layers;
  uint32_t disabled_cloud_layers;
  radicle_form_key precipitation;
  const radicle_weather_ambient* ambient;
  size_t ambient_count;
  void* internal;
  /* Invariant: the members below were appended in ABI 7.616 after internal so consumers built against 7.615 read every older member at the same offset. */
  int32_t has_max_cloud_layers;
  uint32_t max_cloud_layers;
  radicle_form_key visual_effect;
  const float* nam4;
  size_t nam4_count;
  const radicle_weather_sound* sounds;
  size_t sound_count;
  const radicle_form_key* sky_statics;
  size_t sky_static_count;
  const radicle_form_key* image_spaces;
  size_t image_space_count;
  const radicle_form_key* god_rays;
  size_t god_ray_count;
  radicle_form_key sun_glare_lens;
  const char* aurora_model;
  int32_t has_magic;
  radicle_weather_magic_slot lightning_strike;
  radicle_weather_magic_slot weather_activate;
  int32_t has_volatility_mult;
  float volatility_mult;
  int32_t has_visibility_mult;
  float visibility_mult;
} radicle_weather_info;

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
/* Constraint: flags must be the LIGH DATA flags word, and unnamed bits are ignored. */
RADICLE_API radicle_status radicle_light_describe_flags(uint32_t flags, radicle_light_flag_info* out_info);
/* Constraint: record must be an ALCH, ENCH, INGR or SPEL; rows are the effect block in stored order and the caller must free them with radicle_free_magic_effects. */
RADICLE_API radicle_status radicle_record_get_magic_effects(const radicle_env* env,
                                                            const radicle_record* record,
                                                            radicle_magic_effect** out_effects,
                                                            size_t* out_count);
RADICLE_API void radicle_free_magic_effects(radicle_magic_effect* effects);
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
/* Constraint: record must be a WRLD; out_climate is the winning CLMT of the worldspace's CNAM, following the parent chain while the parent-flags word has Use Climate Data (0x10), and NULL with RADICLE_STATUS_OK when none resolves. */
RADICLE_API radicle_status radicle_worldspace_resolve_climate(const radicle_env* env,
                                                              const radicle_record* record,
                                                              const radicle_record** out_climate);

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
/* Requires a record of this environment and a scalar schema path of that record's type, such as "ALCH\Weight"; *out_text is nulled first and, on OK, is the text radicle_plugin_set_record_scalar_at accepts for the index-th occurrence of a numeric or bytes field, or the UTF-8 text radicle_plugin_set_record_string accepts for a string field, released with radicle_record_scalar_free. An unknown path, or an occurrence the record does not carry, returns NOT_FOUND with the detail in radicle_last_error; a field the payload cannot hold or a localized string returns INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_record_get_scalar(
    const radicle_env* env, const radicle_record* record, const char* path, size_t index,
    char** out_text);
RADICLE_API void radicle_record_scalar_free(char* text);
/* Constraint: top-level KWDA keys in stored order, null entries dropped here only; a QUST is INVALID_ARGUMENT because its alias blocks repeat KSIZ/KWDA; release with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_record_keywords(const radicle_env* env, const radicle_record* record,
                                                   radicle_form_key** out_keys, size_t* out_count);

#ifdef __cplusplus
}
#endif
