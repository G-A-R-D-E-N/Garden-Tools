#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::forms {

struct WorldEdit {
  std::optional<std::optional<FormKey>> parent;
  std::optional<std::uint16_t> parent_flags;
  std::optional<std::optional<FormKey>> climate;
  std::optional<std::optional<FormKey>> water;
  std::optional<std::optional<FormKey>> lighting_template;
  std::optional<float> default_land_height;
  std::optional<float> default_water_height;
  std::optional<std::array<float, 2>> bounds_min;
  std::optional<std::array<float, 2>> bounds_max;
  std::optional<std::uint8_t> flags;
  std::optional<std::optional<FormKey>> lod_water;
  std::optional<float> lod_water_height;
};

enum class WorldEditStatus {
  edited,
  world_missing,
  world_deleted,
  not_a_world,
  target_missing,
  target_type_not_allowed,
  target_not_master,
  invalid_value,
  malformed,
};

struct WorldEditResult {
  WorldEditStatus status{WorldEditStatus::world_missing};
  std::string copied_from;
};

WorldEditResult edit_world(Plugin& patch,
                           const fo4::env::Environment& env,
                           const FormKey& world,
                           const WorldEdit& edit);

enum class WorldPropertyKind {
  scalar,
  link,
  flags,
};

struct WorldPropertyDescriptor {
  std::string name;
  std::string label;
  WorldPropertyKind kind{WorldPropertyKind::scalar};
  double minimum{0.0};
  double maximum{0.0};
  std::vector<std::string> targets;
};

std::vector<WorldPropertyDescriptor> world_property_descriptors();

}  // namespace fo4::forms
