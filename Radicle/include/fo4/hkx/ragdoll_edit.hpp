#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxRagdollEditStatus : std::uint8_t {
  Ok,
  NotARagdoll,
  UnsupportedLayout,
  BadTarget,
  BadValue,
  FieldAbsent,
  SharedStorage,
  ShapeGeometryUnsupported,
  VerifyFailed,
  AngleModeUnsupported,
};

struct HkxRagdollBodyEdit {
  std::int32_t body{0};
  std::optional<HkxVec3> position;
  std::optional<HkxQuat> rotation;
  std::optional<HkxVec3> center_of_mass;
  std::optional<float> mass;
};

struct HkxRagdollConstraintEdit {
  std::int32_t constraint{0};
  std::optional<std::int32_t> body_a;
  std::optional<std::int32_t> body_b;
  std::optional<float> twist_min_angle;
  std::optional<float> twist_max_angle;
  std::optional<float> cone_max_angle;
  std::optional<float> min_angle;
  std::optional<float> max_angle;
  std::optional<std::vector<float>> frame_a;
  std::optional<std::vector<float>> frame_b;
  // Constraint: a plane edit is applied only to a ragdoll constraint whose plane atom stores the mode the edit is defined for; friction is the angular friction atom's maximum torque, finite and not negative.
  std::optional<float> plane_min_angle;
  std::optional<float> plane_max_angle;
  std::optional<float> friction;
};

struct HkxRagdollBoneEdit {
  std::int32_t bone{0};
  std::int32_t body{0};
};

// Constraint: a capsule edit must keep the capsule direction and is refused with ShapeGeometryUnsupported when its derived polytope or mass data cannot be proven.
struct HkxRagdollShapeEdit {
  std::int32_t shape{0};
  std::optional<HkxVec3> a;
  std::optional<HkxVec3> b;
  std::optional<float> radius;
};

// Constraint: friction and restitution are stored as the upper 16 bits of a float32, so a written value reads back truncated.
struct HkxRagdollMaterialEdit {
  std::int32_t material{0};
  std::optional<float> dynamic_friction;
  std::optional<float> static_friction;
  std::optional<float> restitution;
  bool allow_shared{false};
};

struct HkxRagdollEdits {
  std::vector<HkxRagdollBodyEdit> bodies;
  std::vector<HkxRagdollConstraintEdit> constraints;
  std::vector<HkxRagdollBoneEdit> bone_map;
  std::vector<HkxRagdollShapeEdit> shapes;
  std::vector<HkxRagdollMaterialEdit> materials;
};

// Invariant: bytes is empty unless status is Ok, and the source span is never modified.
struct HkxRagdollEditResult {
  HkxRagdollEditStatus status{HkxRagdollEditStatus::Ok};
  std::string message;
  std::vector<std::byte> bytes;
};

// Constraint: every edit is an in-place write of existing fields, so the output has the source's size and every untouched byte is identical.
// Constraint: the result is returned only after the output was re-read and matched the requested edits, so a refusal never carries bytes.
HkxRagdollEditResult apply_ragdoll_edits(std::span<const std::byte> source, const HkxRagdollEdits& edits);

const char* ragdoll_edit_status_name(HkxRagdollEditStatus status);

struct HkxRagdollBodyMeasure {
  std::int32_t body{0};
  // Invariant: -1 means the body names no motion row in the file.
  std::int32_t motion{-1};
  float inverse_mass{0.0f};
  HkxVec3 inverse_inertia{};
  HkxVec3 center_of_mass{};

  // Invariant: zero means the file stores no usable inverse mass.
  float mass() const;
  bool operator==(const HkxRagdollBodyMeasure&) const = default;
};

struct HkxRagdollCapsuleMeasure {
  std::int32_t shape{0};
  HkxVec3 a{};
  HkxVec3 b{};
  float radius{0.0f};
  // Invariant: editable is true only when the stored polytope and mass data match the proven capsule model; reason names the first mismatch otherwise.
  bool editable{false};
  std::string reason;
  bool operator==(const HkxRagdollCapsuleMeasure&) const = default;
};

struct HkxRagdollMaterialMeasure {
  std::int32_t material{0};
  float dynamic_friction{0.0f};
  float static_friction{0.0f};
  float restitution{0.0f};
  bool shared{false};
  bool operator==(const HkxRagdollMaterialMeasure&) const = default;
};

struct HkxRagdollMeasures {
  std::vector<HkxRagdollBodyMeasure> bodies;
  std::vector<HkxRagdollCapsuleMeasure> capsules;
  std::vector<HkxRagdollMaterialMeasure> materials;
};

// Constraint: Ok with empty measures means the file holds no ragdoll rows; the shape id of a capsule is its object index, the same number HkxPhysicsShape::id carries.
HkxStatus read_ragdoll_measures(const HkxPackfile& packfile, HkxRagdollMeasures& out);

}  // namespace fo4::hkx
