#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: spot angles are not decoded; the third colour triple holds the radius on vanilla point lights (see code-notes).
struct Light {
  std::uint32_t block{0};
  std::string type;
  std::string name;
  std::int32_t parent_block{-1};
  std::uint32_t flags{0};
  Transform local;
  bool has_colors{false};
  float dimmer{0.0f};
  std::array<float, 3> ambient_color{};
  std::array<float, 3> diffuse_color{};
  std::array<float, 3> specular_or_radius{};
  bool has_attenuation{false};
  std::array<float, 3> attenuation{};
};

// Requires a parsed NIF; light blocks are NiPointLight, NiAmbientLight, NiDirectionalLight and NiSpotLight, in block order.
std::vector<Light> read_lights(const NifFile& nif);

}  // namespace fo4::nif
