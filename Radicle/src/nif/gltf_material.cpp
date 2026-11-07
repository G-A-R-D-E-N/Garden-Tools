#include "gltf_internal.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/shader.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::gltf {
namespace {

// Invariant: BSShaderTextureSet slot 0 is diffuse, slot 1 is normal, and k_smooth_spec_slot is the smoothness/specular map.
constexpr std::size_t k_smooth_spec_slot = 7;
// Invariant: BSEffectShaderProperty stores the normal map at texture index 3, after the source, greyscale and environment paths.
constexpr std::size_t k_effect_normal_slot = 3;
constexpr float k_alpha_levels = 255.0f;

struct Alpha {
  bool blend{false};
  bool test{false};
  std::uint8_t threshold{0};
};

struct Surface {
  std::string name;
  std::string diffuse;
  std::string normal;
  std::string spec;
  bool two_sided{false};
  float roughness{1.0f};
  Alpha alpha;
  bool has_alpha{false};
};

bool has_flag(const ShaderFlags& flags, std::string_view name) {
  const auto names = shader_flag_names(flags);
  return std::ranges::find(names, name) != names.end();
}

bool is_material_name(std::string_view name) {
  if (is_bgsm_path(name)) return true;
  if (name.size() < 5) return false;
  std::string tail(name.substr(name.size() - 5));
  std::ranges::transform(tail, tail.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return tail == ".bgem";
}

std::string slot(const std::vector<std::string>& textures, std::size_t index) {
  return index < textures.size() ? textures[index] : std::string{};
}

float unit(float value) { return std::clamp(value, 0.0f, 1.0f); }

void read_nif_surface(const NifFile& nif, const Shape& shape, Surface& surface) {
  if (shape.shader_ref >= 0) {
    const auto block = static_cast<std::uint32_t>(shape.shader_ref);
    const auto& type = nif.blocks().at(block).type;
    if (type == "BSLightingShaderProperty") {
      const auto shader = read_lighting_shader(nif, block);
      surface.name = shader.name;
      surface.diffuse = slot(shader.textures, 0);
      surface.normal = slot(shader.textures, 1);
      surface.spec = slot(shader.textures, k_smooth_spec_slot);
      surface.two_sided = has_flag(shader.flags, "Double_Sided");
      surface.roughness = 1.0f - unit(shader.smoothness);
    } else if (type == "BSEffectShaderProperty") {
      const auto effect = read_effect_shader(nif, block);
      surface.name = effect.name;
      surface.diffuse = effect.source_texture;
      surface.normal = slot(effect.textures, k_effect_normal_slot);
      surface.two_sided = has_flag(effect.flags, "Double_Sided");
    }
  }
  if (shape.alpha_ref >= 0 && static_cast<std::size_t>(shape.alpha_ref) < nif.blocks().size() &&
      nif.blocks()[static_cast<std::size_t>(shape.alpha_ref)].type == "NiAlphaProperty") {
    const auto property = read_alpha(nif, static_cast<std::uint32_t>(shape.alpha_ref));
    surface.alpha = {property.blend_enabled, property.test_enabled, property.threshold};
    surface.has_alpha = true;
  }
}

void apply_material_file(const material::MaterialFile& file, Surface& surface) {
  const bool bgem = file.format == material::MaterialFormat::bgem;
  const auto text_of = [&file](std::string_view field) {
    const auto value = material::get_field<std::string>(file, field);
    return value ? std::string(material::text(*value)) : std::string{};
  };
  surface.diffuse = text_of(bgem ? "BaseTexture" : "DiffuseTexture");
  surface.normal = text_of("NormalTexture");
  surface.spec = text_of(bgem ? "SpecularTexture" : "SmoothSpecTexture");
  surface.two_sided = surface.two_sided || material::get_field<bool>(file, "TwoSided").value_or(false);
  if (!bgem) {
    if (const auto smoothness = material::get_field<float>(file, "Smoothness")) surface.roughness = 1.0f - unit(*smoothness);
  }
  if (!surface.has_alpha) {
    surface.alpha.blend = material::get_field<std::uint8_t>(file, "AlphaBlendMode0").value_or(0) != 0;
    surface.alpha.test = material::get_field<bool>(file, "AlphaTest").value_or(false);
    surface.alpha.threshold = material::get_field<std::uint8_t>(file, "AlphaTestRef").value_or(0);
  }
}

void read_material_surface(const GltfOptions& options, Surface& surface, GltfExport& out) {
  if (!is_material_name(surface.name)) return;
  if (options.assets == nullptr) {
    out.warnings.push_back("material not read, no asset resolver: " + surface.name);
    return;
  }
  try {
    const auto location = options.assets->locate(surface.name);
    if (!location) {
      out.warnings.push_back("material not found, the NIF texture set is used: " + surface.name);
      return;
    }
    apply_material_file(material::read_material_file(options.assets->read(*location)), surface);
  } catch (const std::exception& error) {
    out.warnings.push_back("material not read, the NIF texture set is used: " + surface.name + " (" + error.what() + ")");
  }
}

nlohmann::json material_json(const Shape& shape, const Surface& surface, TextureTable& textures) {
  nlohmann::json material{{"name", shape.name}};
  nlohmann::json pbr{{"metallicFactor", 0.0}, {"roughnessFactor", surface.roughness}};
  if (const auto index = textures.add(surface.diffuse, false)) pbr["baseColorTexture"] = {{"index", *index}};
  material["pbrMetallicRoughness"] = std::move(pbr);
  if (const auto index = textures.add(surface.normal, true)) material["normalTexture"] = {{"index", *index}};

  nlohmann::json extras = nlohmann::json::object();
  if (!surface.name.empty()) extras["fo4Material"] = surface.name;
  if (!surface.diffuse.empty()) extras["fo4DiffuseTexture"] = surface.diffuse;
  if (!surface.normal.empty()) extras["fo4NormalTexture"] = surface.normal;
  if (const auto index = textures.add(surface.spec, false)) extras["fo4SmoothSpecTexture"] = {{"index", *index}};
  if (!extras.empty()) material["extras"] = std::move(extras);

  if (surface.alpha.blend) {
    material["alphaMode"] = "BLEND";
  } else if (surface.alpha.test) {
    material["alphaMode"] = "MASK";
    material["alphaCutoff"] = static_cast<float>(surface.alpha.threshold) / k_alpha_levels;
  }
  if (surface.two_sided) material["doubleSided"] = true;
  return material;
}

}  // namespace

nlohmann::json shape_material(const NifFile& nif, const Shape& shape, const GltfOptions& options, TextureTable& textures,
                              GltfExport& out) {
  Surface surface;
  try {
    read_nif_surface(nif, shape, surface);
  } catch (const std::exception& error) {
    out.warnings.push_back("shader of " + shape.name + " not read: " + error.what());
    surface = Surface{};
  }
  read_material_surface(options, surface, out);
  return material_json(shape, surface, textures);
}

}  // namespace fo4::nif::gltf
