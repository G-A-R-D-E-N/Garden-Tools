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

struct ClimateData {
  std::vector<ClimateWeather> weathers;
  std::optional<std::string_view> sun_texture;
  std::optional<std::string_view> sun_glare_texture;
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

struct WeatherData {
  std::optional<WeatherColors> colors;
  std::vector<float> fog_distances;
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

LightRecordData decode_light(const Environment& env, const RecordRef& ref);
BendableSplineData decode_bendable_spline(const Environment& env, const RecordRef& ref);
std::optional<FormKey> decode_layer_parent(const Environment& env, const RecordRef& ref);
ClimateData decode_climate(const Environment& env, const RecordRef& ref);
WeatherData decode_weather(const Environment& env, const RecordRef& ref);
std::vector<StaticCollectionPart> decode_static_collection(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
