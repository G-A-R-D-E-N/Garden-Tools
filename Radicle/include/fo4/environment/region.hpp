#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::env {

struct RegionPoint {
  float x{0.0f}, y{0.0f};
};

struct RegionArea {
  std::uint32_t edge_falloff{0};
  std::vector<RegionPoint> points;
  std::optional<float> unknown_anam;
};

struct RegionObject {
  FormKey object;
  std::uint16_t parent_index{0};
  float density{0.0f};
  std::uint8_t clustering{0}, min_slope{0}, max_slope{0}, flags{0};
  std::uint16_t radius_with_parent{0}, radius{0};
  float min_height{0.0f}, max_height{0.0f}, sink{0.0f}, sink_variance{0.0f}, size_variance{0.0f};
  std::array<std::uint16_t, 3> angle_variance{0, 0, 0};
};

struct RegionWeather {
  FormKey weather;
  std::uint32_t chance{0};
  std::optional<FormKey> global;
};

struct RegionSound {
  FormKey sound;
  std::uint32_t flags{0};
  float chance{0.0f};
};

struct RegionDataEntry {
  std::uint32_t type{0};
  std::uint8_t override_value{0}, priority{0};
  std::optional<FormKey> music;
  RecordName map_name;
  std::vector<RegionObject> objects;
  std::vector<FormKey> grasses;
  std::vector<RegionWeather> weathers;
  std::vector<RegionSound> sounds;
  std::optional<float> lod_display_distance_multiplier;
  std::optional<float> occlusion_accuracy_dist;
};

struct RegionData {
  std::optional<ByteColor> map_color;
  std::optional<FormKey> worldspace;
  std::vector<RegionArea> areas;
  std::vector<RegionDataEntry> entries;
};

// An RPLD or entry payload must follow the RPLI or RDAT it belongs to and match that RDAT type.
RegionData decode_region(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
