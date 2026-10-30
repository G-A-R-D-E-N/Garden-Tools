#include "fo4/nif/collision.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::string_view k_physics_system = "bhkPhysicsSystem";
constexpr std::string_view k_ragdoll_system = "bhkRagdollSystem";
constexpr std::string_view k_collision_object = "bhkNPCollisionObject";
constexpr std::size_t k_collision_object_fields = 14;

bool is_collision_system(std::string_view type) {
  return type == k_physics_system || type == k_ragdoll_system;
}

CollisionBlob read_blob(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  if (info.size < 4) {
    throw parse_error(info.offset, "collision block is shorter than its length prefix");
  }
  auto cursor = detail::block_cursor(nif, block);
  const auto size = cursor.read_u32();
  if (size > cursor.remaining()) {
    throw parse_error(info.offset, "collision blob length runs past the end of its block");
  }
  const auto payload = cursor.read_bytes(size);
  CollisionBlob blob;
  blob.block = block;
  blob.type = info.type;
  blob.bytes.assign(payload.begin(), payload.end());
  return blob;
}

std::int32_t block_ref(const NifFile& nif, std::int32_t value) {
  return value >= 0 && static_cast<std::size_t>(value) < nif.blocks().size() ? value : -1;
}

}  // namespace

CollisionObject read_collision_object(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  if (info.size < k_collision_object_fields) {
    throw parse_error(info.offset, "collision object is shorter than its fields");
  }
  auto cursor = detail::block_cursor(nif, block);
  CollisionObject object;
  object.block = block;
  object.target = cursor.read_i32();
  object.flags = cursor.read_u16();
  object.system = cursor.read_i32();
  object.body_id = cursor.read_u32();
  return object;
}

std::vector<CollisionBlob> read_collision_blobs(const NifFile& nif) {
  std::vector<CollisionBlob> blobs;
  std::vector<std::int32_t> slot_of_block(nif.blocks().size(), -1);
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_collision_system(nif.blocks()[block].type)) continue;
    slot_of_block[block] = static_cast<std::int32_t>(blobs.size());
    blobs.push_back(read_blob(nif, static_cast<std::uint32_t>(block)));
  }

  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != k_collision_object) continue;
    const auto owner = read_collision_object(nif, static_cast<std::uint32_t>(block));
    const auto system = block_ref(nif, owner.system);
    if (system < 0) continue;
    const auto slot = slot_of_block[static_cast<std::size_t>(system)];
    if (slot < 0) continue;
    auto& blob = blobs[static_cast<std::size_t>(slot)];
    // Invariant: the first object naming a system owns it, so two owners stay deterministic by block order.
    if (blob.owner >= 0) continue;
    blob.owner = static_cast<std::int32_t>(block);
    blob.flags = owner.flags;
    blob.system_ref = system;
    blob.body_id = owner.body_id;
    blob.target = block_ref(nif, owner.target);
    if (blob.target >= 0) blob.target_node = nif.node_for_block(static_cast<std::uint32_t>(blob.target));
  }

  return blobs;
}

}  // namespace fo4::nif
