#include "write_edit_internal.hpp"

#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fo4::nif::edit_detail {
namespace {

constexpr std::uint32_t k_shader_flags_wide = 0x45;
constexpr std::uint32_t k_fresnel_since = 130;
constexpr std::uint32_t k_since_7c = 0x7c;
constexpr std::uint32_t k_extra_textures_since = 130;
constexpr float k_byte_max = 255.0f;
constexpr float k_u16_max = 65535.0f;

void require_present(const NifFile& nif, std::uint32_t block, bool present, const std::string& key) {
  if (!present) fail(nif, block, "key '" + key + "' is not stored by this block");
}

std::uint64_t flag_mask(const NifFile& nif, const Edit& edit) {
  std::uint64_t mask = 0;
  for (const auto& name : names_value(nif, edit)) {
    std::size_t bit = 0;
    for (; bit < shader_flag_count(); ++bit) {
      const char* known = shader_flag_name(bit);
      if (known != nullptr && name == known) break;
    }
    if (bit == shader_flag_count()) fail(nif, edit.id, "key 'shaderFlags' names an unknown flag '" + name + "'");
    mask |= std::uint64_t{1} << bit;
  }
  return mask;
}

void apply_flags(const NifFile& nif, Model& model, std::uint32_t block, const Edit& edit, std::size_t offset,
                 bool wide) {
  const auto mask = flag_mask(nif, edit);
  write_word(nif, model, block, offset, static_cast<std::uint32_t>(mask));
  if (!wide) {
    if (mask >> 32U != 0) fail(nif, block, "key 'shaderFlags' names a bit above 31, which this version does not store");
    return;
  }
  write_word(nif, model, block, offset + 4, static_cast<std::uint32_t>(mask >> 32U));
}

void apply_texture_path(const NifFile& nif, Model& model, std::uint32_t block, std::size_t offset,
                        const Edit& edit, bool present) {
  require_present(nif, block, present, edit.key);
  const auto& text = text_value(nif, edit);
  check_texture_path(nif, block, text);
  set_sized_string(nif, model, block, offset, text);
}

}  // namespace

void apply_lighting_edit(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source,
                         const Edit& edit) {
  const auto shader = read_lighting_shader(nif, source);
  const auto& at = shader.offsets;
  require_present(nif, block, at.end != 0, edit.key);
  const auto version = nif.bs_version();
  if (edit.key == "name") return set_name(nif, model, block, at.name, text_value(nif, edit));
  if (edit.key == "materialClass") fail(nif, block, "key 'materialClass' is read-only");
  std::size_t slot = 0;
  if (texture_key(edit.key, slot)) {
    return write_texture_slot(nif, model, shader_texture_set(nif, model, block, source), slot,
                              text_value(nif, edit));
  }
  if (is_texture_slot_name(edit.key)) {
    fail(nif, block, "key '" + edit.key + "' names a texture slot this block does not store");
  }
  if (edit.key == "shaderFlags") return apply_flags(nif, model, block, edit, at.flags, version >= k_shader_flags_wide);
  if (edit.key == "rootMaterial") {
    require_present(nif, block, shader.has_root_material, edit.key);
    return set_name(nif, model, block, at.root_material, text_value(nif, edit));
  }
  if (edit.key == "textureClamp") {
    return write_word(nif, model, block, at.texture_clamp, whole_number(nif, edit, k_u16_max));
  }
  if (edit.key == "envMapSSR" || edit.key == "envMapWetnessSSR") {
    require_present(nif, block, shader.has_env_map && version > k_since_7c, edit.key);
    const auto offset = at.tail + (edit.key == "envMapSSR" ? 4 : 5);
    return write_byte(nif, model, block, offset, bool_value(nif, edit) ? 1 : 0);
  }
  if (edit.key == "rimlightPower") {
    // Invariant: Backlight Power is stored only behind a FLT_MAX rimlight, so a rimlight edit on either side of that placeholder moves every field after it.
    const auto wanted = number_value(nif, edit);
    if (wanted == std::numeric_limits<float>::max()) {
      fail(nif, block, "key 'rimlightPower' cannot be set to the FLT_MAX placeholder, which adds a Backlight Power field the block does not store");
    }
    if (shader.rimlight_power == std::numeric_limits<float>::max()) {
      fail(nif, block, "key 'rimlightPower' reads the FLT_MAX placeholder, so writing it would move Backlight Power and every field after it");
    }
    return write_floats(nif, model, block, at.rimlight_power, {wanted});
  }
  if (edit.key == "backlightPower") require_present(nif, block, shader.has_backlight, edit.key);
  if (edit.key == "grayscaleToPaletteScale") {
    require_present(nif, block, shader.has_grayscale_to_palette_scale, edit.key);
  }
  if (edit.key == "fresnelPower") require_present(nif, block, version >= k_fresnel_since, edit.key);
  if (edit.key.starts_with("wetness")) require_present(nif, block, shader.has_wetness, edit.key);
  if (edit.key == "uvOffset" || edit.key == "uvScale") require_present(nif, block, at.uv != 0, edit.key);
  if (edit.key == "envMapScale") require_present(nif, block, shader.has_env_map, edit.key);
  if (edit.key == "skinTintColor" || edit.key == "skinTintAlpha") {
    require_present(nif, block, shader.has_skin_tint, edit.key);
  }
  if (edit.key == "hairTintColor") require_present(nif, block, shader.has_hair_tint, edit.key);
  if (edit.key == "sparkleParameters" || edit.key == "sparkleW") {
    require_present(nif, block, shader.has_snow, edit.key);
  }
  if (edit.key == "parallaxMaxPasses" || edit.key == "parallaxScale") {
    require_present(nif, block, shader.has_parallax_occ, edit.key);
  }
  if (edit.key.starts_with("multiLayer")) {
    require_present(nif, block, shader.has_multi_layer_parallax, edit.key);
  }
  if (edit.key == "eyeCubemapScale" || edit.key == "leftEyeCenter" || edit.key == "rightEyeCenter") {
    require_present(nif, block, shader.has_eye, edit.key);
  }
  if (apply_scalar_refs(nif, model, block, edit,
                        {{"uvOffset", at.uv, 2},
                         {"uvScale", at.uv + 8, 2},
                         {"emissiveColor", at.emissive_color, 3},
                         {"emissiveMultiple", at.emissive_multiple, 1},
                         {"alpha", at.alpha, 1},
                         {"refractionStrength", at.refraction_strength, 1},
                         {"smoothness", at.smoothness, 1},
                         {"glossiness", at.smoothness, 1},
                         {"specularColor", at.specular_color, 3},
                         {"specularStrength", at.specular_strength, 1},
                         {"subsurfaceRolloff", at.subsurface_rolloff, 1},
                         {"backlightPower", at.backlight_power, 1},
                         {"grayscaleToPaletteScale", at.grayscale_to_palette_scale, 1},
                         {"fresnelPower", at.fresnel_power, 1},
                         {"wetnessSpecScale", at.wetness, 1},
                         {"wetnessSpecPower", at.wetness + 4, 1},
                         {"wetnessMinVar", at.wetness + 8, 1},
                         {"wetnessEnvMapScale", at.wetness + 12, 1},
                         {"wetnessFresnelPower", at.wetness + 16, 1},
                         {"wetnessMetalness", at.wetness + 20, 1},
                         {"envMapScale", at.tail, 1},
                         {"skinTintColor", at.tail, 3},
                         {"skinTintAlpha", at.tail + 12, 1},
                         {"hairTintColor", at.tail, 3},
                         {"sparkleParameters", at.tail, 3},
                         {"sparkleW", at.tail + 12, 1},
                         {"parallaxMaxPasses", at.tail, 1},
                         {"parallaxScale", at.tail + 4, 1},
                         {"multiLayerThickness", at.tail, 1},
                         {"multiLayerRefractionScale", at.tail + 4, 1},
                         {"multiLayerInnerScale", at.tail + 8, 2},
                         {"multiLayerEnvMapStrength", at.tail + 16, 1},
                         {"eyeCubemapScale", at.tail, 1},
                         {"leftEyeCenter", at.tail + 4, 3},
                         {"rightEyeCenter", at.tail + 16, 3}})) {
    return;
  }
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

void apply_effect_edit(const NifFile& nif, Model& model, std::uint32_t block, std::uint32_t source,
                       const Edit& edit) {
  const auto shader = read_effect_shader(nif, source);
  const auto& at = shader.offsets;
  require_present(nif, block, at.end != 0, edit.key);
  const auto version = nif.bs_version();
  const bool extra_textures = version >= k_extra_textures_since;
  if (edit.key == "name") return set_name(nif, model, block, at.name, text_value(nif, edit));
  std::size_t slot = 0;
  if (texture_key(edit.key, slot)) {
    require_present(nif, block, slot < shader.textures.size(), edit.key);
    return apply_texture_path(nif, model, block, shader.texture_at[slot], edit, true);
  }
  if (is_texture_slot_name(edit.key)) {
    fail(nif, block, "key '" + edit.key + "' names a texture slot this block does not store");
  }
  if (edit.key == "shaderFlags") return apply_flags(nif, model, block, edit, at.flags, version >= k_shader_flags_wide);
  if (edit.key == "sourceTexture") return apply_texture_path(nif, model, block, at.source_texture, edit, true);
  if (edit.key == "greyscaleTexture") {
    return apply_texture_path(nif, model, block, shader.texture_at[1], edit, true);
  }
  if (edit.key == "envMapTexture") {
    return apply_texture_path(nif, model, block, shader.texture_at[2], edit, extra_textures);
  }
  if (edit.key == "normalTexture") {
    return apply_texture_path(nif, model, block, shader.texture_at[3], edit, extra_textures);
  }
  if (edit.key == "envMaskTexture") {
    return apply_texture_path(nif, model, block, shader.texture_at[4], edit, extra_textures);
  }
  if (edit.key == "textureClamp" || edit.key == "lightingInfluence" || edit.key == "envMapMinLOD") {
    const auto shift = edit.key == "textureClamp" ? 0 : (edit.key == "lightingInfluence" ? 1 : 2);
    return write_byte(nif, model, block, at.clamp_bytes + shift,
                      static_cast<std::uint8_t>(whole_number(nif, edit, k_byte_max)));
  }
  if (edit.key == "envMapScale") require_present(nif, block, extra_textures, edit.key);
  if (edit.key == "uvOffset" || edit.key == "uvScale") require_present(nif, block, at.uv != 0, edit.key);
  if (apply_scalar_refs(nif, model, block, edit,
                        {{"uvOffset", at.uv, 2},
                         {"uvScale", at.uv + 8, 2},
                         {"falloffStartAngle", at.falloff, 1},
                         {"falloffStopAngle", at.falloff + 4, 1},
                         {"falloffStartOpacity", at.falloff + 8, 1},
                         {"falloffStopOpacity", at.falloff + 12, 1},
                         {"baseColor", at.base_color, 3},
                         {"baseColorAlpha", at.base_color + 12, 1},
                         {"baseColorScale", at.base_color_scale, 1},
                         {"softFalloffDepth", at.soft_falloff_depth, 1},
                         {"envMapScale", at.env_map_scale, 1}})) {
    return;
  }
  fail(nif, block, "key '" + edit.key + "' is not supported by the NIF writer");
}

}  // namespace fo4::nif::edit_detail
