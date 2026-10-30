#include "tree_internal.hpp"

#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif::detail {
namespace {

constexpr std::size_t k_texture_slots = 8;
constexpr std::size_t k_greyscale_slot = 1;
constexpr std::size_t k_env_map_slot = 2;
constexpr std::size_t k_normal_slot = 3;
constexpr std::size_t k_env_mask_slot = 4;

std::string flag_label(std::string_view name) {
  std::string label(name);
  for (char& ch : label) {
    if (ch == '_') ch = ' ';
  }
  return label;
}

std::vector<TreeFlag> flag_toggles(const ShaderFlags& flags) {
  std::vector<TreeFlag> toggles;
  const auto value = flags.value();
  toggles.reserve(shader_flag_count());
  for (std::size_t bit = 0; bit < shader_flag_count(); ++bit) {
    const char* name = shader_flag_name(bit);
    if (!name) continue;
    toggles.push_back({name, flag_label(name), (value >> bit & 1U) != 0});
  }
  return toggles;
}

std::vector<float> floats(const float* values, std::size_t count) {
  return std::vector<float>(values, values + count);
}

std::string texture_slot(const std::vector<std::string>& textures, std::size_t slot) {
  return slot < textures.size() ? textures[slot] : std::string{};
}

void append_texture_slots(std::vector<TreeField>& fields, const std::vector<std::string>& textures) {
  for (std::size_t slot = 0; slot < k_texture_slots; ++slot) {
    fields.push_back(tex_field("tex" + std::to_string(slot), "Texture " + std::to_string(slot),
                               texture_slot(textures, slot)));
  }
}

}  // namespace

std::vector<TreeField> lighting_shader_fields(const NifFile& nif, std::uint32_t block) {
  const auto shader = read_lighting_shader(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", shader.name));
  fields.push_back(readonly_field("materialClass", "Material class",
                                  std::string(lighting_type_name(shader.shader_type))));
  fields.push_back(flags_field("shaderFlags", "Shader flags", flag_toggles(shader.flags)));
  fields.push_back(numbers_field("uvOffset", "UV offset", floats(shader.uv_offset, 2), "vec2"));
  fields.push_back(numbers_field("uvScale", "UV scale", floats(shader.uv_scale, 2), "vec2"));
  fields.push_back(numbers_field("emissiveColor", "Emissive color", floats(shader.emissive_color, 3), "vec3"));
  fields.push_back(number_field("emissiveMultiple", "Emissive multiple", shader.emissive_multiple));
  if (shader.has_root_material) fields.push_back(text_field("rootMaterial", "Root material", shader.root_material));
  fields.push_back(number_field("textureClamp", "Texture clamp mode", static_cast<float>(shader.texture_clamp), "int"));
  fields.push_back(number_field("alpha", "Alpha", shader.alpha));
  fields.push_back(number_field("refractionStrength", "Refraction strength", shader.refraction_strength));
  fields.push_back(number_field("smoothness", "Smoothness", shader.smoothness));
  fields.push_back(numbers_field("specularColor", "Specular color", floats(shader.specular_color, 3), "vec3"));
  fields.push_back(number_field("specularStrength", "Specular strength", shader.specular_strength));
  fields.push_back(number_field("subsurfaceRolloff", "Subsurface rolloff", shader.subsurface_rolloff));
  fields.push_back(number_field("rimlightPower", "Rimlight power", shader.rimlight_power));
  if (shader.has_backlight) fields.push_back(number_field("backlightPower", "Backlight power", shader.backlight_power));
  if (shader.has_grayscale_to_palette_scale) {
    fields.push_back(number_field("grayscaleToPaletteScale", "Grayscale to palette scale",
                                  shader.grayscale_to_palette_scale));
  }
  fields.push_back(number_field("fresnelPower", "Fresnel power", shader.fresnel_power));
  if (shader.has_wetness) {
    fields.push_back(number_field("wetnessSpecScale", "Wetness spec scale", shader.wetness.spec_scale));
    fields.push_back(number_field("wetnessSpecPower", "Wetness spec power", shader.wetness.spec_power));
    fields.push_back(number_field("wetnessMinVar", "Wetness min var", shader.wetness.min_var));
    fields.push_back(number_field("wetnessEnvMapScale", "Wetness env map scale", shader.wetness.env_map_scale));
    fields.push_back(number_field("wetnessFresnelPower", "Wetness fresnel power", shader.wetness.fresnel_power));
    fields.push_back(number_field("wetnessMetalness", "Wetness metalness", shader.wetness.metalness));
  }
  if (shader.has_env_map) {
    fields.push_back(number_field("envMapScale", "Environment map scale", shader.env_map_scale));
    fields.push_back(bool_field("envMapSSR", "Use screen space reflections", shader.env_map_ssr != 0));
    fields.push_back(bool_field("envMapWetnessSSR", "Wetness control: use SSR", shader.env_map_wetness_ssr != 0));
  }
  if (shader.has_skin_tint) {
    fields.push_back(numbers_field("skinTintColor", "Skin tint color", floats(shader.skin_tint, 3), "vec3"));
    fields.push_back(number_field("skinTintAlpha", "Skin tint alpha", shader.skin_tint[3]));
  }
  if (shader.has_hair_tint) {
    fields.push_back(numbers_field("hairTintColor", "Hair tint color", floats(shader.hair_tint, 3), "vec3"));
  }
  if (shader.has_snow) {
    fields.push_back(numbers_field("sparkleParameters", "Sparkle parameters", floats(shader.snow, 3), "vec3"));
    fields.push_back(number_field("sparkleW", "Sparkle W", shader.snow[3]));
  }
  if (shader.has_parallax_occ) {
    fields.push_back(number_field("parallaxMaxPasses", "Max passes", shader.parallax_occ[0]));
    fields.push_back(number_field("parallaxScale", "Parallax scale", shader.parallax_occ[1]));
  }
  if (shader.has_multi_layer_parallax) {
    fields.push_back(number_field("multiLayerThickness", "Inner layer thickness", shader.multi_layer_parallax[0]));
    fields.push_back(number_field("multiLayerRefractionScale", "Refraction scale",
                                  shader.multi_layer_parallax[1]));
    fields.push_back(numbers_field("multiLayerInnerScale", "Inner layer texture scale",
                                   floats(shader.multi_layer_parallax + 2, 2), "vec2"));
    fields.push_back(number_field("multiLayerEnvMapStrength", "Env map strength",
                                  shader.multi_layer_parallax[4]));
  }
  if (shader.has_eye) {
    fields.push_back(number_field("eyeCubemapScale", "Eye cubemap scale", shader.eye_cubemap_scale));
    fields.push_back(numbers_field("leftEyeCenter", "Left eye reflection center", floats(shader.eye_left, 3), "vec3"));
    fields.push_back(numbers_field("rightEyeCenter", "Right eye reflection center", floats(shader.eye_right, 3),
                                   "vec3"));
  }
  return fields;
}

std::vector<TreeField> effect_shader_fields(const NifFile& nif, std::uint32_t block) {
  const auto shader = read_effect_shader(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", shader.name));
  fields.push_back(flags_field("shaderFlags", "Shader flags", flag_toggles(shader.flags)));
  fields.push_back(numbers_field("uvOffset", "UV offset", floats(shader.uv_offset, 2), "vec2"));
  fields.push_back(numbers_field("uvScale", "UV scale", floats(shader.uv_scale, 2), "vec2"));
  fields.push_back(tex_field("sourceTexture", "Source texture", shader.source_texture));
  fields.push_back(number_field("textureClamp", "Texture clamp mode", static_cast<float>(shader.texture_clamp), "int"));
  fields.push_back(number_field("lightingInfluence", "Lighting influence",
                                static_cast<float>(shader.lighting_influence), "int"));
  fields.push_back(number_field("envMapMinLOD", "Env map min LOD", static_cast<float>(shader.env_map_min_lod), "int"));
  fields.push_back(number_field("falloffStartAngle", "Falloff start angle", shader.falloff_start_angle));
  fields.push_back(number_field("falloffStopAngle", "Falloff stop angle", shader.falloff_stop_angle));
  fields.push_back(number_field("falloffStartOpacity", "Falloff start opacity", shader.falloff_start_opacity));
  fields.push_back(number_field("falloffStopOpacity", "Falloff stop opacity", shader.falloff_stop_opacity));
  fields.push_back(numbers_field("baseColor", "Base color", floats(shader.base_color, 3), "vec3"));
  fields.push_back(number_field("baseColorAlpha", "Base color alpha", shader.base_color[3]));
  fields.push_back(number_field("baseColorScale", "Base color scale", shader.base_color_scale));
  fields.push_back(number_field("softFalloffDepth", "Soft falloff depth", shader.soft_falloff_depth));
  const auto& textures = shader.textures;
  fields.push_back(tex_field("greyscaleTexture", "Greyscale texture", texture_slot(textures, k_greyscale_slot)));
  // Invariant: the environment map, normal and env mask paths and the scale exist only from BS 130.
  if (shader.offsets.env_map_scale != 0) {
    fields.push_back(tex_field("envMapTexture", "Env map texture", texture_slot(textures, k_env_map_slot)));
    fields.push_back(tex_field("normalTexture", "Normal texture", texture_slot(textures, k_normal_slot)));
    fields.push_back(tex_field("envMaskTexture", "Env mask texture", texture_slot(textures, k_env_mask_slot)));
    fields.push_back(number_field("envMapScale", "Environment map scale", shader.env_map_scale));
  }
  return fields;
}

std::vector<TreeField> alpha_property_fields(const NifFile& nif, std::uint32_t block) {
  const auto alpha = read_alpha(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", alpha.name));
  fields.push_back(number_field("flags", "Flags", static_cast<float>(alpha.flags), "int"));
  fields.push_back(bool_field("alphaBlend", "Alpha blend", alpha.blend_enabled));
  fields.push_back(readonly_field("sourceBlend", "Source blend", alpha_blend_name(alpha.source_blend)));
  fields.push_back(readonly_field("destinationBlend", "Destination blend",
                                  alpha_blend_name(alpha.destination_blend)));
  fields.push_back(bool_field("alphaTest", "Alpha test", alpha.test_enabled));
  fields.push_back(readonly_field("testFunction", "Test function", alpha_test_name(alpha.test_function)));
  fields.push_back(number_field("threshold", "Threshold", static_cast<float>(alpha.threshold), "int"));
  return fields;
}

std::vector<TreeField> texture_set_fields(const NifFile& nif, std::uint32_t block) {
  const auto textures = detail::read_texture_set(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(readonly_field("slots", "Slots", std::to_string(textures.size())));
  append_texture_slots(fields, textures);
  return fields;
}

}  // namespace fo4::nif::detail
