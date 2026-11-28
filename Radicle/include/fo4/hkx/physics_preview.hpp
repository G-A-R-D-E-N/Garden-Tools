#pragma once

#include "fo4/hkx/physics.hpp"
#include "fo4/hkx/ragdoll_edit.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxPreviewStatus : std::uint8_t {
  Ok,
  NoBodies,
  TooManyBodies,
  TooManyConstraints,
  UnsupportedShape,
  BadMass,
  BadPose,
  BadFrame,
  BadConstraint,
  MissingLimits,
  BadOption,
  BadTarget,
  PoseUnavailable,
  Diverged,
};

const char* preview_status_name(HkxPreviewStatus status);

inline constexpr std::int32_t kPreviewMaxBodies = 128;
inline constexpr std::int32_t kPreviewMaxConstraints = 512;
inline constexpr std::int32_t kPreviewMaxStepsPerCall = 20000;
inline constexpr float kPreviewMinMass = 0.0001f;
inline constexpr float kPreviewMaxMass = 100000.0f;
inline constexpr float kPreviewMaxImpulse = 10000.0f;
inline constexpr double kPreviewStep = 1.0 / 120.0;

struct HkxPreviewOptions {
  float gravity{9.81f};
  bool ground{true};
  float ground_z{0.0f};
  bool body_collision{true};
};

struct HkxPreviewBodyState {
  std::int32_t body{0};
  float mass{0.0f};
  HkxVec3 position{};
  HkxQuat rotation{0.0f, 0.0f, 0.0f, 1.0f};
  HkxVec3 center_of_mass{};
  HkxVec3 linear_velocity{};
  HkxVec3 angular_velocity{};
  bool grounded{false};
};

struct HkxPreviewConstraintState {
  std::int32_t constraint{0};
  HkxConstraintKind kind{HkxConstraintKind::Unknown};
  float anchor_error{0.0f};
  std::optional<float> twist_angle;
  std::optional<float> cone_angle;
  std::optional<float> hinge_angle;
  std::optional<float> twist_min;
  std::optional<float> twist_max;
  std::optional<float> cone_max;
  std::optional<float> hinge_min;
  std::optional<float> hinge_max;
  // Invariant: the largest excess of a measured angle over its effective limit, zero while every angle is inside.
  float violation{0.0f};
  bool overridden{false};
};

// Constraint: this class holds all preview state; it never writes the model, the measures or any file, so an override cannot reach a save.
// Constraint: capsule bodies only, a fixed 1/120 s step and fixed iteration counts, so the same input always gives the same bits.
class HkxPhysicsPreview {
 public:
  HkxPhysicsPreview();
  ~HkxPhysicsPreview();
  HkxPhysicsPreview(const HkxPhysicsPreview&) = delete;
  HkxPhysicsPreview& operator=(const HkxPhysicsPreview&) = delete;

  // Requires the measures to come from read_ragdoll_measures on the same packfile as the model. On any refusal the preview stays closed.
  HkxPreviewStatus open(const HkxRagdollModel& model, const HkxRagdollMeasures& measures, const HkxPreviewOptions& options,
                        std::string* message = nullptr);
  bool is_open() const;

  // Constraint: recover is reset, which returns every body to the authored pose or the last set_pose, with zero velocity.
  void reset();
  HkxPreviewStatus step(std::int32_t count, std::string* message = nullptr);
  // Constraint: a non-finite value is refused; a magnitude above kPreviewMaxImpulse is clamped to it.
  HkxPreviewStatus apply_impulse(std::int32_t body, const HkxVec3& linear, const std::optional<HkxVec3>& angular = {});
  // Constraint: only the limit fields of the edit are used; any body or frame field is refused so a preview cannot rebind or move a joint.
  HkxPreviewStatus override_limits(const HkxRagdollConstraintEdit& limits, std::string* message = nullptr);
  void clear_overrides();
  // Constraint: places every bound body from the animation pose through the mapper and resets motion, and fails closed when the pose cannot be mapped.
  HkxPreviewStatus set_pose(std::span<const HkxBoneFrame> animation_world, std::string* message = nullptr);

  std::int64_t steps() const;
  std::vector<HkxPreviewBodyState> bodies() const;
  std::vector<HkxPreviewConstraintState> constraints() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fo4::hkx
