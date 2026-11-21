#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

using HkxVec3 = std::array<float, 3>;
// Constraint: quaternions are stored x, y, z, w, the same order as HkxQsTransform::rotation.
using HkxQuat = std::array<float, 4>;

enum class HkxPhysicsShapeKind : std::uint8_t {
  Unknown,
  Sphere,
  Capsule,
  Box,
  ConvexHull,
  Mesh,
  Compound,
};

struct HkxPhysicsShape {
  // Invariant: id is the shape's object index in the packfile, so a body's shape_id names the same number.
  std::int32_t id{0};
  HkxPhysicsShapeKind kind{HkxPhysicsShapeKind::Unknown};
  HkxVec3 half_extents{};
  float radius{0.0f};
  std::vector<std::int32_t> children;
  // Invariant: empty vertices and face_rings mean the file carried no polytope payload for this shape.
  std::vector<HkxVec3> vertices;
  std::vector<std::vector<std::int32_t>> face_rings;
};

struct HkxRigidBody {
  std::int32_t id{0};
  std::string name;
  // Invariant: -1 means the body names no shape object this reader indexes.
  std::int32_t shape_id{-1};
  std::int32_t bone_index{-1};
  float mass{0.0f};
  // Invariant: a zero center_of_mass means the file carried no measured center of mass for the body.
  HkxVec3 center_of_mass{};
  HkxVec3 position{};
  HkxQuat rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

// Constraint: center_of_mass is in the reference world frame, so it is taken back through the measured rotation first.
HkxVec3 posed_center_of_mass(const HkxRigidBody& body, const HkxVec3& posed_position, const HkxQuat& posed_rotation);

enum class HkxConstraintKind : std::uint8_t {
  Unknown,
  Fixed,
  Hinge,
  LimitedHinge,
  BallAndSocket,
  Ragdoll,
};

struct HkxPhysicsConstraint {
  std::int32_t id{0};
  HkxConstraintKind kind{HkxConstraintKind::Unknown};
  std::int32_t body_a{0};
  std::int32_t body_b{0};
  float min_angle{0.0f};
  float max_angle{0.0f};
  std::optional<float> twist_min_angle;
  std::optional<float> twist_max_angle;
  std::optional<float> cone_max_angle;
  std::int32_t twist_axis{0};
  std::int32_t twist_ref_axis{1};
  std::int32_t cone_twist_axis{0};
  std::int32_t cone_ref_axis{0};
  std::int32_t limit_axis{0};
  bool has_preview_limits{false};
  // Invariant: a frame read from a file holds sixteen floats, three padded columns then the translation.
  std::optional<std::vector<float>> frame_a;
  std::optional<std::vector<float>> frame_b;
};

struct HkxRagdollBoneBinding {
  std::int32_t bone_index{0};
  std::int32_t body_id{0};
};

struct HkxRagdollMapping {
  std::int32_t bone_a{0};
  std::int32_t bone_b{0};
  HkxVec3 a_from_b_translation{};
  HkxQuat a_from_b_rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct HkxBoneFrame {
  HkxVec3 position{};
  HkxQuat rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct HkxRagdollModel {
  std::vector<HkxPhysicsShape> shapes;
  std::vector<HkxRigidBody> bodies;
  std::vector<HkxPhysicsConstraint> constraints;
  std::vector<HkxRagdollBoneBinding> bone_bindings;
  std::optional<HkxSkeleton> skeleton;
  std::vector<HkxRagdollMapping> mappings;
  std::optional<HkxSkeleton> animation_skeleton;
};

// Constraint: a parent index that is not an earlier bone is treated as a root, so the result always has one frame per reference pose.
std::vector<HkxBoneFrame> world_reference_frames(const HkxSkeleton& skeleton);

// Constraint: returns false, leaving `out` empty, when there are no mappings, no physics skeleton or a mapping bone is out of range.
bool map_pose(const HkxRagdollModel& model, std::span<const HkxBoneFrame> animation_world,
              std::vector<HkxBoneFrame>& out);

enum class HkxPhysicsValidationLevel : std::uint8_t { Warning, Error };

struct HkxPhysicsValidationFinding {
  HkxPhysicsValidationLevel level{HkxPhysicsValidationLevel::Error};
  std::string where;
  std::string message;
};

std::vector<HkxPhysicsValidationFinding> check_physics_model(const HkxRagdollModel& model,
                                                             std::optional<std::int32_t> skeleton_bone_count = {});

enum class HkxPhysicsLoadStatus : std::uint8_t {
  Ok,
  SkeletonOnly,
  LegacyPhysics,
  UnsupportedShapes,
  MapperMissing,
  NoBodies,
  WrongContainer,
  ParseError,
};

enum class HkxPhysicsLoadSeverity : std::uint8_t { Info, Warning, Error };

struct HkxPhysicsLoadFinding {
  HkxPhysicsLoadSeverity severity{HkxPhysicsLoadSeverity::Info};
  std::string message;
  // Invariant: an empty hint means the finding has no follow-up advice.
  std::string hint;
};

// Invariant: keys sort byte-wise, so a class listing is the same on every platform.
using HkxClassCounts = std::map<std::string, std::int32_t>;

inline constexpr std::string_view kPhysicsRagdollClass = "hknpRagdollData";
inline constexpr std::string_view kPhysicsSkeletonClass = "hkaSkeleton";
inline constexpr std::string_view kPhysicsMapperClass = "hkaSkeletonMapper";
inline constexpr std::string_view kPhysicsMaterialClass = "hknpBSMaterial";

inline constexpr std::int32_t kFallout4FileVersion = 11;
inline constexpr std::int32_t kFallout4PointerSize = 8;

// Constraint: an empty string field means "not set"; the classifier reads only the fields a stage filled in.
struct HkxPhysicsLoadFacts {
  std::string container_problem;
  std::string container_notice;
  std::string parse_stage;
  std::string parse_message;
  std::optional<std::int32_t> file_version;
  std::optional<std::int32_t> pointer_size;
  HkxClassCounts class_counts;
  std::int32_t body_count{0};
  std::int32_t usable_bodies{0};
  std::int32_t skipped_bodies{0};
  std::int32_t skipped_constraints{0};
  std::int32_t unsupported_shape_bodies{0};
  HkxClassCounts unsupported_shape_classes;
  std::int32_t mappings{0};
  std::int32_t shape_count{0};
  std::int32_t constraint_count{0};

  std::int32_t count_of(const std::string& class_name) const;
  bool has(const std::string& class_name) const { return count_of(class_name) > 0; }
};

struct HkxPhysicsLoadReport {
  HkxPhysicsLoadStatus status{HkxPhysicsLoadStatus::Ok};
  std::vector<HkxPhysicsLoadFinding> findings;
  HkxClassCounts class_counts;
  std::optional<std::int32_t> file_version;
  std::optional<std::int32_t> pointer_size;
  std::string failing_stage;
  std::int32_t body_count{0};
  std::int32_t usable_bodies{0};
  std::int32_t skipped_bodies{0};
  std::int32_t skipped_constraints{0};
  std::int32_t unsupported_shape_bodies{0};
  HkxClassCounts unsupported_shape_classes;
  std::int32_t shape_count{0};
  std::int32_t constraint_count{0};
  std::int32_t mappings{0};

  std::int32_t count_of(const std::string& class_name) const;
  bool has(const std::string& class_name) const { return count_of(class_name) > 0; }
  bool mapper_present() const { return has(std::string(kPhysicsMapperClass)); }
  bool material_present() const { return has(std::string(kPhysicsMaterialClass)); }

  std::string status_label() const;
  std::string status_slug() const;
  HkxPhysicsLoadSeverity severity() const;
  std::string headline() const;
  std::string hint() const;
  std::string summary() const;
  HkxClassCounts shape_classes() const;
  HkxClassCounts constraint_classes() const;
  HkxClassCounts physics_classes() const;
  std::string describe_bodies() const;
  std::string describe_mapper() const;
  // Constraint: file_size is optional because a caller that only holds bytes in memory may not know it.
  std::string diagnostics(const std::string& path, std::optional<std::int64_t> file_size = {},
                          const std::string& product = {}) const;
};

// Constraint: lists "Class xN" entries, most common first then by name, or "none" for an empty set.
std::string describe_physics_classes(const HkxClassCounts& counts);
HkxClassCounts physics_classes_of(const HkxClassCounts& counts);

bool is_physics_shape_class(std::string_view class_name);
bool is_physics_constraint_class(std::string_view class_name);
bool is_legacy_physics_class(std::string_view class_name);

// Constraint: returns an empty string when the bytes look like a Fallout 4 packfile, else the reason they do not.
std::string physics_container_problem(std::span<const std::byte> bytes, std::optional<std::int32_t>& file_version,
                                      std::optional<std::int32_t>& pointer_size);

HkxPhysicsLoadReport classify_physics_load(const HkxPhysicsLoadFacts& facts);

enum class HkxPhysicsClassFamily : std::uint8_t { Cloth, NewPhysics, LegacyPhysics };

std::optional<HkxPhysicsClassFamily> physics_class_family(std::string_view class_name);

// Constraint: Ok means a model was produced; MissingObject means the file holds no usable ragdoll; any other status is the read failure.
// Constraint: `report` is filled on every return and `out` is empty unless Ok, so a caller never has to guess why a file produced no model.
HkxStatus read_ragdoll(const HkxPackfile& packfile, HkxRagdollModel& out, HkxPhysicsLoadReport& report);
HkxStatus read_ragdoll(std::span<const std::byte> bytes, HkxRagdollModel& out, HkxPhysicsLoadReport& report);

// Constraint: classifies a file without keeping the model, so a caller that only wants the verdict pays for one call.
HkxPhysicsLoadReport inspect_physics_file(std::span<const std::byte> bytes);

}  // namespace fo4::hkx
