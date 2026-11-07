#include "fo4/nif/shader.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::nif {
namespace {

constexpr std::string_view k_effect_type = "BSEffectShaderProperty";
constexpr std::uint32_t k_shader_flags_wide = 0x45;
constexpr std::uint32_t k_uv_since = 0x2b;
constexpr std::uint32_t k_extra_textures_since = 130;
constexpr std::uint32_t k_max_texture_path = 1024 * 1024;
constexpr std::size_t k_texture_slots = 5;
constexpr std::size_t k_greyscale_slot = 1;
constexpr std::size_t k_env_map_slot = 2;
constexpr std::size_t k_normal_slot = 3;
constexpr std::size_t k_env_mask_slot = 4;

std::string read_sized_string(binary::Cursor& cursor) {
  const auto length = cursor.read_u32();
  if (length > k_max_texture_path) {
    throw parse_error(cursor.absolute_offset(), "texture path exceeds 1 MiB");
  }
  const auto text = cursor.read_bytes(length);
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string read_texture_slot(binary::Cursor& cursor, std::size_t& at) {
  at = cursor.position();
  return read_sized_string(cursor);
}

}  // namespace

EffectShader read_effect_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  EffectShader shader;
  shader.block = block;
  if (info.type != k_effect_type) return shader;

  auto cursor = detail::block_cursor(nif, block);
  auto& at = shader.offsets;
  const auto version = nif.bs_version();

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

  shader.textures.resize(k_texture_slots);
  at.source_texture = cursor.position();
  shader.source_texture = read_sized_string(cursor);
  shader.textures[0] = shader.source_texture;
  shader.texture_at[0] = at.source_texture;
  at.clamp_bytes = cursor.position();
  shader.texture_clamp = cursor.read_u8();
  shader.lighting_influence = cursor.read_u8();
  shader.env_map_min_lod = cursor.read_u8();
  shader.unused = cursor.read_u8();
  at.falloff = cursor.position();
  shader.falloff_start_angle = cursor.read_f32();
  shader.falloff_stop_angle = cursor.read_f32();
  shader.falloff_start_opacity = cursor.read_f32();
  shader.falloff_stop_opacity = cursor.read_f32();
  at.base_color = cursor.position();
  for (float& value : shader.base_color) value = cursor.read_f32();
  at.base_color_scale = cursor.position();
  shader.base_color_scale = cursor.read_f32();
  at.soft_falloff_depth = cursor.position();
  shader.soft_falloff_depth = cursor.read_f32();

  // Invariant: the stored paths are source, greyscale, env map, normal and env mask, in that order.
  shader.textures[k_greyscale_slot] = read_texture_slot(cursor, shader.texture_at[k_greyscale_slot]);
  if (version >= k_extra_textures_since) {
    shader.textures[k_env_map_slot] = read_texture_slot(cursor, shader.texture_at[k_env_map_slot]);
    shader.textures[k_normal_slot] = read_texture_slot(cursor, shader.texture_at[k_normal_slot]);
    shader.textures[k_env_mask_slot] = read_texture_slot(cursor, shader.texture_at[k_env_mask_slot]);
    at.env_map_scale = cursor.position();
    shader.env_map_scale = cursor.read_f32();
  }
  at.end = cursor.position();
  return shader;
}

}  // namespace fo4::nif
