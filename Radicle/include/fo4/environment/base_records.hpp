#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::env {

// LIGH DATA; xEdit requires the first 48 bytes, Mutagen breaks after Scalar and Value.
struct LightData {
  std::int32_t time{0};
  std::uint32_t radius{0};
  ByteColor color;
  std::uint32_t flags{0};
  float falloff_exponent{0.0f};
  float fov{0.0f};
  float near_clip{0.0f};
  float flicker_period{0.0f};
  float flicker_intensity_amplitude{0.0f};
  float flicker_movement_amplitude{0.0f};
  float constant{0.0f};
  float scalar{0.0f};
  std::optional<float> exponent;
  std::optional<float> god_rays_near_clip;
  std::optional<std::uint32_t> value;
  std::optional<float> weight;
};

struct LightRecordData {
  std::optional<LightData> data;
  std::optional<float> fade;
  std::optional<std::string_view> gobo;
  std::optional<FormKey> lens;
  std::optional<FormKey> god_rays;
};

struct BendableSplineData {
  std::optional<float> default_tiles;
  std::optional<std::uint16_t> default_slices;
  std::optional<bool> tiles_relative_to_length;
  std::optional<std::array<float, 3>> default_color;
  std::optional<float> wind_sensibility;
  std::optional<float> wind_flexibility;
  std::optional<FormKey> texture;
};

struct ClimateWeather {
  FormKey weather;
  std::int32_t chance{0};
  FormKey global;
};

struct ClimateTiming {
  std::uint8_t sunrise_begin{0};
  std::uint8_t sunrise_end{0};
  std::uint8_t sunset_begin{0};
  std::uint8_t sunset_end{0};
  std::uint8_t volatility{0};
  std::uint8_t moons_phase_length{0};
};

struct ClimateData {
  std::vector<ClimateWeather> weathers;
  std::optional<std::string_view> sun_texture;
  std::optional<std::string_view> sun_glare_texture;
  std::optional<ClimateTiming> timing;
};

enum class WeatherColorGroup : std::uint8_t {
  sky_upper, fog_near, unknown, ambient, sunlight, sun, stars, sky_lower, horizon, effect_lighting,
  cloud_lod_diffuse, cloud_lod_ambient, fog_far, sky_statics, water_multiplier, sun_glare, moon_glare,
  fog_near_high, fog_far_high,
};

inline constexpr std::size_t k_weather_day = 1;

struct WeatherColors {
  std::size_t times{0};
  std::size_t groups{0};
  std::span<const std::byte> raw;

  std::optional<ByteColor> color(WeatherColorGroup group, std::size_t time) const noexcept;
};

struct WeatherParameters {
  std::uint8_t wind_speed{0};
  std::uint8_t transition_delta{0};
  std::uint8_t sun_glare{0};
  std::uint8_t sun_damage{0};
  std::uint8_t precipitation_begin{0};
  std::uint8_t precipitation_end{0};
  std::uint8_t lightning_begin{0};
  std::uint8_t lightning_end{0};
  std::uint8_t lightning_frequency{0};
  std::uint8_t flags{0};
  ByteColor lightning_color;
  std::uint8_t visual_effect_begin{0};
  std::uint8_t visual_effect_end{0};
  std::uint8_t wind_direction{0};
  std::uint8_t wind_direction_range{0};
  std::optional<std::uint8_t> wind_turbulence;
};

struct WeatherData {
  std::optional<WeatherColors> colors;
  std::vector<float> fog_distances;
  std::optional<WeatherParameters> parameters;
};

struct StaticPlacement {
  std::array<float, 3> position{};
  std::array<float, 3> rotation{};
  float scale{0.0f};
};

struct StaticCollectionPart {
  FormKey static_record;
  std::vector<StaticPlacement> placements;
};

// STAT MNAM: up to four 260-byte slots, each a NUL-terminated mesh path followed by unused bytes (xEdit wbDefinitionsFO4.pas).
inline constexpr std::size_t k_distant_lod_levels = 4;

struct StaticDistantLod {
  std::size_t levels{0};
  std::array<std::optional<std::string_view>, k_distant_lod_levels> meshes{};
};

struct WaterNoiseLayer {
  float wind_direction{0.0f};
  float wind_speed{0.0f};
  float amplitude_scale{0.0f};
  float uv_scale{0.0f};
  float noise_falloff{0.0f};
};

// WATR DNAM through Interior Specular Power and the noise layers; the silt and reflection tail is optional.
struct WaterVisual {
  float fog_depth_amount{0.0f};
  ByteColor fog_shallow_color;
  ByteColor fog_deep_color;
  float fog_color_shallow_range{0.0f};
  float fog_color_deep_range{0.0f};
  float fog_shallow_alpha{0.0f};
  float fog_deep_alpha{0.0f};
  float fog_alpha_shallow_range{0.0f};
  float fog_alpha_deep_range{0.0f};
  ByteColor fog_underwater_color;
  float fog_underwater_amount{0.0f};
  float fog_underwater_near{0.0f};
  float fog_underwater_far{0.0f};
  float normal_magnitude{0.0f};
  float shallow_normal_falloff{0.0f};
  float deep_normal_falloff{0.0f};
  float reflectivity_amount{0.0f};
  float fresnel_amount{0.0f};
  float surface_effect_falloff{0.0f};
  float displacement_force{0.0f};
  float displacement_velocity{0.0f};
  float displacement_falloff{0.0f};
  float displacement_dampener{0.0f};
  float displacement_starting_size{0.0f};
  ByteColor reflection_color;
  float sun_specular_power{0.0f};
  float sun_specular_magnitude{0.0f};
  float sun_sparkle_power{0.0f};
  float sun_sparkle_magnitude{0.0f};
  float interior_specular_radius{0.0f};
  float interior_specular_brightness{0.0f};
  float interior_specular_power{0.0f};
  std::array<WaterNoiseLayer, 3> noise{};
  std::optional<float> silt_amount;
  std::optional<ByteColor> silt_light_color;
  std::optional<ByteColor> silt_dark_color;
  std::optional<bool> screen_space_reflections;
};

struct WaterData {
  std::optional<std::uint8_t> flags;
  std::optional<WaterVisual> visual;
  std::array<std::optional<std::string_view>, 3> noise_textures{};
  std::optional<std::array<float, 3>> linear_velocity;
  std::optional<std::array<float, 3>> angular_velocity;
};

LightRecordData decode_light(const Environment& env, const RecordRef& ref);
BendableSplineData decode_bendable_spline(const Environment& env, const RecordRef& ref);
std::optional<FormKey> decode_layer_parent(const Environment& env, const RecordRef& ref);
ClimateData decode_climate(const Environment& env, const RecordRef& ref);
WeatherData decode_weather(const Environment& env, const RecordRef& ref);
std::vector<StaticCollectionPart> decode_static_collection(const Environment& env, const RecordRef& ref);
StaticDistantLod decode_static_distant_lod(const Environment& env, const RecordRef& ref);
std::optional<FormKey> decode_activator_water_type(const Environment& env, const RecordRef& ref);
WaterData decode_water(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
