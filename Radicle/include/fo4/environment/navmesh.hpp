#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/plugin/navmesh_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4::env {

using fo4::NavmeshParent;
using fo4::NavmeshTriangle;
using fo4::NavmeshDoorLink;
using fo4::NavmeshEdgeLink;
using fo4::NavmeshCover;
using fo4::NavmeshCoverMap;
using fo4::NavmeshWaypoint;
using fo4::NaviDoorLink;
using fo4::NavmeshInfo;

struct NavmeshGeometry {
  std::uint32_t version{0};
  NavmeshParent parent;
  std::vector<std::array<float, 3>> vertices;
  std::vector<NavmeshTriangle> triangles;
  std::vector<NavmeshEdgeLink> edges;
  std::vector<NavmeshDoorLink> doors;
  std::span<const std::byte> opaque_tail;
};

std::optional<NavmeshGeometry> decode_navmesh(const Environment& env, const RecordRef& ref);
struct NavmeshPathData {
  std::vector<std::vector<FormKey>> paths;
  std::vector<std::pair<FormKey, std::uint32_t>> anchors;
};

std::vector<NavmeshInfo> decode_navmesh_info(const Environment& env, const RecordRef& ref);
NavmeshPathData decode_navmesh_paths(const Environment& env, const RecordRef& ref);

}
