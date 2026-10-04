#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fo4::env {

struct NavmeshParent {
  std::uint32_t crc{0};
  FormKey world;
  FormKey cell;
  std::int16_t grid_y{0}, grid_x{0};
};

struct NavmeshTriangle {
  std::array<std::uint16_t, 3> vertices{};
  std::array<std::int16_t, 3> edges{};
  float height{0};
  std::uint8_t unknown{0};
  std::uint16_t flags{0}, cover_flags{0};
};

struct NavmeshEdgeLink {
  std::uint32_t type{0};
  FormKey navmesh;
  std::int16_t triangle{0};
  std::uint8_t edge{0};
};

struct NavmeshDoorLink {
  std::int16_t triangle{0};
  std::uint32_t crc{0};
  FormKey door;
};

struct NavmeshGeometry {
  std::uint32_t version{0};
  NavmeshParent parent;
  std::vector<std::array<float, 3>> vertices;
  std::vector<NavmeshTriangle> triangles;
  std::vector<NavmeshEdgeLink> edges;
  std::vector<NavmeshDoorLink> doors;
  std::span<const std::byte> opaque_tail;
};

struct NaviDoorLink {
  std::uint32_t crc{0};
  FormKey door;
};

struct NavmeshInfo {
  FormKey navmesh;
  std::uint32_t flags{0};
  std::array<float, 3> location{};
  float preferred{0};
  std::vector<FormKey> edges, preferred_edges;
  std::vector<NaviDoorLink> doors;
  bool has_island{false};
  std::span<const std::byte> island;
  NavmeshParent parent;
};

std::optional<NavmeshGeometry> decode_navmesh(const Environment& env, const RecordRef& ref);
std::vector<NavmeshInfo> decode_navmesh_info(const Environment& env, const RecordRef& ref);

}
