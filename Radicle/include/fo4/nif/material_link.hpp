#pragma once

#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct MaterialMismatch {
  std::string field;
  std::string nif_value;
  std::string material_value;
};

// Requires a shape block whose shader property is BSLightingShaderProperty (BGSM) or BSEffectShaderProperty (BGEM).
std::vector<MaterialMismatch> compare_shape_material(const NifFile& nif, std::uint32_t shape,
                                                     const material::MaterialFile& file);
// Requires the same shape as compare_shape_material; returns texN edits only for slots that differ.
std::vector<Edit> shape_material_texture_edits(const NifFile& nif, std::uint32_t shape,
                                               const material::MaterialFile& file);
std::vector<std::uint32_t> shapes_using_material(const NifFile& nif, std::string_view material_path);
std::string data_relative_material_path(std::string_view material_path);

}  // namespace fo4::nif
