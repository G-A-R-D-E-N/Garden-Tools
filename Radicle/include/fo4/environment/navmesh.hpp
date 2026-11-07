#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
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
  float clearance{0};
  std::uint8_t traversal{0};
  std::uint32_t flags{0};
  bool edge_is_extra(std::size_t i) const { return i < edges.size() && ((flags >> i) & 1u) != 0; }
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
struct NavmeshPathData {
  std::vector<std::vector<FormKey>> paths;
  std::vector<std::pair<FormKey, std::uint32_t>> anchors;
};

std::vector<NavmeshInfo> decode_navmesh_info(const Environment& env, const RecordRef& ref);
NavmeshPathData decode_navmesh_paths(const Environment& env, const RecordRef& ref);

}
