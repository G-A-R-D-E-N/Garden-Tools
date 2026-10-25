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

}  // namespace

std::vector<CollisionBlob> read_collision_blobs(const NifFile& nif) {
  std::vector<CollisionBlob> blobs;
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_collision_system(nif.blocks()[block].type)) continue;
    blobs.push_back(read_blob(nif, static_cast<std::uint32_t>(block)));
  }
  return blobs;
}

}  // namespace fo4::nif
