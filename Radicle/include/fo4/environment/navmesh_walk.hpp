#pragma once

#include "fo4/environment/navmesh.hpp"
#include "fo4/environment/navmesh_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::env {

inline constexpr float k_walk_metres_per_unit = 0.0142875f;
inline constexpr std::uint32_t k_walk_class_large_forbidden = 0x1u;
inline constexpr std::uint32_t k_race_flags2_large_pathing = 0x800u;
inline constexpr float k_walk_default_radius_units = 35.0f;
inline constexpr float k_walk_default_height_units = 128.0f;

struct WalkLink {
  std::array<float, 3> start{};
  std::array<float, 3> end{};
  std::uint32_t source_triangle{0};
  std::uint8_t source_edge{0};
  FormKey target_navmesh;
  std::uint32_t target_triangle{0};
  std::uint8_t target_edge{0};
  PathVerdict verdict{PathVerdict::unknown};
};

struct WalkDoor {
  FormKey door;
  std::vector<std::uint32_t> triangles;
};

struct WalkView {
  FormKey navmesh;
  bool has_grid{false};
  std::int16_t grid_x{0};
  std::int16_t grid_y{0};
  std::vector<std::array<float, 3>> vertices;
  std::vector<std::array<std::uint32_t, 3>> triangles;
  std::vector<std::uint32_t> triangle_class;
  std::size_t skipped_triangles{0};
  std::vector<WalkLink> links;
  std::vector<WalkDoor> doors;
};

struct AgentProfile {
  float radius{0.0f};
  float height{0.0f};
  bool large{false};
  std::uint32_t forbidden_class{0};
};

std::optional<WalkView> build_walk_view(const Environment& env, const RecordRef& navmesh, float inset_metres);

std::optional<AgentProfile> agent_profile(const Environment& env, const std::optional<FormKey>& race);

}  // namespace fo4::env
