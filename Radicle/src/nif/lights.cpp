#include "fo4/nif/lights.hpp"

#include "nif_internal.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace fo4::nif {
namespace {

constexpr std::array<std::string_view, 4> k_light_types{"NiPointLight", "NiAmbientLight", "NiDirectionalLight",
                                                        "NiSpotLight"};

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
    lights.push_back(std::move(light));
  }
  return lights;
}

}  // namespace fo4::nif
