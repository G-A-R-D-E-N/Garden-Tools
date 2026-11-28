#pragma once

#include "fo4/formid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4 {

struct NavmeshTriangle {
  std::array<std::uint16_t, 3> vertices{};
  std::array<std::int16_t, 3> edges{};
  float clearance{0};
  std::uint8_t traversal{0};
  std::uint32_t flags{0};
  bool edge_is_extra(std::size_t i) const { return i < edges.size() && ((flags >> i) & 1u) != 0; }
};

struct NavmeshDoorLink {
  std::int16_t triangle{0};
  std::uint32_t crc{0};
  FormKey door;
};

struct NavmeshParent {
  std::uint32_t crc{0};
  FormKey world;
  FormKey cell;
  std::int16_t grid_y{0}, grid_x{0};
};

struct NavmeshEdgeLink {
  std::uint32_t type{0};
  FormKey navmesh;
  std::int16_t triangle{0};
  std::uint8_t edge{0};
};

struct NavmeshCover {
  std::array<std::uint16_t, 2> vertices{};
  std::array<std::uint8_t, 4> data{};
};

struct NavmeshCoverMap {
  std::uint16_t cover{0};
  std::uint16_t triangle{0};
};

struct NavmeshWaypoint {
  std::array<float, 3> position{};
  std::int16_t triangle{0};
  std::uint32_t flags{0};
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

}
