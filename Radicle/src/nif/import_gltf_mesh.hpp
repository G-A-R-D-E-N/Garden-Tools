#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "gltf_frame.hpp"
#include "gltf_read.hpp"
#include "import_mesh.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fo4::nif::import_detail {

using BoneNames = std::unordered_set<std::string>;

struct GltfMaterial {
  std::string diffuse;
  std::string normal;
  std::string bgsm;
};

struct GltfPrimitive {
  Mesh mesh;
  GltfMaterial material;
  std::vector<std::string> bone_names;
  std::vector<Transform> bone_binds;
};

// Requires a triangle primitive whose attributes the NIF writer can hold; skin is the owning node's skin.
GltfPrimitive decode_primitive(const gltf_detail::Document& doc, const nlohmann::json& primitive,
                               const std::string& shape_name, std::optional<std::size_t> skin,
                               const gltf_detail::Frame& frame, const BoneNames* bones, const ImportOptions& options);

}
