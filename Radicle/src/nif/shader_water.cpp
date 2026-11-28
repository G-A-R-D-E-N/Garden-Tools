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

constexpr std::string_view k_water_type = "BSWaterShaderProperty";
constexpr std::string_view k_sky_type = "BSSkyShaderProperty";
constexpr std::uint32_t k_shader_flags_wide = 0x45;
constexpr std::uint32_t k_uv_since = 0x2b;

// Requires the same BSShaderProperty base the lighting reader uses: two flag words, then the UV pair.
void read_base(binary::Cursor& cursor, std::uint32_t version, ShaderFlags& flags, float (&uv_offset)[2],
               float (&uv_scale)[2], std::size_t& flags_at, std::size_t& uv_at) {
  flags_at = cursor.position();
  flags.word1 = cursor.read_u32();
  if (version >= k_shader_flags_wide) flags.word2 = cursor.read_u32();
  if (version > k_uv_since) {
    uv_at = cursor.position();
    for (float& value : uv_offset) value = cursor.read_f32();
    for (float& value : uv_scale) value = cursor.read_f32();
  }
}

}  // namespace

WaterShader read_water_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  WaterShader shader;
  shader.block = block;
  if (info.type != k_water_type) return shader;

  auto cursor = detail::block_cursor(nif, block);
  auto& at = shader.offsets;
  const auto version = nif.bs_version();

  at.name = cursor.position();
  shader.name = detail::read_net_prefix(cursor, nif).name;
  read_base(cursor, version, shader.flags, shader.uv_offset, shader.uv_scale, at.flags, at.uv);

  at.water_flags = cursor.position();
  shader.water_flags = cursor.read_u32();
  at.end = cursor.position();
  return shader;
}

SkyShader read_sky_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  SkyShader shader;
  shader.block = block;
  if (info.type != k_sky_type) return shader;

  auto cursor = detail::block_cursor(nif, block);
  auto& at = shader.offsets;
  const auto version = nif.bs_version();

  at.name = cursor.position();
  shader.name = detail::read_net_prefix(cursor, nif).name;
  read_base(cursor, version, shader.flags, shader.uv_offset, shader.uv_scale, at.flags, at.uv);

  at.filename = cursor.position();
  shader.filename = detail::read_sized_string(cursor);
  at.sky_object_type = cursor.position();
  shader.sky_object_type = cursor.read_u32();
  at.end = cursor.position();
  return shader;
}

}  // namespace fo4::nif
