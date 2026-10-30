#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: word 2 holds bits 32-63, which is where the extract's GetFlagString table stops (below 0x40).
struct ShaderFlags {
  std::uint32_t word1{0};
  std::uint32_t word2{0};

  std::uint64_t value() const noexcept {
    return static_cast<std::uint64_t>(word1) | (static_cast<std::uint64_t>(word2) << 32U);
  }
};

std::size_t shader_flag_count() noexcept;
// Requires bit < shader_flag_count(); the name is the nif.xml bit name.
const char* shader_flag_name(std::size_t bit) noexcept;
std::vector<std::string> shader_flag_names(const ShaderFlags& flags);

struct WetnessParams {
  float spec_scale{0.0f};
  float spec_power{0.0f};
  float min_var{0.0f};
  float env_map_scale{0.0f};
  float fresnel_power{0.0f};
  float metalness{0.0f};
};

// Offset of every editable field in read order; the writer must write the fields it reads.
struct LightingFieldOffsets {
  std::size_t shader_type{0};
  std::size_t name{0};
  std::size_t flags{0};
  std::size_t uv{0};
  std::size_t texture_set{0};
  std::size_t emissive_color{0};
  std::size_t emissive_multiple{0};
  std::size_t root_material{0};
  std::size_t texture_clamp{0};
  std::size_t alpha{0};
  std::size_t refraction_strength{0};
  std::size_t smoothness{0};
  std::size_t specular_color{0};
  std::size_t specular_strength{0};
  std::size_t subsurface_rolloff{0};
  std::size_t rimlight_power{0};
  std::size_t backlight_power{0};
  std::size_t grayscale_to_palette_scale{0};
  std::size_t fresnel_power{0};
  std::size_t wetness{0};
  std::size_t tail{0};
  std::size_t end{0};
};

struct EffectFieldOffsets {
  std::size_t name{0};
  std::size_t flags{0};
  std::size_t uv{0};
  std::size_t source_texture{0};
  std::size_t clamp_bytes{0};
  std::size_t falloff{0};
  std::size_t base_color{0};
  std::size_t base_color_scale{0};
  std::size_t soft_falloff_depth{0};
  std::size_t env_map_scale{0};
  std::size_t end{0};
};

// Invariant: the ids are nif.xml's shader-type conditions and the names are the extract's material classes.
enum class LightingMaterialType : std::uint32_t {
  Default = 0,
  EnvMap = 1,
  SkinTint = 5,
  HairTint = 6,
  ParallaxOcc = 7,
  MultiLayerParallax = 11,
  Snow = 14,
  Eye = 16,
};

struct LightingShader {
  std::uint32_t block{0};
  std::uint32_t shader_type{0};
  std::string name;
  ShaderFlags flags;
  float uv_offset[2]{};
  float uv_scale[2]{};
  std::int32_t texture_set_ref{-1};
  std::vector<std::string> textures;
  float emissive_color[3]{};
  float emissive_multiple{0.0f};
  std::string root_material;
  bool has_root_material{false};
  std::uint32_t texture_clamp{0};
  float alpha{0.0f};
  float refraction_strength{0.0f};
  float smoothness{0.0f};
  float specular_color[3]{};
  float specular_strength{0.0f};
  float subsurface_rolloff{0.0f};
  float rimlight_power{0.0f};
  float backlight_power{0.0f};
  bool has_backlight{false};
  float grayscale_to_palette_scale{0.0f};
  bool has_grayscale_to_palette_scale{false};
  float fresnel_power{0.0f};
  WetnessParams wetness;
  bool has_wetness{false};

  // Invariant: per-type data is present only for the shader type that stores it.
  float env_map_scale{0.0f};
  std::uint8_t env_map_ssr{0};
  std::uint8_t env_map_wetness_ssr{0};
  bool has_env_map{false};
  float skin_tint[4]{};
  bool has_skin_tint{false};
  float hair_tint[3]{};
  bool has_hair_tint{false};
  float snow[4]{};
  bool has_snow{false};
  float parallax_occ[2]{};
  bool has_parallax_occ{false};
  float multi_layer_parallax[5]{};
  bool has_multi_layer_parallax{false};
  float eye_cubemap_scale{0.0f};
  float eye_left[3]{};
  float eye_right[3]{};
  bool has_eye{false};

  std::size_t tail_bytes{0};
  LightingFieldOffsets offsets;
};

struct EffectShader {
  std::uint32_t block{0};
  std::string name;
  ShaderFlags flags;
  float uv_offset[2]{};
  float uv_scale[2]{};
  std::string source_texture;
  std::uint8_t texture_clamp{0};
  std::uint8_t lighting_influence{0};
  std::uint8_t env_map_min_lod{0};
  std::uint8_t unused{0};
  float falloff_start_angle{0.0f};
  float falloff_stop_angle{0.0f};
  float falloff_start_opacity{0.0f};
  float falloff_stop_opacity{0.0f};
  float base_color[4]{};
  float base_color_scale{0.0f};
  float soft_falloff_depth{0.0f};
  std::vector<std::string> textures;
  float env_map_scale{0.0f};
  // Offset of the u32 length prefix of each texture slot, 0 when the block does not store it.
  std::size_t texture_at[5]{};
  EffectFieldOffsets offsets;
};

// Invariant: the blend values are the NiAlphaProperty enum, 0 one through 10 source alpha saturate.
enum class AlphaBlendMode : std::uint8_t {
  One = 0,
  Zero = 1,
  SourceColor = 2,
  InverseSourceColor = 3,
  DestinationColor = 4,
  InverseDestinationColor = 5,
  SourceAlpha = 6,
  InverseSourceAlpha = 7,
  DestinationAlpha = 8,
  InverseDestinationAlpha = 9,
  SourceAlphaSaturate = 10,
};

// Invariant: the test values are the NiAlphaProperty enum, 0 always through 7 never.
enum class AlphaTestFunction : std::uint8_t {
  Always = 0,
  Less = 1,
  Equal = 2,
  LessEqual = 3,
  Greater = 4,
  NotEqual = 5,
  GreaterEqual = 6,
  Never = 7,
};

// Invariant: 0xEC is the flags word the engine writes for a new alpha property.
constexpr std::uint16_t k_alpha_default_flags = 0xEC;

const char* alpha_blend_name(AlphaBlendMode mode) noexcept;
const char* alpha_test_name(AlphaTestFunction function) noexcept;

struct AlphaProperty {
  std::uint32_t block{0};
  std::uint16_t flags{0};
  std::uint8_t threshold{0};
  // Invariant: the five decoded fields are flags bits 0, 1-4, 5-8, 9 and 10-12, so no caller shifts the word itself.
  bool blend_enabled{false};
  AlphaBlendMode source_blend{AlphaBlendMode::One};
  AlphaBlendMode destination_blend{AlphaBlendMode::One};
  bool test_enabled{false};
  AlphaTestFunction test_function{AlphaTestFunction::Always};
  std::size_t flags_at{0};
  std::size_t threshold_at{0};
  std::size_t end{0};
};

LightingShader read_lighting_shader(const NifFile& nif, std::uint32_t block);
EffectShader read_effect_shader(const NifFile& nif, std::uint32_t block);
AlphaProperty read_alpha(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif
