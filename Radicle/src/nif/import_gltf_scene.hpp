#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "gltf_read.hpp"
#include "import_gltf_mesh.hpp"
#include "import_mesh.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif::import_detail {

struct GltfNode {
  std::string name;
  std::int32_t parent{-1};
  Transform local;
  std::vector<GltfPrimitive> primitives;
};

struct GltfScene {
  std::vector<GltfNode> nodes;
  std::array<Vec3, 2> bounds{};
  bool skinned{false};
};

// Requires the glTF to hold at least one triangle primitive; bones is null when no skeleton was chosen.
GltfScene read_scene(const gltf_detail::Document& doc, const ImportOptions& options, const BoneNames* bones);

}
