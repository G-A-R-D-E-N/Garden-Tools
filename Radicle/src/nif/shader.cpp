#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_texture_set_header = 4;
constexpr std::uint32_t k_max_texture_slots = 4096;

std::string read_name_index(binary::Cursor& cursor, const NifFile& nif) {
  const auto index = cursor.read_i32();
  if (index < 0) return {};
  if (static_cast<std::size_t>(index) >= nif.strings().size()) {
    throw parse_error(cursor.absolute_offset(), "string index is out of range");
  }
  return nif.strings()[static_cast<std::size_t>(index)];
}

std::vector<std::string> read_textures(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  if (info.type != "BSShaderTextureSet") return {};
  if (info.size < k_texture_set_header) {
    throw parse_error(info.offset, "texture set is shorter than its slot count");
  }
  auto cursor = detail::block_cursor(nif, block);
  const auto count = cursor.read_u32();
  if (count > k_max_texture_slots) {
    throw parse_error(cursor.absolute_offset(), "texture slot count exceeds 4,096");
  }
  if (static_cast<std::size_t>(count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "texture slot list runs past the end of its block");
  }
  std::vector<std::string> textures;
  textures.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto length = cursor.read_u32();
    if (length > cursor.remaining()) {
      throw parse_error(cursor.absolute_offset(), "texture path runs past the end of its block");
    }
    const auto text = cursor.read_bytes(length);
    textures.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
  }
  return textures;
}

}

Shader read_shader(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  Shader shader;
  shader.block = block;
  if (info.type != "BSLightingShaderProperty") return shader;

  auto cursor = detail::block_cursor(nif, block);
  shader.type = cursor.read_u32();
  shader.name = read_name_index(cursor, nif);
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
    shader.textures = read_textures(nif, static_cast<std::uint32_t>(shader.texture_set_ref));
  }
  return shader;
}

}
