#pragma once

#include "fo4/environment/environment.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fo4::env {

struct ByteColor {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

struct WorldspaceData {
  std::optional<FormKey> parent;
  std::optional<std::uint16_t> parent_flags;
  std::optional<FormKey> climate;
  std::optional<FormKey> water;
  std::optional<FormKey> lighting_template;
  std::optional<float> default_land_height;
  std::optional<float> default_water_height;
  // NAM0/NAM9 floats as stored; Mutagen ObjectBoundsMin/Max are these divided by 4096.
  std::optional<std::array<float, 2>> bounds_min;
  std::optional<std::array<float, 2>> bounds_max;
  std::optional<std::uint8_t> flags;
  std::optional<FormKey> lod_water;
  std::optional<float> lod_water_height;
};

struct CellGrid {
  std::int32_t x{0};
  std::int32_t y{0};
  std::optional<std::uint8_t> land_flags;
};

struct CellLighting {
  ByteColor ambient;
  ByteColor directional;
  ByteColor fog_near_color;
  float fog_near{0.0f};
  float fog_far{0.0f};
  std::int32_t directional_rotation_xy{0};
  std::int32_t directional_rotation_z{0};
  float directional_fade{0.0f};
  float fog_clip_distance{0.0f};
  float fog_power{0.0f};
  std::optional<ByteColor> fog_far_color;
  std::optional<float> fog_max;
  std::optional<float> light_fade_begin;
  std::optional<float> light_fade_end;
  std::optional<std::uint32_t> inherits;
  std::optional<float> near_height_mid;
  std::optional<float> near_height_range;
  std::optional<ByteColor> fog_color_high_near;
  std::optional<ByteColor> fog_color_high_far;
  std::optional<float> high_density_scale;
  std::optional<float> fog_near_scale;
  std::optional<float> fog_far_scale;
  std::optional<float> fog_high_near_scale;
  std::optional<float> fog_high_far_scale;
  std::optional<float> far_height_mid;
  std::optional<float> far_height_range;
};

struct WaterCurrent {
  std::array<float, 3> velocity{0.0f, 0.0f, 0.0f};
  float unnamed{0.0f};
};

struct ExteriorLod {
  FormKey worldspace;
  std::array<float, 3> offset{0.0f, 0.0f, 0.0f};
};

struct CellData {
  std::optional<std::uint16_t> flags;
  std::optional<CellGrid> grid;
  std::optional<CellLighting> lighting;
  std::optional<FormKey> lighting_template;
  std::optional<float> water_height;
  std::optional<FormKey> image_space;
  std::optional<FormKey> encounter_zone;
  std::optional<FormKey> location;
  std::optional<FormKey> worldspace;
  std::optional<FormKey> water;
  std::optional<std::uint16_t> previs_timestamp;
  std::optional<std::uint16_t> precombined_timestamp;
  std::optional<FormKey> previs_root;
  std::vector<FormKey> regions;
  std::vector<FormKey> previs_references;
  std::optional<FormKey> acoustic_space;
  std::optional<FormKey> music_type;
  std::optional<FormKey> sky_weather_region;
  std::optional<FormKey> god_rays;
  std::optional<FormKey> lock_list;
  std::optional<FormKey> owner;
  std::optional<std::int32_t> owner_rank;
  std::optional<ExteriorLod> exterior_lod;
  std::optional<std::uint32_t> water_current_count;
  std::vector<WaterCurrent> water_currents;
  std::optional<std::string_view> water_environment_map;
};

struct LightingTemplateData {
  std::optional<CellLighting> lighting;
  std::optional<FormKey> god_rays;
};

struct WaterSelection {
  FormKey water;
  const RecordRef* record{nullptr};
  bool uses_default_water{false};
  std::optional<float> height;
};

class WorldDecodeError : public EnvironmentError {
public:
  using EnvironmentError::EnvironmentError;
};

// Both throw WorldDecodeError on a wrong signature or a malformed present subrecord.
WorldspaceData decode_worldspace(const Environment& env, const RecordRef& ref);
CellData decode_cell(const Environment& env, const RecordRef& ref);
LightingTemplateData decode_lighting_template(const Environment& env, const RecordRef& ref);
WaterSelection resolve_water(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
