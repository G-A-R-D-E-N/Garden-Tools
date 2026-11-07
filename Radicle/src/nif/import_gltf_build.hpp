#pragma once

#include "fo4/nif/nif.hpp"
#include "import_gltf_scene.hpp"

#include <cstddef>
#include <vector>

namespace fo4::nif::import_detail {

// Requires every bone of a skinned primitive to name a node of skeleton, which is needed whenever the scene is skinned.
std::vector<std::byte> build_gltf_nif(const GltfScene& scene, const NifFile* skeleton, bool box_collision);

}
