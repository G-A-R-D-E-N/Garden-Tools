#pragma once

#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::nif {

enum class PlainShapeKind : std::uint8_t { box = 1, sphere = 2, capsule = 3, convex_hull = 4, triangle_mesh = 5 };

// Constraint: a capsule whose end points are nearer than this many metres is emitted as a sphere.
inline constexpr float k_degenerate_capsule_metres = 1.0e-5f;

// Invariant: every position is in engine axes (x, z, -y of Havok) and metres, and basis is row-major.
struct PlainShape {
  PlainShapeKind kind{PlainShapeKind::box};
  std::uint32_t system{0};
  std::uint32_t body{0};
  std::uint32_t material{0};
  std::array<float, 3> origin{};
  std::array<float, 9> basis{1, 0, 0, 0, 1, 0, 0, 0, 1};
  // Constraint: half_extents belong to a box and are along the basis columns.
  std::array<float, 3> half_extents{};
  // Constraint: radius belongs to a sphere or capsule and includes the whole rounding.
  float radius{0.0f};
  std::array<float, 3> point_a{};
  std::array<float, 3> point_b{};
  // Constraint: margin is a hull's convex radius, stored and not applied to points.
  float margin{0.0f};
  // Constraint: points are xyz triples of a hull or a mesh, already in the shape frame.
  std::vector<float> points;
  // Invariant: indices are triangle corners into points, three per triangle, quads already split.
  std::vector<std::uint32_t> indices;
  // Invariant: triangle_materials holds one entry per triangle of a mesh and is empty otherwise.
  std::vector<std::uint32_t> triangle_materials;
};

// Invariant: engine axes and metres like every plain position; rotation is column-vector, row-major like basis; scale is the chain's uniform scale, not folded into rotation.
struct PlainCollisionTransform {
  std::array<float, 3> translation{};
  std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
  float scale{1.0f};
};

struct PlainBody {
  std::uint32_t system{0};
  std::uint32_t body{0};
  // Constraint: layer is the low byte of the packed collision filter.
  std::uint32_t layer{0};
  bool dynamic{false};
  // Constraint: trigger_type is the material record's trigger byte, passed through uninterpreted.
  std::uint8_t trigger_type{0};
  bool has_mass{false};
  float mass{0.0f};
  bool has_center_of_mass{false};
  // Constraint: center_of_mass is in physics-system space, the space of origin, not body-local.
  std::array<float, 3> center_of_mass{};
  // Constraint: origin and basis are the body's stored placement and are not applied to its shapes.
  std::array<float, 3> origin{};
  std::array<float, 9> basis{1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::size_t first_shape{0};
  std::size_t shape_count{0};
  // Constraint: counts only the shapes skipped under this body; PlainCollision::skipped_shapes is the total.
  std::uint32_t skipped_shapes{0};
  // Invariant: has_owner is false when no bhkNPCollisionObject names this body, and then the owner fields stay at -1 and identity.
  bool has_owner{false};
  std::int32_t owner_block{-1};
  std::int32_t owner_target_node{-1};
  PlainCollisionTransform owner_transform;
};

struct PlainCollision {
  std::vector<PlainBody> bodies;
  // Invariant: shapes of one body are contiguous, in body order.
  std::vector<PlainShape> shapes;
  // Invariant: a shape the plain forms cannot hold (an unsupported class, a non-uniformly scaled sphere or capsule, a hull under four points) is counted here and left out.
  std::uint32_t skipped_shapes{0};
  std::uint32_t undecodable_systems{0};
};

// Requires decoded systems; system is the index the shapes and bodies carry.
void append_plain_collision(const HknpSystem& system, std::uint32_t system_index, PlainCollision& out);

// Constraint: the node's chain without the root, the frame shape_to_global gives, as a PlainCollisionTransform; a node index of -1 or the root is identity.
PlainCollisionTransform plain_node_transform(const NifFile& nif, std::int32_t node);

// Invariant: per body the first owner in block order with the same body id wins; blobs must be the list build_plain_collision got.
void attach_collision_owners(PlainCollision& plain, const NifFile& nif, std::span<const CollisionBlob> blobs);

// Constraint: a blob that does not read or decode is counted in undecodable_systems and the others still return.
PlainCollision build_plain_collision(std::span<const CollisionBlob> blobs);

}  // namespace fo4::nif
