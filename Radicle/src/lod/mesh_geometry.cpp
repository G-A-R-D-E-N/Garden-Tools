#include "fo4/lod/mesh_geometry.hpp"

#include "fo4/lod/object_atlas.hpp"
#include "fo4/material/material_file.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

namespace fo4::lod {
namespace {

std::string lower_copy(std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

}  // namespace

// Constraint: a STAT model path may omit the folder, so the archive lookup must add the Meshes prefix here.
std::string mesh_asset_path(std::string_view mesh) {
  std::string path{mesh};
  std::ranges::replace(path, '/', '\\');
  if (lower_copy(path).rfind("meshes\\", 0) == 0) return path;
  return "Meshes\\" + path;
}

MeshMaterial shape_material(const fo4::nif::Shape& shape) {
  MeshMaterial out;
  // Invariant: measured vanilla lists diffuse at 0, normal at 1 and specular at 7, never specular at 2.
  if (shape.textures.size() > 0) out.textures[0] = shape.textures[0];
  if (shape.textures.size() > 1) out.textures[1] = shape.textures[1];
  if (shape.textures.size() > 7) out.textures[2] = shape.textures[7];
  return out;
}

std::optional<std::pair<bool, std::array<std::string, 3>>> read_material_textures(const archive::AssetResolver& assets,
                                                                                 std::string_view path) {
  const auto location = assets.locate(std::string{path});
  if (!location.has_value()) return std::nullopt;
  try {
    const auto file = material::read_material_file(assets.read(*location));
    std::pair<bool, std::array<std::string, 3>> out;
    out.first = material::get_field<bool>(file, "AlphaTest").value_or(false);
    const std::array<std::string_view, 3> names{"DiffuseTexture", "NormalTexture", "SmoothSpecTexture"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      if (const auto value = material::get_field<std::string>(file, names[index])) {
        out.second[index] = std::string{material::text(*value)};
      }
    }
    return out;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

MeshGeometryReader::MeshGeometryReader(const archive::AssetResolver& assets) : assets_(assets) {}

const fo4::nif::NifFile* MeshGeometryReader::nif(std::string_view mesh) {
  const auto found = nifs_.find(mesh_asset_path(mesh));
  return found == nifs_.end() ? nullptr : found->second.get();
}

std::optional<std::pair<std::string, MeshMaterial>> MeshGeometryReader::swapped_material(
    std::string_view material, const std::vector<std::pair<std::string, std::string>>& swaps) {
  if (swaps.empty() || material.empty()) return std::nullopt;
  const auto name = lower_copy(lod_data_folder_path(material, "Materials"));
  for (const auto& swap : swaps) {
    if (lower_copy(lod_data_folder_path(swap.first, "Materials")) != name) continue;
    const auto path = lod_data_folder_path(swap.second, "Materials");
    const auto key = lower_copy(path);
    auto found = swapped_.find(key);
    if (found == swapped_.end()) {
      std::optional<MeshMaterial> loaded;
      if (const auto read = read_material_textures(assets_, path)) {
        MeshMaterial out;
        out.alpha_test = read->first;
        out.textures = read->second;
        loaded = std::move(out);
      }
      found = swapped_.emplace(key, std::move(loaded)).first;
    }
    if (!found->second) return std::nullopt;
    return std::make_pair(path, *found->second);
  }
  return std::nullopt;
}

const std::optional<std::vector<ResolvedMeshShape>>& MeshGeometryReader::read(std::string_view mesh) {
  const auto path = mesh_asset_path(mesh);
  const auto found = parsed_.find(path);
  if (found != parsed_.end()) return found->second;
  auto& slot = parsed_[path];
  const auto location = assets_.locate(path);
  if (!location.has_value()) return slot;
  try {
    const auto bytes = assets_.read(*location);
    auto parsed = fo4::nif::read_nif(bytes);
    std::vector<ResolvedMeshShape> shapes;
    for (const auto& shape : fo4::nif::read_shapes(parsed)) {
      ResolvedMeshShape resolved;
      resolved.shape = shape;
      resolved.material.textures = shape_material(shape).textures;
      // Invariant: a shape's .bgsm textures and alpha test win over the mesh's own, resolved through the Materials folder.
      if (!shape.material.empty() && lower_copy(shape.material).ends_with(".bgsm")) {
        const auto override_material =
            read_material_textures(assets_, lod_data_folder_path(shape.material, "Materials"));
        if (override_material.has_value()) {
          resolved.material.alpha_test = override_material->first;
          resolved.material.textures = override_material->second;
        }
      }
      shapes.push_back(std::move(resolved));
    }
    nifs_[path] = std::make_shared<const fo4::nif::NifFile>(std::move(parsed));
    slot = std::move(shapes);
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception&) {
    slot.reset();
  }
  return slot;
}

}  // namespace fo4::lod
