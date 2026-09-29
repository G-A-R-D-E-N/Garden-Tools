#pragma once

#include "fo4/environment/environment.hpp"

#include <array>
#include <cstdint>
#include <optional>

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
};

struct CellGrid {
  std::int32_t x{0};
  std::int32_t y{0};
  std::optional<std::uint8_t> land_flags;
};

// XCLL members through Inherits; later members are not decoded yet.
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
};

class WorldDecodeError : public EnvironmentError {
public:
  using EnvironmentError::EnvironmentError;
};

// Both throw WorldDecodeError on a wrong signature or a malformed present subrecord.
WorldspaceData decode_worldspace(const Environment& env, const RecordRef& ref);
CellData decode_cell(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
