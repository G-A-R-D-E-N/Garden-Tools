#pragma once

#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: only the NiAVObject prefix is decoded; colour, dimmer and attenuation are not (the layout is unverified, see code-notes).
struct Light {
  std::uint32_t block{0};
  std::string type;
  std::string name;
  std::int32_t parent_block{-1};
  std::uint32_t flags{0};
  Transform local;
};

// Requires a parsed NIF; light blocks are NiPointLight, NiAmbientLight, NiDirectionalLight and NiSpotLight, in block order.
std::vector<Light> read_lights(const NifFile& nif);

}  // namespace fo4::nif
