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

// Invariant: bit n is EShaderPropertyFlag n (G-A-R-D-E-N/commonlibf4@22b0dbd:include/RE/B/BSShaderProperty.h), the same 64 bits as nif.xml SLSF1 and SLSF2.
constexpr std::array<std::string_view, 64> k_shader_flag_names{
    "Specular", "Skinned", "TempRefraction", "VertexAlpha",
    "GreyscaleToPaletteColor", "GreyscaleToPaletteAlpha", "Falloff", "EnvMap",
    "RGBFalloff", "CastShadows", "Face", "UIMaskRects",
    "ModelSpaceNormals", "RefractionClamp", "MultiTextureLandscape", "Refraction",
    "RefractionFalloff", "EyeReflect", "HairTint", "ScreendoorAlphaFade",
    "LocalMapClear", "FaceGenRGBTint", "OwnEmit", "ProjectedUV",
    "MultipleTextures", "Tessellate", "Decal", "DynamicDecal",
    "CharacterLight", "ExternalEmittance", "SoftEffect", "ZBufferTest",
    "ZBufferWrite", "LODLandscape", "LODObjects", "NoFade",
    "TwoSided", "VertexColors", "GlowMap", "TransformChanged",
    "DismembermentMeatCuff", "Tint", "VertexLighting", "UniformScale",
    "FitSlope", "Billboard", "LODLandBlend", "Dismemberment",
    "Wireframe", "WeaponBlood", "HideOnLocalMap", "PremultipliedAlpha",
    "VATSTarget", "AnisotropicLighting", "SkewSpecularAlpha", "MenuScreen",
    "MultiLayerParallax", "AlphaTest", "InvertedFadePattern", "VATSTargetDrawAll",
    "PipboyScreen", "TreeAnim", "EffectLighting", "RefractionWritesDepth",
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
  detail::read_net_prefix(cursor, nif);
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
