#pragma once

#include "fo4/plugin/navmesh_types.hpp"
#include "fo4/plugin/mutation.hpp"

namespace fo4 {

enum class NaviLinkKind { edge, preferred_edge, door };
ScalarMutationStatus set_navi_path_link(Plugin& plugin, const FormKey& key, std::size_t path,
                                        std::size_t index, const FormKey& target);
ScalarMutationStatus set_navi_link(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                   NaviLinkKind kind, std::size_t index, const FormKey& target);

ScalarMutationStatus set_navmesh_edge_target(Plugin& plugin, const FormKey& key, std::size_t index,
                                           const FormKey& target);

ScalarMutationStatus set_navmesh_vertex(Plugin& plugin, const FormKey& key, std::size_t index,
                                      const std::array<float, 3>& value);
ScalarMutationStatus set_navmesh_triangle(Plugin& plugin, const FormKey& key, std::size_t index,
                                        const NavmeshTriangle& value);
ScalarMutationStatus set_navmesh_door(Plugin& plugin, const FormKey& key, std::size_t index,
                                    const NavmeshDoorLink& value);

}
