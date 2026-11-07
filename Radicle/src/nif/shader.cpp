#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

// Invariant: bit n is option n of nif.xml F4SF1 then F4SF2, the same order as commonlibf4's EShaderPropertyFlag.
constexpr std::array<std::string_view, 64> k_shader_flag_names{
    "Specular", "Skinned", "Temp_Refraction", "Vertex_Alpha",
    "GreyscaleToPalette_Color", "GreyscaleToPalette_Alpha", "Use_Falloff", "Environment_Mapping",
    "RGB_Falloff", "Cast_Shadows", "Face", "UI_Mask_Rects",
    "Model_Space_Normals", "Non_Projective_Shadows", "Landscape", "Refraction",
    "Fire_Refraction", "Eye_Environment_Mapping", "Hair", "Screendoor_Alpha_Fade",
    "Localmap_Hide_Secret", "Skin_Tint", "Own_Emit", "Projected_UV",
    "Multiple_Textures", "Tessellate", "Decal", "Dynamic_Decal",
    "Character_Lighting", "External_Emittance", "Soft_Effect", "ZBuffer_Test",
    "ZBuffer_Write", "LOD_Landscape", "LOD_Objects", "No_Fade",
    "Double_Sided", "Vertex_Colors", "Glow_Map", "Transform_Changed",
    "Dismemberment_Meatcuff", "Tint", "Grass_Vertex_Lighting", "Grass_Uniform_Scale",
    "Grass_Fit_Slope", "Grass_Billboard", "No_LOD_Land_Blend", "Dismemberment",
    "Wireframe", "Weapon_Blood", "Hide_On_Local_Map", "Premult_Alpha",
    "VATS_Target", "Anisotropic_Lighting", "Skew_Specular_Alpha", "Menu_Screen",
    "Multi_Layer_Parallax", "Alpha_Test", "Gradient_Remap", "VATS_Target_Draw_All",
    "Pipboy_Screen", "Tree_Anim", "Effect_Lighting", "Refraction_Writes_Depth",
};

constexpr std::uint16_t k_blend_enable_bit = 1U << 0U;
constexpr std::uint16_t k_source_blend_shift = 1;
constexpr std::uint16_t k_destination_blend_shift = 5;
constexpr std::uint16_t k_test_enable_bit = 1U << 9U;
constexpr std::uint16_t k_test_function_shift = 10;
constexpr std::uint16_t k_blend_nibble = 0xFU;
constexpr std::uint16_t k_test_function_mask = 0x7U;

constexpr std::array<std::string_view, 11> k_blend_names{
    "One", "Zero", "SourceColor", "InverseSourceColor", "DestinationColor", "InverseDestinationColor",
    "SourceAlpha", "InverseSourceAlpha", "DestinationAlpha", "InverseDestinationAlpha", "SourceAlphaSaturate",
};

constexpr std::array<std::string_view, 8> k_test_names{
    "Always", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Never",
};

}  // namespace

Shader read_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  Shader shader;
  shader.block = block;
  if (info.type != "BSLightingShaderProperty") return shader;

  auto cursor = detail::block_cursor(nif, block);
  shader.type = cursor.read_u32();
  shader.name = detail::read_string_ref(cursor, nif);
  const auto extra_count = cursor.read_u32();
  if (static_cast<std::size_t>(extra_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "extra-data list runs past the end of its block");
  }
  cursor.read_bytes(static_cast<std::size_t>(extra_count) * 4);
  cursor.read_i32();
  cursor.read_bytes(8);
  cursor.read_bytes(16);
  shader.texture_set_ref = cursor.read_i32();
  if (shader.texture_set_ref >= 0) {
    if (static_cast<std::size_t>(shader.texture_set_ref) >= nif.blocks().size()) {
      throw parse_error(cursor.absolute_offset(), "texture set reference is out of range");
    }
    shader.textures = detail::read_texture_set(nif, static_cast<std::uint32_t>(shader.texture_set_ref));
  }
  return shader;
}

const char* alpha_blend_name(AlphaBlendMode mode) noexcept {
  const auto index = static_cast<std::size_t>(mode);
  return index < k_blend_names.size() ? k_blend_names[index].data() : "Unknown";
}

const char* alpha_test_name(AlphaTestFunction function) noexcept {
  const auto index = static_cast<std::size_t>(function);
  return index < k_test_names.size() ? k_test_names[index].data() : "Unknown";
}

std::size_t shader_flag_count() noexcept { return k_shader_flag_names.size(); }

const char* shader_flag_name(std::size_t bit) noexcept {
  return bit < k_shader_flag_names.size() ? k_shader_flag_names[bit].data() : nullptr;
}

std::vector<std::string> shader_flag_names(const ShaderFlags& flags) {
  std::vector<std::string> names;
  const auto value = flags.value();
  for (std::size_t bit = 0; bit < k_shader_flag_names.size(); ++bit) {
    if ((value >> bit & 1U) != 0) names.emplace_back(k_shader_flag_names[bit]);
  }
  return names;
}

AlphaProperty read_alpha(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  AlphaProperty alpha;
  alpha.block = block;
  if (info.type != "NiAlphaProperty") return alpha;

  auto cursor = detail::block_cursor(nif, block);
  alpha.name = detail::read_net_prefix(cursor, nif).name;
  alpha.flags_at = cursor.position();
  alpha.flags = cursor.read_u16();
  alpha.threshold_at = cursor.position();
  alpha.threshold = cursor.read_u8();
  alpha.end = cursor.position();
  const auto flags = alpha.flags;
  alpha.blend_enabled = (flags & k_blend_enable_bit) != 0;
  alpha.source_blend = static_cast<AlphaBlendMode>((flags >> k_source_blend_shift) & k_blend_nibble);
  alpha.destination_blend = static_cast<AlphaBlendMode>((flags >> k_destination_blend_shift) & k_blend_nibble);
  alpha.test_enabled = (flags & k_test_enable_bit) != 0;
  alpha.test_function = static_cast<AlphaTestFunction>((flags >> k_test_function_shift) & k_test_function_mask);
  return alpha;
}

}  // namespace fo4::nif
