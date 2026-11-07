#include "fo4/nif/material_link.hpp"

#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr float k_tolerance = 1e-5F;
constexpr std::string_view k_lighting_type = "BSLightingShaderProperty";
constexpr std::string_view k_effect_type = "BSEffectShaderProperty";
constexpr std::string_view k_textures_root = "textures\\";
constexpr std::string_view k_materials_root = "materials\\";

struct MaterialTexture {
  std::string_view field;
  std::size_t slot;
};

// Constraint: slots are measured on vanilla texture sets; Wrinkles, Specular, Lighting and Flow have no slot here.
constexpr MaterialTexture k_lighting_textures[] = {
    {"DiffuseTexture", 0}, {"NormalTexture", 1}, {"GlowTexture", 2}, {"GreyscaleTexture", 3},
    {"EnvmapTexture", 4},  {"SmoothSpecTexture", 7},
};
constexpr MaterialTexture k_effect_textures[] = {
    {"BaseTexture", 0}, {"GrayscaleTexture", 1}, {"EnvmapTexture", 2}, {"NormalTexture", 3}, {"EnvmapMaskTexture", 4},
};

std::string normal_path(std::string_view path) {
  std::string out(fo4::material::text(path));
  for (auto& ch : out) {
    ch = ch == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

std::string rooted_path(std::string_view path, std::string_view root) {
  auto out = normal_path(path);
  if (!out.empty() && !out.starts_with(root)) out.insert(0, root);
  return out;
}

std::string texture_text(std::string_view path) {
  std::string out(fo4::material::text(path));
  if (out.empty()) return out;
  std::replace(out.begin(), out.end(), '/', '\\');
  if (!normal_path(out).starts_with(k_textures_root)) out.insert(0, k_textures_root);
  return out;
}

std::string color_text(const float* rgb) { return std::format("{} {} {}", rgb[0], rgb[1], rgb[2]); }

class MaterialChecks {
public:
  MaterialChecks(const fo4::material::MaterialFile& file, std::vector<MaterialMismatch>& out) : file_(file), out_(out) {}

  void number(std::string_view field, float nif) {
    const auto value = fo4::material::get_field<float>(file_, field);
    if (value && std::fabs(*value - nif) > k_tolerance) add(field, std::format("{}", nif), std::format("{}", *value));
  }

  void u8(std::string_view field, std::uint8_t nif) {
    const auto value = fo4::material::get_field<std::uint8_t>(file_, field);
    if (value && *value != nif) add(field, std::to_string(nif), std::to_string(*value));
  }

  void color(std::string_view field, const float* nif) {
    const auto value = fo4::material::get_field<fo4::material::Color3>(file_, field);
    if (!value) return;
    const float material[3]{value->r, value->g, value->b};
    for (std::size_t i = 0; i < 3; ++i) {
      if (std::fabs(material[i] - nif[i]) > k_tolerance) {
        add(field, color_text(nif), color_text(material));
        return;
      }
    }
  }

  void path(std::string_view field, std::string_view nif, std::string_view root) {
    const auto value = fo4::material::get_field<std::string>(file_, field);
    if (value && rooted_path(nif, root) != rooted_path(*value, root)) {
      add(field, std::string(nif), std::string(fo4::material::text(*value)));
    }
  }

  void root_material(std::string_view nif) {
    const auto value = fo4::material::get_field<std::string>(file_, "RootMaterialPath");
    if (!value || fo4::material::text(nif).empty() || fo4::material::text(*value).empty()) return;
    if (rooted_path(nif, k_materials_root) != rooted_path(*value, k_materials_root)) {
      add("RootMaterialPath", std::string(nif), std::string(fo4::material::text(*value)));
    }
  }

  void texture(const MaterialTexture& texture, const std::vector<std::string>& textures) {
    path(texture.field, texture.slot < textures.size() ? std::string_view(textures[texture.slot]) : std::string_view{},
         k_textures_root);
  }

private:
  void add(std::string_view field, std::string nif, std::string material) {
    out_.push_back(MaterialMismatch{std::string(field), std::move(nif), std::move(material)});
  }

  const fo4::material::MaterialFile& file_;
  std::vector<MaterialMismatch>& out_;
};

std::uint32_t shader_of(const NifFile& nif, std::uint32_t shape) {
  if (!is_shape_block_type(nif.blocks().at(shape).type)) {
    throw parse_error(nif.blocks()[shape].offset, "material link needs a shape block");
  }
  const auto ref = read_shape(nif, shape).shader_ref;
  if (ref < 0 || static_cast<std::size_t>(ref) >= nif.blocks().size()) {
    throw parse_error(nif.blocks()[shape].offset, "shape has no shader property");
  }
  const auto& type = nif.blocks()[static_cast<std::size_t>(ref)].type;
  if (type != k_lighting_type && type != k_effect_type) {
    throw parse_error(nif.blocks()[shape].offset, "shape's shader is not a lighting or effect shader");
  }
  return static_cast<std::uint32_t>(ref);
}

void compare_lighting(const NifFile& nif, std::uint32_t shader, MaterialChecks& check) {
  const auto fields = read_lighting_shader(nif, shader);
  check.number("UOffset", fields.uv_offset[0]);
  check.number("VOffset", fields.uv_offset[1]);
  check.number("UScale", fields.uv_scale[0]);
  check.number("VScale", fields.uv_scale[1]);
  check.number("Alpha", fields.alpha);
  check.number("RefractionPower", fields.refraction_strength);
  check.number("Smoothness", fields.smoothness);
  check.number("FresnelPower", fields.fresnel_power);
  check.number("SpecularMult", fields.specular_strength);
  check.color("SpecularColor", fields.specular_color);
  check.color("EmittanceColor", fields.emissive_color);
  check.number("EmittanceMult", fields.emissive_multiple);
  if (fields.has_env_map) check.number("EnvironmentMappingMaskScale", fields.env_map_scale);
  if (fields.has_grayscale_to_palette_scale) {
    check.number("GrayscaleToPaletteScale", fields.grayscale_to_palette_scale);
  }
  if (fields.has_root_material) check.root_material(fields.root_material);
  for (const auto& texture : k_lighting_textures) check.texture(texture, fields.textures);
}

void compare_effect(const NifFile& nif, std::uint32_t shader, MaterialChecks& check) {
  const auto fields = read_effect_shader(nif, shader);
  check.number("UOffset", fields.uv_offset[0]);
  check.number("VOffset", fields.uv_offset[1]);
  check.number("UScale", fields.uv_scale[0]);
  check.number("VScale", fields.uv_scale[1]);
  check.color("BaseColor", fields.base_color);
  check.number("BaseColorScale", fields.base_color_scale);
  check.number("FalloffStartAngle", fields.falloff_start_angle);
  check.number("FalloffStopAngle", fields.falloff_stop_angle);
  check.number("FalloffStartOpacity", fields.falloff_start_opacity);
  check.number("FalloffStopOpacity", fields.falloff_stop_opacity);
  check.u8("EnvmapMinLOD", fields.env_map_min_lod);
  check.number("SoftDepth", fields.soft_falloff_depth);
  if (fields.offsets.env_map_scale != 0) check.number("EffectEnvironmentMappingMaskScale", fields.env_map_scale);
  for (const auto& texture : k_effect_textures) check.texture(texture, fields.textures);
}

std::vector<Edit> texture_edits(std::uint32_t shape, const fo4::material::MaterialFile& file,
                                std::span<const MaterialTexture> table, const std::vector<std::string>& textures) {
  std::vector<Edit> edits;
  for (const auto& texture : table) {
    const auto value = fo4::material::get_field<std::string>(file, texture.field);
    if (!value) continue;
    const auto current = texture.slot < textures.size() ? std::string_view(textures[texture.slot]) : std::string_view{};
    if (rooted_path(current, k_textures_root) == rooted_path(*value, k_textures_root)) continue;
    edits.push_back(Edit{shape, "tex" + std::to_string(texture.slot), texture_text(*value)});
  }
  return edits;
}

}  // namespace

std::vector<MaterialMismatch> compare_shape_material(const NifFile& nif, std::uint32_t shape,
                                                     const material::MaterialFile& file) {
  const auto shader = shader_of(nif, shape);
  const bool lighting = nif.blocks()[shader].type == k_lighting_type;
  const bool bgsm = file.format == material::MaterialFormat::bgsm;
  if (lighting != bgsm) {
    const auto material_format = bgsm ? "BGSM" : "BGEM";
    return {MaterialMismatch{"Format", nif.blocks()[shader].type, material_format}};
  }
  std::vector<MaterialMismatch> mismatches;
  MaterialChecks check(file, mismatches);
  if (lighting) {
    compare_lighting(nif, shader, check);
  } else {
    compare_effect(nif, shader, check);
  }
  return mismatches;
}

std::vector<Edit> shape_material_texture_edits(const NifFile& nif, std::uint32_t shape,
                                               const material::MaterialFile& file) {
  const auto shader = shader_of(nif, shape);
  if (nif.blocks()[shader].type != k_lighting_type) {
    return texture_edits(shape, file, k_effect_textures, read_effect_shader(nif, shader).textures);
  }
  const auto fields = read_lighting_shader(nif, shader);
  auto edits = texture_edits(shape, file, k_lighting_textures, fields.textures);
  const auto set = fields.texture_set_ref;
  const bool has_set = set >= 0 && static_cast<std::size_t>(set) < nif.blocks().size() &&
                       nif.blocks()[static_cast<std::size_t>(set)].type == "BSShaderTextureSet";
  if (!edits.empty() && !has_set) {
    throw parse_error(nif.blocks()[shape].offset, "shape's shader has no texture set to fill; the NIF writer cannot create one");
  }
  return edits;
}

std::vector<std::uint32_t> shapes_using_material(const NifFile& nif, std::string_view material_path) {
  const auto wanted = normal_path(data_relative_material_path(material_path));
  std::vector<std::uint32_t> blocks;
  if (wanted.empty()) return blocks;
  for (const auto& shape : read_shapes(nif)) {
    if (normal_path(data_relative_material_path(shape.material)) == wanted) blocks.push_back(shape.block);
  }
  return blocks;
}

std::string data_relative_material_path(std::string_view material_path) {
  std::string text(material_path);
  std::replace(text.begin(), text.end(), '/', '\\');
  std::string lower = text;
  for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (lower.starts_with("materials\\")) return text;
  if (const auto at = lower.find("\\materials\\"); at != std::string::npos) return text.substr(at + 1);
  if (const auto at = lower.rfind("\\data\\"); at != std::string::npos) return text.substr(at + 6);
  return text;
}

}  // namespace fo4::nif
