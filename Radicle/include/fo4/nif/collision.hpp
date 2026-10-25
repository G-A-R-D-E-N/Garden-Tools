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
  // Constraint: owner, body and target come from the bhkNPCollisionObject that names this system.
  // Invariant: all four stay at -1/0 when no collision object names the system, so absence is not a guess.
  std::int32_t owner{-1};
  std::uint32_t body_id{0};
  std::int32_t target{-1};
  std::int32_t target_node{-1};
};

std::vector<CollisionBlob> read_collision_blobs(const NifFile& nif);

}  // namespace fo4::nif
