#pragma once

#include "fo4/environment/navmesh.hpp"
#include "fo4/environment/navmesh_rules.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace fo4::env {

struct NavmeshPathOptions {
  std::uint32_t forbidden_class{0};
  bool allow_unknown_links{false};
  bool allow_unknown_doors{false};
  std::vector<std::pair<FormKey, DoorState>> door_states;
};

struct NavmeshPathNode {
  FormKey navmesh;
  std::uint32_t triangle{0};
  bool via_link{false};
  bool needs_opening{false};
};

enum class NavmeshPathStatus : std::uint8_t { found, no_path, invalid_endpoint };

struct NavmeshPath {
  NavmeshPathStatus status{NavmeshPathStatus::no_path};
  std::vector<NavmeshPathNode> nodes;
  float cost{0.0f};
};

NavmeshPath find_navmesh_path(const Environment& env, const FormKey& start_navmesh, std::uint32_t start_triangle,
                              const FormKey& goal_navmesh, std::uint32_t goal_triangle,
                              const NavmeshPathOptions& options);

}  // namespace fo4::env
