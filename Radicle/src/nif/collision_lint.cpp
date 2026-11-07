#include "fo4/nif/collision_lint.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::string_view k_collision_object = "bhkNPCollisionObject";
constexpr std::string_view k_physics_system = "bhkPhysicsSystem";
constexpr std::string_view k_ragdoll_system = "bhkRagdollSystem";
constexpr std::string_view k_bsx_flags = "BSXFlags";
constexpr std::uint32_t k_havok_bsx_bit = 2;
constexpr std::uint32_t k_layer_mask = 0xFF;
constexpr std::string_view k_dynamic_compound = "hknpDynamicCompoundShape";

struct ObjectRefs {
  std::int32_t target{-1};
  std::int32_t data{-1};
  std::uint32_t body{0};
};

bool is_system(std::string_view type) { return type == k_physics_system || type == k_ragdoll_system; }

ObjectRefs object_refs(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  ObjectRefs refs;
  refs.target = cursor.read_i32();
  cursor.read_u16();
  refs.data = cursor.read_i32();
  refs.body = cursor.read_u32();
  return refs;
}

bool has_havok_flag(const NifFile& nif) {
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != k_bsx_flags) continue;
    auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(block));
    cursor.read_i32();
    if ((cursor.read_u32() & k_havok_bsx_bit) != 0) return true;
  }
  return false;
}

void add(std::vector<CollisionFinding>& findings, CollisionIssue issue, std::uint32_t block) {
  findings.push_back(CollisionFinding{issue, block});
}

void lint_system(std::vector<CollisionFinding>& findings, const CollisionBlob& blob,
                 std::map<std::uint32_t, std::size_t>& bodies_of) {
  try {
    const auto system = decode_hknp_system(read_hknp_packfile(blob.bytes));
    bodies_of[blob.block] = system.bodies.size();
    for (const auto& body : system.bodies) {
      if (!body.dynamic && (body.packed_filter & k_layer_mask) == 0) {
        add(findings, CollisionIssue::layer_zero, blob.block);
        break;
      }
    }
    if (!system.unsupported.empty()) add(findings, CollisionIssue::unsupported_shape, blob.block);
    for (const auto& constraint : system.constraints) {
      if (constraint.unread_atom != 0) {
        add(findings, CollisionIssue::unread_atom, blob.block);
        break;
      }
    }
    for (const auto& shape : system.shapes) {
      if (shape.kind == HknpShapeKind::compound && shape.instances.size() == 1 &&
          shape.class_name == k_dynamic_compound) {
        add(findings, CollisionIssue::one_child_compound, blob.block);
        break;
      }
    }
  } catch (const fo4::parse_error&) {
    add(findings, CollisionIssue::undecodable_system, blob.block);
  }
}

}  // namespace

std::vector<CollisionFinding> lint_collision(const NifFile& nif) {
  std::vector<CollisionFinding> findings;
  const auto blobs = read_collision_blobs(nif);
  std::map<std::uint32_t, std::size_t> bodies_of;
  for (const auto& blob : blobs) {
    if (blob.owner < 0) add(findings, CollisionIssue::system_without_object, blob.block);
    lint_system(findings, blob, bodies_of);
  }

  bool any_object = false;
  std::uint32_t first_object = 0;
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != k_collision_object) continue;
    const auto object = static_cast<std::uint32_t>(block);
    if (!any_object) {
      any_object = true;
      first_object = object;
    }
    const auto refs = object_refs(nif, object);
    const bool target_ok = refs.target >= 0 && nif.node_for_block(static_cast<std::uint32_t>(refs.target)) >= 0;
    const bool data_ok = refs.data >= 0 && static_cast<std::size_t>(refs.data) < nif.blocks().size() &&
                         is_system(nif.blocks()[static_cast<std::size_t>(refs.data)].type);
    if (!target_ok || !data_ok) {
      add(findings, CollisionIssue::dangling_object, object);
      continue;
    }
    const auto bodies = bodies_of.find(static_cast<std::uint32_t>(refs.data));
    if (bodies != bodies_of.end() && refs.body >= bodies->second) {
      add(findings, CollisionIssue::body_out_of_range, object);
    }
  }
  if (any_object && !has_havok_flag(nif)) add(findings, CollisionIssue::bsx_mismatch, first_object);
  return findings;
}

}  // namespace fo4::nif
