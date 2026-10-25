#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Requires a bhkPhysicsSystem or bhkRagdollSystem block; the bytes are the ByteArray payload undecoded.
struct CollisionBlob {
  std::uint32_t block{0};
  std::string type;
  std::vector<std::byte> bytes;
};

std::vector<CollisionBlob> read_collision_blobs(const NifFile& nif);

}  // namespace fo4::nif
