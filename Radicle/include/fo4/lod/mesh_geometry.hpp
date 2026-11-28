#pragma once

// Invariant: one decoder reads a mesh into shapes and resolves its material, so every LOD caller sees the same key.

#include "fo4/archive/assets.hpp"
#include "fo4/nif/nif.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::lod {

struct MeshMaterial {
  bool alpha_test{false};
  std::array<std::string, 3> textures{};
};

struct ResolvedMeshShape {
  fo4::nif::Shape shape;
  MeshMaterial material;
};

class MeshGeometryReader {
 public:
  explicit MeshGeometryReader(const archive::AssetResolver& assets);

  // Constraint: empty when the mesh is missing or does not parse, which a caller counts as a missing mesh.
  const std::optional<std::vector<ResolvedMeshShape>>& read(std::string_view mesh);

  const fo4::nif::NifFile* nif(std::string_view mesh);

  // Constraint: empty when no pair names the material or the target is unreadable.
  std::optional<std::pair<std::string, MeshMaterial>> swapped_material(
      std::string_view material, const std::vector<std::pair<std::string, std::string>>& swaps);

 private:
  const archive::AssetResolver& assets_;
  std::map<std::string, std::optional<std::vector<ResolvedMeshShape>>, std::less<>> parsed_;
  std::map<std::string, std::shared_ptr<const fo4::nif::NifFile>, std::less<>> nifs_;
  std::map<std::string, std::optional<MeshMaterial>, std::less<>> swapped_;
};

MeshMaterial shape_material(const fo4::nif::Shape& shape);

// Invariant: this is the asset path the reader opens, so a loose stat and a mesh read name the same file.
std::string mesh_asset_path(std::string_view mesh);

std::optional<std::pair<bool, std::array<std::string, 3>>> read_material_textures(const archive::AssetResolver& assets,
                                                                                 std::string_view path);

}  // namespace fo4::lod
