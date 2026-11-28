#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/navmesh_writer.hpp"

namespace fo4 {

ScalarMutationStatus add_navmesh_vertex(Plugin& plugin, const FormKey& key, const std::array<float, 3>& value);
ScalarMutationStatus remove_navmesh_vertex(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_triangle(Plugin& plugin, const FormKey& key, const NavmeshTriangle& value);
ScalarMutationStatus remove_navmesh_triangle(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_edge_link(Plugin& plugin, const FormKey& key, const NavmeshEdgeLink& value);
ScalarMutationStatus remove_navmesh_edge_link(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_door(Plugin& plugin, const FormKey& key, const NavmeshDoorLink& value);
ScalarMutationStatus remove_navmesh_door(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_cover(Plugin& plugin, const FormKey& key, const NavmeshCover& value);
ScalarMutationStatus remove_navmesh_cover(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_cover_map(Plugin& plugin, const FormKey& key, const NavmeshCoverMap& value);
ScalarMutationStatus remove_navmesh_cover_map(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navmesh_waypoint(Plugin& plugin, const FormKey& key, const NavmeshWaypoint& value);
ScalarMutationStatus remove_navmesh_waypoint(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navi_info(Plugin& plugin, const FormKey& key, const NavmeshInfo& value);
ScalarMutationStatus remove_navi_info(Plugin& plugin, const FormKey& key, std::size_t info_index);

ScalarMutationStatus add_navi_link(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                   NaviLinkKind kind, const FormKey& navmesh);
ScalarMutationStatus add_navi_door(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                   const NaviDoorLink& door);
ScalarMutationStatus remove_navi_link(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                      NaviLinkKind kind, std::size_t index);

ScalarMutationStatus add_navi_deleted(Plugin& plugin, const FormKey& key, const FormKey& navmesh);
ScalarMutationStatus remove_navi_deleted(Plugin& plugin, const FormKey& key, std::size_t index);

ScalarMutationStatus add_navi_path(Plugin& plugin, const FormKey& key, const std::vector<FormKey>& links);
ScalarMutationStatus remove_navi_path(Plugin& plugin, const FormKey& key, std::size_t path);

FormKey create_navmesh(Plugin& plugin, const FormKey& cell);

}
