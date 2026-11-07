#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct ImportOptions {
  std::string material;
  std::string tex_diffuse;
  std::string tex_normal;
  bool box_collision{false};
  bool hull_collision{false};
  bool mesh_collision{false};
  float body_mass{0.0f};
  bool from_blender{false};
  std::vector<std::byte> skeleton;
};

using GltfUriLoader = std::function<std::vector<std::byte>(std::string_view uri)>;

std::vector<std::byte> import_obj(std::string_view obj_text, const ImportOptions& options);
// Requires a binary .glb or a glTF 2.0 JSON document; an external URI is read through load_uri, and a data: URI is decoded.
std::vector<std::byte> import_gltf(std::span<const std::byte> bytes, const ImportOptions& options,
                                   const GltfUriLoader& load_uri = {});

}
