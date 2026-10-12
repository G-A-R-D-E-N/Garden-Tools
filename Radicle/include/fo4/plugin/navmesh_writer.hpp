#pragma once

#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/mutation.hpp"

namespace fo4 {

ScalarMutationStatus set_navmesh_vertex(Plugin& plugin, const FormKey& key, std::size_t index,
                                      const std::array<float, 3>& value);
ScalarMutationStatus set_navmesh_triangle(Plugin& plugin, const FormKey& key, std::size_t index,
                                        const env::NavmeshTriangle& value);
ScalarMutationStatus set_navmesh_door(Plugin& plugin, const FormKey& key, std::size_t index,
                                    const env::NavmeshDoorLink& value);

}
