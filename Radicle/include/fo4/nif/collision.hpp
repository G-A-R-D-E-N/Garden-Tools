#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Constraint: bhkPhysicsSystem and bhkRagdollSystem are the two blocks that store a collision blob.
inline constexpr std::string_view k_physics_system_type = "bhkPhysicsSystem";
inline constexpr std::string_view k_ragdoll_system_type = "bhkRagdollSystem";

inline bool is_collision_system_type(std::string_view type) {
  return type == k_physics_system_type || type == k_ragdoll_system_type;
}

// Requires a bhkPhysicsSystem or bhkRagdollSystem block; the bytes are the ByteArray payload undecoded.
struct CollisionBlob {
  std::uint32_t block{0};
  std::string type;
  std::vector<std::byte> bytes;
  // Constraint: owner, flags, system_ref and target come from the bhkNPCollisionObject that names this system.
  // Invariant: every one of them stays at -1/0 when no collision object names the system, so absence is not a guess.
  std::int32_t owner{-1};
  std::uint16_t flags{0};
  // The owning object's Data reference. It equals block whenever owner >= 0, because Data must name a system block.
  std::int32_t system_ref{-1};
  std::uint32_t body_id{0};
  std::int32_t target{-1};
  std::int32_t target_node{-1};
};

// Constraint: bhkNPCollisionObject stores Target, Flags, Data and Body ID in that order, 14 bytes in all.
struct CollisionObject {
  std::uint32_t block{0};
  std::int32_t target{-1};
  std::uint16_t flags{0};
  std::int32_t system{-1};
  std::uint32_t body_id{0};
};

std::vector<CollisionBlob> read_collision_blobs(const NifFile& nif);
// Requires a bhkNPCollisionObject block; the references are the stored values and may be out of range.
CollisionObject read_collision_object(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif
