#include "fo4/hkx/physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr float kRotationNormTolerance = 1.0e-3f;
constexpr float kRotationNormEpsilon = 1.0e-4f;
constexpr float kScaleTolerance = 1.0e-4f;

float quat_length_squared(const HkxQuat& q) { return q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]; }

HkxQuat quat_normalize(const HkxQuat& q) {
  const float length = std::sqrt(quat_length_squared(q));
  return {q[0] / length, q[1] / length, q[2] / length, q[3] / length};
}

HkxQuat quat_multiply(const HkxQuat& a, const HkxQuat& b) {
  return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

HkxQuat quat_inverse(const HkxQuat& q) {
  const float length_squared = quat_length_squared(q);
  if (length_squared <= 0.0f) return q;
  return {-q[0] / length_squared, -q[1] / length_squared, -q[2] / length_squared, q[3] / length_squared};
}

HkxVec3 quat_rotate(const HkxQuat& q, const HkxVec3& v) {
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  return {v[0] + q[3] * tx + (q[1] * tz - q[2] * ty), v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
          v[2] + q[3] * tz + (q[0] * ty - q[1] * tx)};
}

HkxVec3 add(const HkxVec3& a, const HkxVec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

HkxVec3 subtract(const HkxVec3& a, const HkxVec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

bool finite(float value) { return std::isfinite(value); }

bool finite(const HkxVec3& value) { return finite(value[0]) && finite(value[1]) && finite(value[2]); }

bool finite(const HkxQuat& value) {
  return finite(value[0]) && finite(value[1]) && finite(value[2]) && finite(value[3]);
}

std::string number(float value) {
  char buffer[32];
  for (int digits = 1; digits <= 9; ++digits) {
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, static_cast<double>(value));
    if (std::strtof(buffer, nullptr) == value) break;
  }
  return buffer;
}

std::string short_number(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.4f", static_cast<double>(value));
  std::string text = buffer;
  while (!text.empty() && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  return text;
}

HkxPhysicsValidationFinding error(std::string where, std::string message) {
  return {HkxPhysicsValidationLevel::Error, std::move(where), std::move(message)};
}

HkxPhysicsValidationFinding warning(std::string where, std::string message) {
  return {HkxPhysicsValidationLevel::Warning, std::move(where), std::move(message)};
}

void check_finite_non_negative(float value, const std::string& where, const char* field,
                               std::vector<HkxPhysicsValidationFinding>& findings) {
  if (!finite(value) || value < 0.0f) {
    findings.push_back(error(where, std::string(field) + " must be a finite non-negative value"));
  }
}

void check_unique_ids(const std::vector<std::int32_t>& ids, const char* kind,
                      std::vector<HkxPhysicsValidationFinding>& findings) {
  std::unordered_map<std::int32_t, int> seen;
  for (const std::int32_t id : ids) ++seen[id];
  std::unordered_set<std::int32_t> reported;
  for (const std::int32_t id : ids) {
    if (seen[id] > 1 && reported.insert(id).second) {
      findings.push_back(error(std::string(kind) + " " + std::to_string(id), std::string("duplicate ") + kind + " id"));
    }
  }
}

void check_frame(const std::optional<std::vector<float>>& frame, const std::string& where, const char* label,
                 std::vector<HkxPhysicsValidationFinding>& findings) {
  if (!frame) return;
  if (frame->size() != 16) {
    findings.push_back(error(where, std::string(label) + " must hold sixteen floats"));
    return;
  }
  if (std::any_of(frame->begin(), frame->end(), [](float value) { return !finite(value); })) {
    findings.push_back(error(where, std::string(label) + " must contain only finite values"));
  }
}

void check_axis_index(std::int32_t axis, const std::optional<std::vector<float>>& frame, const std::string& where,
                      const char* label, std::vector<HkxPhysicsValidationFinding>& findings) {
  if (!frame) return;
  if (axis < 0 || axis > 2) {
    findings.push_back(
        error(where, std::string(label) + " " + std::to_string(axis) + " is outside a three-column frame"));
  }
}

void check_shape_cycles(const std::vector<const HkxPhysicsShape*>& ordered,
                        const std::unordered_map<std::int32_t, const HkxPhysicsShape*>& shapes,
                        std::vector<HkxPhysicsValidationFinding>& findings) {
  constexpr int kVisiting = 1;
  constexpr int kDone = 2;
  std::unordered_map<std::int32_t, int> state;
  std::unordered_set<std::int32_t> reported;
  std::vector<std::int32_t> path;

  const std::function<void(std::int32_t)> visit = [&](std::int32_t id) {
    const int current = state.contains(id) ? state[id] : 0;
    if (current == kDone) return;
    if (current == kVisiting) {
      const auto last = std::find(path.rbegin(), path.rend(), id);
      std::vector<std::int32_t> loop;
      if (last != path.rend()) {
        loop.assign(path.begin() + (path.rend() - last - 1), path.end());
      } else {
        loop = path;
      }
      loop.push_back(id);
      if (reported.insert(id).second) {
        std::string text;
        for (std::size_t i = 0; i < loop.size(); ++i) {
          if (i > 0) text += " -> ";
          text += std::to_string(loop[i]);
        }
        findings.push_back(error("shape " + std::to_string(id), "is part of a containment cycle (" + text + ")"));
      }
      return;
    }
    const auto found = shapes.find(id);
    if (found == shapes.end()) return;
    state[id] = kVisiting;
    path.push_back(id);
    for (const std::int32_t child : found->second->children) visit(child);
    path.pop_back();
    state[id] = kDone;
  };

  for (const HkxPhysicsShape* shape : ordered) visit(shape->id);
}

}  // namespace

HkxVec3 posed_center_of_mass(const HkxRigidBody& body, const HkxVec3& posed_position, const HkxQuat& posed_rotation) {
  if (body.center_of_mass == HkxVec3{}) return {};
  const HkxQuat identity{0.0f, 0.0f, 0.0f, 1.0f};
  const HkxQuat reference = quat_length_squared(body.rotation) > 1.0e-10f ? quat_normalize(body.rotation) : identity;
  const HkxQuat target = quat_length_squared(posed_rotation) > 1.0e-10f ? quat_normalize(posed_rotation) : identity;
  const HkxVec3 local = quat_rotate(quat_inverse(reference), subtract(body.center_of_mass, body.position));
  return add(posed_position, quat_rotate(target, local));
}

std::vector<HkxBoneFrame> world_reference_frames(const HkxSkeleton& skeleton) {
  const std::size_t count = skeleton.reference_pose.size();
  std::vector<HkxBoneFrame> frames(count);
  for (std::size_t i = 0; i < count; ++i) {
    const HkxQsTransform& pose = skeleton.reference_pose[i];
    const bool has_parent = i > 0 && skeleton.parent_indices.size() > i && skeleton.parent_indices[i] >= 0 &&
                            static_cast<std::size_t>(skeleton.parent_indices[i]) < i;
    if (!has_parent) {
      frames[i] = {pose.translation, quat_normalize(pose.rotation)};
    } else {
      const HkxBoneFrame& parent = frames[static_cast<std::size_t>(skeleton.parent_indices[i])];
      frames[i] = {add(parent.position, quat_rotate(parent.rotation, pose.translation)),
                   quat_normalize(quat_multiply(parent.rotation, pose.rotation))};
    }
  }
  return frames;
}

bool map_pose(const HkxRagdollModel& model, std::span<const HkxBoneFrame> animation_world,
              std::vector<HkxBoneFrame>& out) {
  out.clear();
  if (model.mappings.empty() || !model.skeleton) return false;

  const std::vector<HkxBoneFrame> reference = world_reference_frames(*model.skeleton);
  std::vector<HkxBoneFrame> frames(reference.size());
  std::vector<bool> covered(reference.size(), false);

  for (const HkxRagdollMapping& mapping : model.mappings) {
    if (mapping.bone_a < 0 || static_cast<std::size_t>(mapping.bone_a) >= animation_world.size()) return false;
    if (mapping.bone_b < 0 || static_cast<std::size_t>(mapping.bone_b) >= frames.size()) return false;
    const HkxBoneFrame& a = animation_world[static_cast<std::size_t>(mapping.bone_a)];
    const auto b = static_cast<std::size_t>(mapping.bone_b);
    frames[b] = {add(a.position, quat_rotate(a.rotation, mapping.a_from_b_translation)),
                 quat_normalize(quat_multiply(a.rotation, mapping.a_from_b_rotation))};
    covered[b] = true;
  }
  for (std::size_t i = 0; i < reference.size(); ++i) {
    if (!covered[i]) frames[i] = reference[i];
  }
  out = std::move(frames);
  return true;
}

std::vector<HkxPhysicsValidationFinding> check_physics_model(const HkxRagdollModel& model,
                                                             std::optional<std::int32_t> skeleton_bone_count) {
  std::vector<HkxPhysicsValidationFinding> findings;

  std::vector<std::int32_t> ids;
  for (const HkxPhysicsShape& shape : model.shapes) ids.push_back(shape.id);
  check_unique_ids(ids, "shape", findings);
  ids.clear();
  for (const HkxRigidBody& body : model.bodies) ids.push_back(body.id);
  check_unique_ids(ids, "body", findings);
  ids.clear();
  for (const HkxPhysicsConstraint& constraint : model.constraints) ids.push_back(constraint.id);
  check_unique_ids(ids, "constraint", findings);

  std::unordered_map<std::int32_t, const HkxPhysicsShape*> shapes;
  std::vector<const HkxPhysicsShape*> ordered_shapes;
  for (const HkxPhysicsShape& shape : model.shapes) {
    if (shapes.emplace(shape.id, &shape).second) ordered_shapes.push_back(&shape);
  }
  std::unordered_set<std::int32_t> bodies;
  for (const HkxRigidBody& body : model.bodies) bodies.insert(body.id);

  for (const HkxPhysicsShape& shape : model.shapes) {
    const std::string where = "shape " + std::to_string(shape.id);
    if (shape.id < 0) findings.push_back(error(where, "shape ids must be non-negative"));
    check_finite_non_negative(shape.radius, where, "radius", findings);
    check_finite_non_negative(shape.half_extents[0], where, "half extent X", findings);
    check_finite_non_negative(shape.half_extents[1], where, "half extent Y", findings);
    check_finite_non_negative(shape.half_extents[2], where, "half extent Z", findings);

    for (const HkxVec3& vertex : shape.vertices) {
      if (!finite(vertex)) findings.push_back(error(where, "polytope vertices must contain only finite values"));
    }
    const auto vertex_count = static_cast<std::int64_t>(shape.vertices.size());
    for (const auto& ring : shape.face_rings) {
      for (const std::int32_t index : ring) {
        if (index < 0 || index >= vertex_count) {
          findings.push_back(warning(where, "face references missing polytope vertex " + std::to_string(index)));
        }
      }
    }
    for (const std::int32_t child : shape.children) {
      if (!shapes.contains(child)) {
        findings.push_back(error(where, "references missing child shape " + std::to_string(child)));
      }
    }
  }

  check_shape_cycles(ordered_shapes, shapes, findings);

  for (const HkxRigidBody& body : model.bodies) {
    const std::string where = "body " + std::to_string(body.id);
    if (body.id < 0) findings.push_back(error(where, "body ids must be non-negative"));
    if (!shapes.contains(body.shape_id)) {
      findings.push_back(error(where, "references missing shape " + std::to_string(body.shape_id)));
    }
    check_finite_non_negative(body.mass, where, "mass", findings);
    if (!finite(body.center_of_mass)) findings.push_back(error(where, "center of mass must contain only finite values"));
    if (!finite(body.position)) findings.push_back(error(where, "position must contain only finite values"));
    if (!finite(body.rotation)) {
      findings.push_back(error(where, "rotation must contain only finite values"));
    } else {
      const float norm = std::sqrt(quat_length_squared(body.rotation));
      if (norm < kRotationNormEpsilon) {
        findings.push_back(error(where, "rotation is degenerate (its length is effectively zero)"));
      } else if (std::fabs(norm - 1.0f) > kRotationNormTolerance) {
        findings.push_back(error(
            where, "rotation is not a unit quaternion (length " + short_number(norm) + "); normalise it before use"));
      }
    }
    if (body.bone_index < -1) findings.push_back(error(where, "bone index cannot be below -1"));
    if (skeleton_bone_count && body.bone_index >= *skeleton_bone_count) {
      findings.push_back(error(where, "bone index " + std::to_string(body.bone_index) + " is outside a " +
                                          std::to_string(*skeleton_bone_count) + "-bone skeleton"));
    }
  }

  for (const HkxPhysicsConstraint& constraint : model.constraints) {
    const std::string where = "constraint " + std::to_string(constraint.id);
    if (constraint.id < 0) findings.push_back(error(where, "constraint ids must be non-negative"));
    if (!bodies.contains(constraint.body_a)) {
      findings.push_back(error(where, "references missing body " + std::to_string(constraint.body_a)));
    }
    if (!bodies.contains(constraint.body_b)) {
      findings.push_back(error(where, "references missing body " + std::to_string(constraint.body_b)));
    }
    if (constraint.body_a == constraint.body_b) findings.push_back(error(where, "cannot constrain a body to itself"));
    if (!finite(constraint.min_angle) || !finite(constraint.max_angle)) {
      findings.push_back(error(where, "angle limits must be finite"));
    } else if (constraint.min_angle > constraint.max_angle) {
      findings.push_back(error(where, "minimum angle is greater than maximum angle"));
    }

    check_frame(constraint.frame_a, where, "frame A", findings);
    check_frame(constraint.frame_b, where, "frame B", findings);

    if (constraint.twist_min_angle && constraint.twist_max_angle) {
      const float twist_min = *constraint.twist_min_angle;
      const float twist_max = *constraint.twist_max_angle;
      if (!finite(twist_min) || !finite(twist_max)) {
        findings.push_back(error(where, "twist angle limits must be finite"));
      } else if (twist_min > twist_max) {
        findings.push_back(error(where, "minimum twist angle is greater than maximum twist angle"));
      }
    }
    if (constraint.cone_max_angle) {
      const float cone_max = *constraint.cone_max_angle;
      if (!finite(cone_max) || cone_max < 0.0f) {
        findings.push_back(error(where, "cone half-angle must be a finite non-negative value"));
      }
    }

    check_axis_index(constraint.twist_axis, constraint.frame_a, where, "twist axis", findings);
    check_axis_index(constraint.twist_ref_axis, constraint.frame_a, where, "twist reference axis", findings);
    check_axis_index(constraint.cone_twist_axis, constraint.frame_a, where, "cone twist axis", findings);
    check_axis_index(constraint.cone_ref_axis, constraint.frame_b, where, "cone reference axis", findings);
    check_axis_index(constraint.limit_axis, constraint.frame_a, where, "limit axis", findings);
  }

  std::unordered_set<std::int32_t> bound_bones;
  std::unordered_set<std::int32_t> bound_bodies;
  for (const HkxRagdollBoneBinding& binding : model.bone_bindings) {
    const std::string where = "bone " + std::to_string(binding.bone_index);
    if (!bodies.contains(binding.body_id)) {
      findings.push_back(error(where, "references missing body " + std::to_string(binding.body_id)));
    }
    if (binding.bone_index < 0) findings.push_back(error(where, "bone index must be non-negative"));
    if (skeleton_bone_count && binding.bone_index >= *skeleton_bone_count) {
      findings.push_back(error(where, "is outside a " + std::to_string(*skeleton_bone_count) + "-bone skeleton"));
    }
    if (model.skeleton && binding.bone_index >= 0 &&
        static_cast<std::size_t>(binding.bone_index) >= model.skeleton->bone_names.size()) {
      findings.push_back(error(where, "is outside the " + std::to_string(model.skeleton->bone_names.size()) +
                                          "-bone physics skeleton"));
    }
    if (!bound_bones.insert(binding.bone_index).second) {
      findings.push_back(error(where, "is mapped to more than one rigid body"));
    }
    if (!bound_bodies.insert(binding.body_id).second) {
      findings.push_back(
          warning("body " + std::to_string(binding.body_id), "is mapped to more than one skeleton bone"));
    }
  }

  if (model.skeleton) {
    const HkxSkeleton& skeleton = *model.skeleton;
    const std::size_t bone_count = skeleton.bone_names.size();
    if (skeleton.parent_indices.size() != bone_count) {
      findings.push_back(error("skeleton", "has " + std::to_string(bone_count) + " bone names but " +
                                               std::to_string(skeleton.parent_indices.size()) + " parent indices"));
    }
    if (skeleton.reference_pose.size() != bone_count) {
      findings.push_back(error("skeleton", "has " + std::to_string(bone_count) + " bone names but " +
                                               std::to_string(skeleton.reference_pose.size()) + " reference poses"));
    }
    for (std::size_t i = 0; i < skeleton.parent_indices.size(); ++i) {
      const std::int32_t parent = skeleton.parent_indices[i];
      if (parent < -1 || static_cast<std::int64_t>(parent) >= static_cast<std::int64_t>(bone_count)) {
        findings.push_back(
            error("bone " + std::to_string(i), "parent index " + std::to_string(parent) + " is outside the skeleton"));
      }
    }
    for (std::size_t i = 0; i < skeleton.reference_pose.size(); ++i) {
      const HkxQsTransform& pose = skeleton.reference_pose[i];
      const std::string where = "bone " + std::to_string(i);
      if (!finite(pose.translation) || !finite(pose.rotation)) {
        findings.push_back(error(where, "reference pose must contain only finite values"));
      }
      const float dx = pose.scale[0] - 1.0f;
      const float dy = pose.scale[1] - 1.0f;
      const float dz = pose.scale[2] - 1.0f;
      if (std::sqrt(dx * dx + dy * dy + dz * dz) > kScaleTolerance) {
        findings.push_back(warning(where, "reference pose scale (<" + number(pose.scale[0]) + ", " +
                                              number(pose.scale[1]) + ", " + number(pose.scale[2]) +
                                              ">) is not identity and is not applied"));
      }
    }
  }

  if (model.animation_skeleton) {
    const HkxSkeleton& animation = *model.animation_skeleton;
    const std::size_t bone_count = animation.bone_names.size();
    if (animation.parent_indices.size() != bone_count) {
      findings.push_back(error("animation skeleton", "has " + std::to_string(bone_count) + " bone names but " +
                                                         std::to_string(animation.parent_indices.size()) +
                                                         " parent indices"));
    }
    if (animation.reference_pose.size() != bone_count) {
      findings.push_back(error("animation skeleton", "has " + std::to_string(bone_count) + " bone names but " +
                                                         std::to_string(animation.reference_pose.size()) +
                                                         " reference poses"));
    }
  }

  std::unordered_set<std::int32_t> mapped_physics_bones;
  for (std::size_t i = 0; i < model.mappings.size(); ++i) {
    const HkxRagdollMapping& mapping = model.mappings[i];
    const std::string where = "mapping " + std::to_string(i);
    if (mapping.bone_a < 0 || (model.animation_skeleton &&
                               static_cast<std::size_t>(mapping.bone_a) >= model.animation_skeleton->bone_names.size())) {
      findings.push_back(
          error(where, "animation bone " + std::to_string(mapping.bone_a) + " is outside the animation skeleton"));
    }
    if (mapping.bone_b < 0 ||
        (model.skeleton && static_cast<std::size_t>(mapping.bone_b) >= model.skeleton->bone_names.size())) {
      findings.push_back(
          error(where, "physics bone " + std::to_string(mapping.bone_b) + " is outside the physics skeleton"));
    }
    if (!finite(mapping.a_from_b_translation)) {
      findings.push_back(error(where, "aFromB translation must contain only finite values"));
    }
    if (!finite(mapping.a_from_b_rotation)) {
      findings.push_back(error(where, "aFromB rotation must contain only finite values"));
    } else if (std::sqrt(quat_length_squared(mapping.a_from_b_rotation)) < kRotationNormEpsilon) {
      findings.push_back(error(where, "aFromB rotation is degenerate (its length is effectively zero)"));
    }
    if (!mapped_physics_bones.insert(mapping.bone_b).second) {
      findings.push_back(error(where, "physics bone " + std::to_string(mapping.bone_b) + " is mapped more than once"));
    }
  }

  if (!model.mappings.empty() && model.skeleton && model.animation_skeleton) {
    for (std::size_t bone = 0; bone < model.skeleton->bone_names.size(); ++bone) {
      if (!mapped_physics_bones.contains(static_cast<std::int32_t>(bone))) {
        findings.push_back(warning("bone " + std::to_string(bone),
                                   "is not covered by any animation mapping and stays at its reference frame"));
      }
    }
  }

  return findings;
}

}  // namespace fo4::hkx
