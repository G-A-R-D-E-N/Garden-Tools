#pragma once

#include "fo4/nif/hknp_packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fo4::nif {

using HknpVec3 = std::array<float, 3>;
// Constraint: a HknpVec4 holding a rotation is stored x, y, z, w, which is Havok's order.
using HknpVec4 = std::array<float, 4>;

// Invariant: this index means no object is referenced here, either a null pointer or one the decoder did not follow.
inline constexpr std::size_t k_no_object = std::numeric_limits<std::size_t>::max();

enum class HknpShapeKind : std::uint8_t { convex, sphere, capsule, scaled_convex, compound };

struct HknpMassProperties {
  HknpVec3 center{};
  // Constraint: inertia is stored at 1.5 times the physical inertia, and is not divided here.
  HknpVec3 inertia_raw{};
  float volume{0.0f};
  float mass{0.0f};
  std::uint64_t packed_orientation{0};
};

struct HknpInstance {
  std::array<HknpVec3, 3> rotation_rows{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  HknpVec3 translation{};
  HknpVec3 scale{1, 1, 1};
  // Invariant: child_object is k_no_object when the instance holds a null child pointer.
  std::size_t child_object{k_no_object};
  // Invariant: shape is an index into HknpSystem::shapes, or -1 when the child is listed in HknpSystem::unsupported.
  int shape{-1};
};

struct HknpShape {
  HknpShapeKind kind{HknpShapeKind::convex};
  std::string class_name;
  // Invariant: object indexes HknpSystem::packfile::objects, whose bytes are the source this shape was decoded from.
  std::size_t object{0};
  std::uint32_t shape_flags{0};
  float convex_radius{0.0f};
  std::uint32_t material_crc{0};
  // Constraint: convex shapes keep every stored vertex in order, including duplicates.
  std::vector<HknpVec3> vertices;
  std::vector<HknpVec4> planes;
  // Invariant: each face is a loop of at least three vertex indices into vertices.
  std::vector<std::vector<std::uint8_t>> faces;
  std::vector<std::uint8_t> face_min_half_angles;
  // Constraint: point_a is the sphere centre, or capsule end a, and point_b is capsule end b.
  HknpVec3 point_a{};
  HknpVec3 point_b{};
  // Constraint: radius is the sphere radius, or the outer capsule radius.
  float radius{0.0f};
  HknpVec3 scale{1, 1, 1};
  // Invariant: scaled_convex child is an index into HknpSystem::shapes, or -1 when the child is unsupported.
  int child{-1};
  std::vector<HknpInstance> instances;
  bool has_mass_properties{false};
  HknpMassProperties mass_properties{};
};

struct HknpBody {
  // Constraint: cinfo_offset is the offset of the hknpBodyCinfo inside the root object.
  std::size_t cinfo_offset{0};
  // Invariant: shape_object is the referenced shape object, and shape is its index into HknpSystem::shapes or -1.
  std::size_t shape_object{k_no_object};
  int shape{-1};
  std::uint32_t packed_filter{0};
  std::uint32_t flags{0};
  // Constraint: motion_index 0x7fffffff marks a static body, and dynamic is false for it.
  std::uint32_t motion_index{0};
  bool dynamic{false};
  HknpVec3 position{};
  HknpVec4 orientation{{0, 0, 0, 1}};
  // Constraint: friction and restitution are read from the float32 whose upper 16 bits are stored.
  float friction{0.0f};
  float restitution{0.0f};
  std::uint32_t material_flags{0};
  std::uint8_t trigger_type{0};
};

struct HknpDynMotion {
  float gravity_factor{0.0f};
  float max_linear_velocity{0.0f};
  float max_angular_velocity{0.0f};
  float linear_damping{0.0f};
  float angular_damping{0.0f};
};

struct HknpDynInertia {
  // Constraint: inverse_mass is the stored value, and mass is 1 / inverse_mass or 0 when that is not positive.
  float inverse_mass{0.0f};
  float mass{0.0f};
  float density{0.0f};
  // Invariant: inverse_inertia is the diagonal of the inverse inertia tensor.
  HknpVec3 inverse_inertia{};
  HknpVec3 center{};
  HknpVec4 orientation{{0, 0, 0, 1}};
};

struct HknpAngularLimit {
  bool present{false};
  float min{0.0f};
  float max{0.0f};
  float tau{0.0f};
};

struct HknpConstraint {
  int child_body{-1};
  int parent_body{-1};
  // Invariant: object is the hkp*ConstraintData object that holds the atoms, after a breakable wrapper is unwrapped.
  std::size_t object{k_no_object};
  std::string kind;
  bool breakable{false};
  float break_threshold{0.0f};
  bool has_frames{false};
  // Constraint: rotation_a and pivot_a belong to the child body, and rotation_b and pivot_b to the parent body.
  std::array<HknpVec3, 3> rotation_a{};
  std::array<HknpVec3, 3> rotation_b{};
  HknpVec3 pivot_a{};
  HknpVec3 pivot_b{};
  HknpAngularLimit twist;
  HknpAngularLimit cone;
  HknpAngularLimit plane;
  HknpAngularLimit hinge;
  float friction{0.0f};
  bool motor_enabled{false};
  // Invariant: unread_atom is the type of the atom where the walk stopped on a type with no length, and 0 when the walk reached a terminator.
  std::uint16_t unread_atom{0};
};

struct HknpBone {
  int parent{-1};
  HknpVec3 translation{};
  HknpVec4 rotation{{0, 0, 0, 1}};
  HknpVec3 scale{1, 1, 1};
};

struct HknpUnsupportedShape {
  std::string class_name;
  std::size_t object{0};
};

struct HknpSystem {
  // Invariant: packfile owns every object that the decoded indices refer to, so source bytes stay reachable.
  HknpPackfile packfile;
  std::string root_class;
  std::vector<HknpShape> shapes;
  std::vector<HknpBody> bodies;
  std::vector<HknpDynMotion> motions;
  std::vector<HknpDynInertia> inertias;
  std::vector<HknpConstraint> constraints;
  std::vector<HknpBone> bones;
  // Invariant: every shape class reached from a body or a compound child that the decoder does not handle is listed here with its object index.
  std::vector<HknpUnsupportedShape> unsupported;
};

// Requires objects[0] to be hknpPhysicsSystemData or hknpRagdollData; malformed data raises fo4::parse_error, and an unsupported shape class is listed in unsupported, never thrown.
HknpSystem decode_hknp_system(HknpPackfile pack);

}  // namespace fo4::nif
