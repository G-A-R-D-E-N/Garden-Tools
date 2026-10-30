#include "fo4/nif/shader.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace fo4::nif {
namespace {

constexpr std::string_view k_lighting_type = "BSLightingShaderProperty";
constexpr std::uint32_t k_shader_flags_wide = 0x45;
constexpr std::uint32_t k_uv_since = 0x2b;
// Invariant: Root Material and the two env-map bytes both start above stream version 0x7c.
constexpr std::uint32_t k_since_7c = 0x7c;
constexpr std::uint32_t k_grayscale_since = 0x79;
constexpr std::uint32_t k_wetness_since = 0x7b;
constexpr std::uint32_t k_fresnel_since = 130;
constexpr std::uint32_t k_multi_layer_fifth_since = 0x4a;

void read_triple(binary::Cursor& cursor, float (&out)[3]) {
  for (float& value : out) value = cursor.read_f32();
}

void read_quad(binary::Cursor& cursor, float (&out)[4]) {
  for (float& value : out) value = cursor.read_f32();
}

// Requires the block to store the type's tail right after the base material fields.
void read_tail(binary::Cursor& cursor, std::uint32_t version, LightingShader& shader) {
  const auto start = cursor.position();
  switch (static_cast<LightingMaterialType>(shader.shader_type)) {
    case LightingMaterialType::EnvMap:
      shader.has_env_map = true;
      shader.env_map_scale = cursor.read_f32();
      if (version > k_since_7c) {
        shader.env_map_ssr = cursor.read_u8();
        shader.env_map_wetness_ssr = cursor.read_u8();
      }
      break;
    case LightingMaterialType::SkinTint:
      shader.has_skin_tint = true;
      read_quad(cursor, shader.skin_tint);
      break;
    case LightingMaterialType::HairTint:
      shader.has_hair_tint = true;
      read_triple(cursor, shader.hair_tint);
      break;
    case LightingMaterialType::ParallaxOcc:
      shader.has_parallax_occ = true;
      for (float& value : shader.parallax_occ) value = cursor.read_f32();
      break;
    case LightingMaterialType::MultiLayerParallax:
      shader.has_multi_layer_parallax = true;
      for (std::size_t i = 0; i < 4; ++i) shader.multi_layer_parallax[i] = cursor.read_f32();
      shader.multi_layer_parallax[4] = version > k_multi_layer_fifth_since ? cursor.read_f32() : 0.0f;
      break;
    case LightingMaterialType::Snow:
      shader.has_snow = true;
      read_quad(cursor, shader.snow);
      break;
    case LightingMaterialType::Eye:
      shader.has_eye = true;
      shader.eye_cubemap_scale = cursor.read_f32();
      read_triple(cursor, shader.eye_left);
      read_triple(cursor, shader.eye_right);
      break;
    default:
      break;
  }
  shader.tail_bytes = cursor.position() - start;
}

}  // namespace

LightingShader read_lighting_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  LightingShader shader;
  shader.block = block;
  if (info.type != k_lighting_type) return shader;

  auto cursor = detail::block_cursor(nif, block);
  auto& at = shader.offsets;
  const auto version = nif.bs_version();

  at.shader_type = cursor.position();
  shader.shader_type = cursor.read_u32();
  at.name = cursor.position();
  shader.name = detail::read_net_prefix(cursor, nif).name;

  at.flags = cursor.position();
  shader.flags.word1 = cursor.read_u32();
  if (version >= k_shader_flags_wide) shader.flags.word2 = cursor.read_u32();
  if (version > k_uv_since) {
    at.uv = cursor.position();
    for (float& value : shader.uv_offset) value = cursor.read_f32();
    for (float& value : shader.uv_scale) value = cursor.read_f32();
  }

  at.texture_set = cursor.position();
  shader.texture_set_ref = cursor.read_i32();
  if (shader.texture_set_ref >= 0) {
    if (static_cast<std::size_t>(shader.texture_set_ref) >= nif.blocks().size()) {
      throw parse_error(cursor.absolute_offset(), "texture set reference is out of range");
    }
    shader.textures = detail::read_texture_set(nif, static_cast<std::uint32_t>(shader.texture_set_ref));
  }

  at.emissive_color = cursor.position();
  read_triple(cursor, shader.emissive_color);
  at.emissive_multiple = cursor.position();
  shader.emissive_multiple = cursor.read_f32();
  if (version > k_since_7c) {
    at.root_material = cursor.position();
    shader.root_material = detail::read_string_ref(cursor, nif);
    shader.has_root_material = true;
  }
  at.texture_clamp = cursor.position();
  shader.texture_clamp = cursor.read_u32();
  at.alpha = cursor.position();
  shader.alpha = cursor.read_f32();
  at.refraction_strength = cursor.position();
  shader.refraction_strength = cursor.read_f32();
  at.smoothness = cursor.position();
  shader.smoothness = cursor.read_f32();
  at.specular_color = cursor.position();
  read_triple(cursor, shader.specular_color);
  at.specular_strength = cursor.position();
  shader.specular_strength = cursor.read_f32();
  at.subsurface_rolloff = cursor.position();
  shader.subsurface_rolloff = cursor.read_f32();
  at.rimlight_power = cursor.position();
  shader.rimlight_power = cursor.read_f32();
  // Invariant: nif.xml writes Backlight Power only when Rimlight Power is the FLT_MAX placeholder.
  if (shader.rimlight_power == std::numeric_limits<float>::max()) {
    at.backlight_power = cursor.position();
    shader.backlight_power = cursor.read_f32();
    shader.has_backlight = true;
  }
  if (version > k_grayscale_since) {
    at.grayscale_to_palette_scale = cursor.position();
    shader.grayscale_to_palette_scale = cursor.read_f32();
    shader.has_grayscale_to_palette_scale = true;
  }
  if (version >= k_fresnel_since) {
    at.fresnel_power = cursor.position();
    shader.fresnel_power = cursor.read_f32();
  }
  if (version > k_wetness_since) {
    at.wetness = cursor.position();
    shader.wetness.spec_scale = cursor.read_f32();
    shader.wetness.spec_power = cursor.read_f32();
    shader.wetness.min_var = cursor.read_f32();
    shader.wetness.env_map_scale = cursor.read_f32();
    shader.wetness.fresnel_power = cursor.read_f32();
    shader.wetness.metalness = cursor.read_f32();
    shader.has_wetness = true;
  }

  at.tail = cursor.position();
  read_tail(cursor, version, shader);
  at.end = cursor.position();
  return shader;
}

}  // namespace fo4::nif
