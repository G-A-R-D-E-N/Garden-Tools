#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <optional>
#include <vector>

namespace fo4::env {

struct GrassData {
  std::uint8_t density{0}, min_slope{0}, max_slope{0};
  std::uint16_t units_from_water{0};
  std::uint32_t water_type{0};
  float position_range{0}, height_range{0}, color_range{0}, wave_period{0};
  std::uint8_t flags{0};

  bool water_filter_active() const { return water_type <= 5; }
};

struct HarvestData {
  std::optional<FormKey> ingredient, sound;
  std::optional<std::array<std::uint8_t, 4>> seasons;
};

struct TreeData {
  HarvestData harvest;
  std::optional<std::array<float, 12>> wind;
};

struct FloraData {
  HarvestData harvest;
  std::optional<std::uint16_t> flags;
};

std::optional<GrassData> decode_grass(const Environment& env, const RecordRef& ref);
TreeData decode_tree(const Environment& env, const RecordRef& ref);
FloraData decode_flora(const Environment& env, const RecordRef& ref);
std::vector<FormKey> decode_landscape_grasses(const Environment& env, const RecordRef& ref);
std::vector<FormKey> landscape_grasses_runtime_view(const Environment& env, const RecordRef& ref);

}
