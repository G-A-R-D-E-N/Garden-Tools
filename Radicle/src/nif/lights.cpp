#include "fo4/nif/lights.hpp"

#include "nif_internal.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace fo4::nif {
namespace {

constexpr std::array<std::string_view, 4> k_light_types{"NiPointLight", "NiAmbientLight", "NiDirectionalLight",
                                                        "NiSpotLight"};

constexpr std::size_t k_collision_ref_bytes = 4;
constexpr std::size_t k_light_colour_bytes = 40;
constexpr std::size_t k_light_attenuation_bytes = 12;

bool has_attenuation_type(std::string_view type) { return type == "NiPointLight" || type == "NiSpotLight"; }

std::array<float, 3> read_triple(binary::Cursor& cursor) {
  std::array<float, 3> out{};
  for (float& value : out) value = cursor.read_f32();
  return out;
}

bool is_light_type(std::string_view type) {
  for (const auto candidate : k_light_types) {
    if (candidate == type) return true;
  }
  return false;
}

}  // namespace

std::vector<Light> read_lights(const NifFile& nif) {
  std::vector<Light> lights;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_light_type(nif.blocks()[block].type)) continue;
    auto cursor = detail::block_cursor(nif, block);
    const auto header = detail::read_av_header(cursor, nif);
    Light light;
    light.block = block;
    light.type = nif.blocks()[block].type;
    light.name = header.name;
    light.parent_block = nif.parent(block);
    light.flags = header.flags;
    light.local = header.transform;
    if (cursor.remaining() >= k_collision_ref_bytes + k_light_colour_bytes) {
      cursor.read_i32();  // Constraint: the collision object reference follows the transform; read_av_header stops before it.
      light.has_colors = true;
      light.dimmer = cursor.read_f32();
      light.ambient_color = read_triple(cursor);
      light.diffuse_color = read_triple(cursor);
      light.specular_or_radius = read_triple(cursor);
      if (has_attenuation_type(light.type) && cursor.remaining() >= k_light_attenuation_bytes) {
        light.has_attenuation = true;
        light.attenuation = read_triple(cursor);
      }
    }
    lights.push_back(std::move(light));
  }
  return lights;
}

}  // namespace fo4::nif
