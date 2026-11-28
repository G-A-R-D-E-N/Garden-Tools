#include "fo4/hkx/ragdoll_edit.hpp"

#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"
#include "ragdoll_capsule.hpp"
#include "write_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <utility>

namespace fo4::hkx {

float HkxRagdollBodyMeasure::mass() const {
  return inverse_mass > 0.0f && std::isfinite(inverse_mass) ? 1.0f / inverse_mass : 0.0f;
}

namespace {

using detail::Elements;
using detail::elements_at;
using detail::holds_layout;
using detail::in_section;
using detail::member_field;
using detail::pointer_target;
using detail::value_at;

constexpr std::string_view kCapsuleClass = "hknpCapsuleShape";
constexpr double kPi = 3.14159265358979323846;
constexpr double kAngleSlack = 1.0e-6;
constexpr float kMassMin = 0.0001f;
constexpr float kMassMax = 100000.0f;
constexpr std::size_t kFrameFloats = 16;
// Invariant: the MeasurementMode values of HavokClassTypes.json that the edits are defined for (ZERO_WHEN_VECTORS_ALIGNED and ZERO_WHEN_VECTORS_PERPENDICULAR).
constexpr std::int32_t kConeAtomMode = 0;
constexpr std::int32_t kPlaneAtomMode = 1;

struct Refusal {
  HkxRagdollEditStatus status{HkxRagdollEditStatus::Ok};
  std::string message;
};
using Check = std::optional<Refusal>;

Check refuse(HkxRagdollEditStatus status, std::string message) { return Refusal{status, std::move(message)}; }

std::optional<std::size_t> offset_in(const HkxClassLayout& layout, std::string_view member) {
  const auto offset = find_member_offset(layout, member);
  if (!offset) return std::nullopt;
  return static_cast<std::size_t>(*offset);
}

std::optional<std::size_t> offset_of(std::string_view class_name, std::string_view member) {
  const HkxClassLayout* layout = find_layout(class_name);
  if (!layout) return std::nullopt;
  return offset_in(*layout, member);
}

std::optional<std::size_t> object_at(const HkxPackfile& pack, std::size_t offset) {
  const auto found = std::lower_bound(pack.objects.begin(), pack.objects.end(), offset,
                                      [](const HkxObjectView& object, std::size_t value) { return object.offset < value; });
  if (found == pack.objects.end() || found->offset != offset) return std::nullopt;
  return static_cast<std::size_t>(found - pack.objects.begin());
}

struct Located {
  const HkxPackfile* pack{nullptr};
  const HkxSection* section{nullptr};
  Elements motions;
  Elements bodies;
  Elements constraints;
  Elements bone_map;
  std::size_t body_size{0};
  std::size_t motion_size{0};
  std::size_t constraint_size{0};
  std::size_t body_position{0};
  std::size_t body_orientation{0};
  std::size_t body_motion_id{0};
  std::size_t motion_inverse_mass{0};
  std::size_t motion_inertia{0};
  std::size_t motion_center{0};
  std::size_t cinfo_data{0};
  std::size_t cinfo_body_a{0};
  std::size_t cinfo_body_b{0};
  std::size_t body_shape{0};
  Elements materials;
  std::size_t material_size{0};
  std::size_t material_dynamic{0};
  std::size_t material_static{0};
  std::size_t material_restitution{0};
  std::size_t material_shared{0};
  bool materials_ok{false};
};

// Requires the same layout rows physics.cpp reads, so an edit can only touch fields the reader already proved.
Check locate(const HkxPackfile& pack, Located& out) {
  const auto ragdoll = std::find_if(pack.objects.begin(), pack.objects.end(), [](const HkxObjectView& object) {
    return object.class_name == kPhysicsRagdollClass;
  });
  if (ragdoll == pack.objects.end()) return refuse(HkxRagdollEditStatus::NotARagdoll, "the file holds no hknpRagdollData object");
  if (pack.pointer_size != 8) {
    return refuse(HkxRagdollEditStatus::UnsupportedLayout,
                  "the packfile uses " + std::to_string(pack.pointer_size) + "-byte pointers; only 8-byte pointers are proven");
  }
  if (pack.data_section >= pack.sections.size()) return refuse(HkxRagdollEditStatus::NotARagdoll, "the file has no data section");

  const HkxSection& section = pack.sections[pack.data_section];
  const HkxClassLayout* ragdoll_layout = find_layout(kPhysicsRagdollClass, ragdoll->signature);
  const HkxClassLayout* system = find_layout("hknpPhysicsSystemData");
  const HkxClassLayout* body = find_layout("hknpBodyCinfo");
  const HkxClassLayout* motion = find_layout("hknpMotionCinfo");
  const HkxClassLayout* constraint = find_layout("hknpConstraintCinfo");
  if (!ragdoll_layout || !system || !body || !motion || !constraint || !holds_layout(*ragdoll, *ragdoll_layout)) {
    return refuse(HkxRagdollEditStatus::UnsupportedLayout,
                  "the ragdoll class signature or its cinfo layouts do not match the proven tables");
  }

  const auto position = offset_in(*body, "position");
  const auto orientation = offset_in(*body, "orientation");
  const auto motion_id = offset_in(*body, "motionId");
  const auto inverse_mass = offset_in(*motion, "inverseMass");
  const auto inertia = offset_in(*motion, "inverseInertiaLocal");
  const auto center = offset_in(*motion, "centerOfMassWorld");
  const auto data = offset_in(*constraint, "constraintData");
  const auto body_a = offset_in(*constraint, "bodyA");
  const auto body_b = offset_in(*constraint, "bodyB");
  if (!position || !orientation || !motion_id || !inverse_mass || !inertia || !center || !data || !body_a || !body_b) {
    return refuse(HkxRagdollEditStatus::UnsupportedLayout, "a body, motion or constraint cinfo member is missing from the layout table");
  }

  Located at;
  at.pack = &pack;
  at.section = &section;
  at.body_size = body->size;
  at.motion_size = motion->size;
  at.constraint_size = constraint->size;
  at.body_position = *position;
  at.body_orientation = *orientation;
  at.body_motion_id = *motion_id;
  at.motion_inverse_mass = *inverse_mass;
  at.motion_inertia = *inertia;
  at.motion_center = *center;
  at.cinfo_data = *data;
  at.cinfo_body_a = *body_a;
  at.cinfo_body_b = *body_b;
  const auto body_shape = offset_in(*body, "shape");
  if (!body_shape) return refuse(HkxRagdollEditStatus::UnsupportedLayout, "the body cinfo shape member is missing from the layout table");
  at.body_shape = *body_shape;

  const auto fetch = [&](const HkxClassLayout& layout, std::string_view member, std::size_t element_size,
                         Elements& into) -> Check {
    const auto field = member_field(section, layout, *ragdoll, member);
    if (!field) return std::nullopt;
    if (elements_at(section, *field, pack.pointer_size, element_size, into) != HkxStatus::Ok) {
      return refuse(HkxRagdollEditStatus::UnsupportedLayout, std::string(member) + " lies outside the data section");
    }
    return std::nullopt;
  };
  if (const Check problem = fetch(*system, "motionCinfos", motion->size, at.motions)) return problem;
  if (const Check problem = fetch(*system, "bodyCinfos", body->size, at.bodies)) return problem;
  if (const Check problem = fetch(*system, "constraintCinfos", constraint->size, at.constraints)) return problem;
  if (const Check problem = fetch(*ragdoll_layout, "boneToBodyMap", 4, at.bone_map)) return problem;

  const HkxClassLayout* material = find_layout("hknpMaterial");
  const auto dynamic_friction = material ? offset_in(*material, "dynamicFriction") : std::nullopt;
  const auto static_friction = material ? offset_in(*material, "staticFriction") : std::nullopt;
  const auto restitution_at = material ? offset_in(*material, "restitution") : std::nullopt;
  const auto shared_at = material ? offset_in(*material, "isShared") : std::nullopt;
  if (material && dynamic_friction && static_friction && restitution_at && shared_at) {
    const auto materials_field = member_field(section, *system, *ragdoll, "materials");
    Elements rows;
    if (materials_field && elements_at(section, *materials_field, pack.pointer_size, material->size, rows) == HkxStatus::Ok) {
      at.materials = rows;
      at.material_size = material->size;
      at.material_dynamic = *dynamic_friction;
      at.material_static = *static_friction;
      at.material_restitution = *restitution_at;
      at.material_shared = *shared_at;
      at.materials_ok = true;
    }
  }
  out = at;
  return std::nullopt;
}

std::optional<std::size_t> motion_of(const Located& at, std::size_t body) {
  const std::size_t field = at.bodies.first + body * at.body_size + at.body_motion_id;
  if (!in_section(*at.section, field, 4)) return std::nullopt;
  const std::size_t motion = value_at<std::uint32_t>(at.section->data, field);
  if (motion >= at.motions.count) return std::nullopt;
  return motion;
}

HkxConstraintKind kind_of(std::string_view class_name) {
  if (class_name == "hkpRagdollConstraintData") return HkxConstraintKind::Ragdoll;
  if (class_name == "hkpLimitedHingeConstraintData") return HkxConstraintKind::LimitedHinge;
  if (class_name == "hkpHingeConstraintData") return HkxConstraintKind::Hinge;
  if (class_name == "hkpBallAndSocketConstraintData") return HkxConstraintKind::BallAndSocket;
  if (class_name == "hkpFixedConstraintData") return HkxConstraintKind::Fixed;
  return HkxConstraintKind::Unknown;
}

struct Slots {
  HkxConstraintKind kind{HkxConstraintKind::Unknown};
  std::optional<std::size_t> data_object;
  std::optional<std::size_t> frame_a;
  std::optional<std::size_t> frame_b;
  std::optional<std::size_t> twist_min;
  std::optional<std::size_t> twist_max;
  std::optional<std::size_t> cone_max;
  std::optional<std::size_t> plane_min;
  std::optional<std::size_t> plane_max;
  std::optional<std::size_t> friction;
  std::optional<std::size_t> hinge_min;
  std::optional<std::size_t> hinge_max;
};

// Requires the object view to hold the whole constraint-data class, so a slot never points into a neighbouring object.
Slots slots_for(const Located& at, std::size_t index) {
  Slots slots;
  const HkxPackfile& pack = *at.pack;
  const HkxSection& section = *at.section;
  const std::size_t field = at.constraints.first + index * at.constraint_size + at.cinfo_data;
  if (!in_section(section, field, 8)) return slots;
  const auto target = pointer_target(pack, section, field);
  if (!target) return slots;
  const auto object = object_at(pack, *target);
  if (!object) return slots;

  const HkxObjectView& view = pack.objects[*object];
  slots.data_object = object;
  slots.kind = kind_of(view.class_name);
  if (slots.kind == HkxConstraintKind::Unknown) return slots;

  const std::string class_name(view.class_name);
  const HkxClassLayout* layout = find_layout(class_name);
  if (!layout || !holds_layout(view, *layout)) return slots;
  const auto atoms_offset = offset_in(*layout, "atoms");
  if (!atoms_offset) return slots;
  const std::size_t atoms = view.offset + *atoms_offset;
  const std::string atoms_type = class_name + "Atoms";

  const auto atom = [&](std::string_view name, std::string_view type, std::string_view member) -> std::optional<std::size_t> {
    const auto start = offset_of(atoms_type, name);
    const auto leaf = offset_of(type, member);
    if (!start || !leaf) return std::nullopt;
    return atoms + *start + *leaf;
  };

  slots.frame_a = atom("transforms", "hkpSetLocalTransformsConstraintAtom", "transformA");
  slots.frame_b = atom("transforms", "hkpSetLocalTransformsConstraintAtom", "transformB");
  if (slots.kind == HkxConstraintKind::Ragdoll) {
    slots.twist_min = atom("twistLimit", "hkpTwistLimitConstraintAtom", "minAngle");
    slots.twist_max = atom("twistLimit", "hkpTwistLimitConstraintAtom", "maxAngle");
    slots.cone_max = atom("coneLimit", "hkpConeLimitConstraintAtom", "maxAngle");
    slots.plane_min = atom("planesLimit", "hkpConeLimitConstraintAtom", "minAngle");
    slots.plane_max = atom("planesLimit", "hkpConeLimitConstraintAtom", "maxAngle");
    slots.friction = atom("angFriction", "hkpAngFrictionConstraintAtom", "maxFrictionTorque");
  } else if (slots.kind == HkxConstraintKind::LimitedHinge) {
    slots.hinge_min = atom("angLimit", "hkpAngLimitConstraintAtom", "minAngle");
    slots.hinge_max = atom("angLimit", "hkpAngLimitConstraintAtom", "maxAngle");
    slots.friction = atom("angFriction", "hkpAngFrictionConstraintAtom", "maxFrictionTorque");
  }
  return slots;
}

struct Patch {
  std::vector<std::byte>& bytes;
  const HkxSection& section;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;

  bool put(std::size_t at, const void* from, std::size_t width) {
    if (!in_section(section, at, width)) return false;
    const std::size_t file = section.file_offset + at;
    if (file > bytes.size() || width > bytes.size() - file) return false;
    std::memcpy(bytes.data() + file, from, width);
    ranges.emplace_back(file, file + width);
    return true;
  }
  bool floats(std::size_t at, const float* values, std::size_t count) { return put(at, values, count * sizeof(float)); }
  bool float1(std::size_t at, float value) { return put(at, &value, sizeof value); }
  bool u32(std::size_t at, std::uint32_t value) { return put(at, &value, sizeof value); }
  bool i32(std::size_t at, std::int32_t value) { return put(at, &value, sizeof value); }
};

Check write_failed(const std::string& who) {
  return refuse(HkxRagdollEditStatus::BadTarget, who + " has a field that lies outside the data section");
}

bool finite_all(const float* values, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    if (!std::isfinite(values[i])) return false;
  }
  return true;
}

HkxRigidBody* find_body(HkxRagdollModel& model, std::int32_t id) {
  for (HkxRigidBody& body : model.bodies) {
    if (body.id == id) return &body;
  }
  return nullptr;
}

HkxPhysicsConstraint* find_constraint(HkxRagdollModel& model, std::int32_t id) {
  for (HkxPhysicsConstraint& constraint : model.constraints) {
    if (constraint.id == id) return &constraint;
  }
  return nullptr;
}

HkxRagdollBodyMeasure* find_measure(HkxRagdollMeasures& measures, std::int32_t id) {
  for (HkxRagdollBodyMeasure& measure : measures.bodies) {
    if (measure.body == id) return &measure;
  }
  return nullptr;
}

Check edit_body(const Located& at, Patch& patch, const HkxRagdollBodyEdit& edit, HkxRagdollModel& model,
                HkxRagdollMeasures& measures) {
  const std::string who = "body " + std::to_string(edit.body);
  HkxRigidBody* row = edit.body >= 0 ? find_body(model, edit.body) : nullptr;
  if (!row || static_cast<std::size_t>(edit.body) >= at.bodies.count) {
    return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a body of this ragdoll");
  }
  const std::size_t base = at.bodies.first + static_cast<std::size_t>(edit.body) * at.body_size;

  if (edit.position) {
    if (!finite_all(edit.position->data(), 3)) return refuse(HkxRagdollEditStatus::BadValue, who + " position must be finite");
    if (!patch.floats(base + at.body_position, edit.position->data(), 3)) return write_failed(who);
    row->position = *edit.position;
  }
  if (edit.rotation) {
    if (!finite_all(edit.rotation->data(), 4)) return refuse(HkxRagdollEditStatus::BadValue, who + " rotation must be finite");
    const double length = std::sqrt(static_cast<double>((*edit.rotation)[0]) * (*edit.rotation)[0] +
                                    static_cast<double>((*edit.rotation)[1]) * (*edit.rotation)[1] +
                                    static_cast<double>((*edit.rotation)[2]) * (*edit.rotation)[2] +
                                    static_cast<double>((*edit.rotation)[3]) * (*edit.rotation)[3]);
    if (!(length > 0.0)) return refuse(HkxRagdollEditStatus::BadValue, who + " rotation must not be zero");
    const auto unit = detail::normalised_quaternion(*edit.rotation);
    if (!unit) return refuse(HkxRagdollEditStatus::BadValue, who + " rotation cannot be normalised");
    if (!patch.floats(base + at.body_orientation, unit->data(), 4)) return write_failed(who);
    row->rotation = *unit;
  }
  if (!edit.center_of_mass && !edit.mass) return std::nullopt;

  const auto motion = motion_of(at, static_cast<std::size_t>(edit.body));
  HkxRagdollBodyMeasure* measure = find_measure(measures, edit.body);
  if (!motion || !measure) return refuse(HkxRagdollEditStatus::BadTarget, who + " names no motion row to edit");
  std::size_t users = 0;
  for (std::size_t other = 0; other < at.bodies.count; ++other) {
    if (motion_of(at, other) == motion) ++users;
  }
  if (users > 1) {
    return refuse(HkxRagdollEditStatus::SharedStorage,
                  who + " shares motion " + std::to_string(*motion) + " with another body, so its mass or center of mass was not changed");
  }
  const std::size_t motion_base = at.motions.first + *motion * at.motion_size;

  if (edit.center_of_mass) {
    if (!finite_all(edit.center_of_mass->data(), 3)) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " center of mass must be finite");
    }
    if (!patch.floats(motion_base + at.motion_center, edit.center_of_mass->data(), 3)) return write_failed(who);
    row->center_of_mass = *edit.center_of_mass;
    measure->center_of_mass = *edit.center_of_mass;
  }
  if (edit.mass) {
    const float mass = *edit.mass;
    if (!std::isfinite(mass) || mass < kMassMin || mass > kMassMax) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " mass must be within " + std::to_string(kMassMin) + " and " +
                                                       std::to_string(kMassMax));
    }
    if (!(measure->inverse_mass > 0.0f) || !std::isfinite(measure->inverse_mass)) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " stores no usable inverse mass, so its inertia cannot be scaled");
    }
    const float inverse = 1.0f / mass;
    const float ratio = inverse / measure->inverse_mass;
    HkxVec3 inertia{measure->inverse_inertia[0] * ratio, measure->inverse_inertia[1] * ratio,
                    measure->inverse_inertia[2] * ratio};
    if (!std::isfinite(ratio) || !finite_all(inertia.data(), 3)) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " mass would give a non-finite inertia");
    }
    if (!patch.float1(motion_base + at.motion_inverse_mass, inverse) ||
        !patch.floats(motion_base + at.motion_inertia, inertia.data(), 3)) {
      return write_failed(who);
    }
    measure->inverse_mass = inverse;
    measure->inverse_inertia = inertia;
  }
  return std::nullopt;
}

bool angle_in(float value, double low, double high) {
  return std::isfinite(value) && value >= low - kAngleSlack && value <= high + kAngleSlack;
}

Check edit_angle_pair(Patch& patch, const std::string& who, const char* label, const std::optional<float>& new_min,
                      const std::optional<float>& new_max, std::optional<float>& row_min, std::optional<float>& row_max,
                      std::size_t min_slot, std::size_t max_slot, const Check& gate = std::nullopt) {
  const float low = new_min.value_or(*row_min);
  const float high = new_max.value_or(*row_max);
  if (!angle_in(low, -kPi, kPi) || !angle_in(high, -kPi, kPi)) {
    return refuse(HkxRagdollEditStatus::BadValue, who + " " + label + " limits must lie within -pi and pi");
  }
  if (low > high) return refuse(HkxRagdollEditStatus::BadValue, who + " " + label + " minimum must not exceed its maximum");
  if (gate) return gate;
  if (new_min && !patch.float1(min_slot, low)) return write_failed(who);
  if (new_max && !patch.float1(max_slot, high)) return write_failed(who);
  if (new_min) row_min = low;
  if (new_max) row_max = high;
  return std::nullopt;
}

// Requires the stored mode to equal the one the edit is defined for, so a limit is never written in another angle measurement.
Check require_angle_mode(const std::string& who, const char* atom, const std::optional<std::int32_t>& stored, std::int32_t wanted) {
  if (stored == wanted) return std::nullopt;
  const std::string found = stored ? who + " stores " + atom + " angle measurement mode " + std::to_string(*stored)
                                   : who + " " + atom + " angle measurement mode is unreadable";
  return refuse(HkxRagdollEditStatus::AngleModeUnsupported,
                found + ", where the " + atom + " edit is defined for mode " + std::to_string(wanted) + ", so the " + atom +
                    " limit was not changed");
}

Check edit_constraint(const Located& at, Patch& patch, const HkxRagdollConstraintEdit& edit,
                      const std::vector<Slots>& all, HkxRagdollModel& model) {
  const std::string who = "constraint " + std::to_string(edit.constraint);
  HkxPhysicsConstraint* row = edit.constraint >= 0 ? find_constraint(model, edit.constraint) : nullptr;
  if (!row || static_cast<std::size_t>(edit.constraint) >= at.constraints.count) {
    return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a constraint of this ragdoll");
  }
  const auto index = static_cast<std::size_t>(edit.constraint);
  const Slots& slots = all[index];
  const std::size_t base = at.constraints.first + index * at.constraint_size;

  const auto body_ok = [&](std::int32_t body) { return body >= 0 && static_cast<std::size_t>(body) < at.bodies.count; };
  if (edit.body_a || edit.body_b) {
    const std::int32_t a = edit.body_a.value_or(row->body_a);
    const std::int32_t b = edit.body_b.value_or(row->body_b);
    if (!body_ok(a) || !body_ok(b)) return refuse(HkxRagdollEditStatus::BadValue, who + " must join two existing bodies");
    if (a == b) return refuse(HkxRagdollEditStatus::BadValue, who + " must join two different bodies");
    if (edit.body_a && !patch.u32(base + at.cinfo_body_a, static_cast<std::uint32_t>(a))) return write_failed(who);
    if (edit.body_b && !patch.u32(base + at.cinfo_body_b, static_cast<std::uint32_t>(b))) return write_failed(who);
    row->body_a = a;
    row->body_b = b;
  }

  const bool touches_data = edit.twist_min_angle || edit.twist_max_angle || edit.cone_max_angle || edit.min_angle ||
                            edit.max_angle || edit.frame_a || edit.frame_b || edit.plane_min_angle || edit.plane_max_angle ||
                            edit.friction;
  if (!touches_data) return std::nullopt;
  if (slots.data_object) {
    std::size_t users = 0;
    for (const Slots& other : all) {
      if (other.data_object == slots.data_object) ++users;
    }
    if (users > 1) {
      return refuse(HkxRagdollEditStatus::SharedStorage,
                    who + " shares its constraint data with another constraint, so its limits and frames were not changed");
    }
  }

  if (edit.twist_min_angle || edit.twist_max_angle) {
    if (!slots.twist_min || !slots.twist_max || !row->twist_min_angle || !row->twist_max_angle) {
      return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no twist limit to edit");
    }
    if (const Check problem = edit_angle_pair(patch, who, "twist", edit.twist_min_angle, edit.twist_max_angle,
                                              row->twist_min_angle, row->twist_max_angle, *slots.twist_min,
                                              *slots.twist_max)) {
      return problem;
    }
  }
  if (edit.cone_max_angle) {
    if (!slots.cone_max || !row->cone_max_angle) return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no cone limit to edit");
    if (!angle_in(*edit.cone_max_angle, 0.0, kPi)) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " cone limit must lie within 0 and pi");
    }
    if (const Check problem = require_angle_mode(who, "cone", row->cone_angle_mode, kConeAtomMode)) return problem;
    if (!patch.float1(*slots.cone_max, *edit.cone_max_angle)) return write_failed(who);
    row->cone_max_angle = *edit.cone_max_angle;
  }
  if (edit.plane_min_angle || edit.plane_max_angle) {
    if (!slots.plane_min || !slots.plane_max || !row->plane_min_angle || !row->plane_max_angle) {
      return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no plane limit to edit");
    }
    if (const Check problem = edit_angle_pair(patch, who, "plane", edit.plane_min_angle, edit.plane_max_angle,
                                              row->plane_min_angle, row->plane_max_angle, *slots.plane_min,
                                              *slots.plane_max,
                                              require_angle_mode(who, "plane", row->plane_angle_mode, kPlaneAtomMode))) {
      return problem;
    }
  }
  if (edit.friction) {
    if (!slots.friction || !row->friction) return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no angular friction to edit");
    if (!std::isfinite(*edit.friction) || *edit.friction < 0.0f) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " friction must be finite and not negative");
    }
    if (!patch.float1(*slots.friction, *edit.friction)) return write_failed(who);
    row->friction = *edit.friction;
  }
  if (edit.min_angle || edit.max_angle) {
    if (!slots.hinge_min || !slots.hinge_max) return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no hinge limit to edit");
    std::optional<float> low = row->min_angle;
    std::optional<float> high = row->max_angle;
    if (const Check problem = edit_angle_pair(patch, who, "hinge", edit.min_angle, edit.max_angle, low, high,
                                              *slots.hinge_min, *slots.hinge_max)) {
      return problem;
    }
    row->min_angle = *low;
    row->max_angle = *high;
  }

  const auto frame = [&](const std::optional<std::vector<float>>& value, const std::optional<std::size_t>& slot,
                         std::optional<std::vector<float>>& target, const char* label) -> Check {
    if (!value) return std::nullopt;
    if (!slot || !target) return refuse(HkxRagdollEditStatus::FieldAbsent, who + " has no " + label + " to edit");
    if (value->size() != kFrameFloats || !finite_all(value->data(), value->size())) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " " + label + " must hold sixteen finite floats");
    }
    if (!patch.floats(*slot, value->data(), value->size())) return write_failed(who);
    target = *value;
    return std::nullopt;
  };
  if (const Check problem = frame(edit.frame_a, slots.frame_a, row->frame_a, "frame A")) return problem;
  if (const Check problem = frame(edit.frame_b, slots.frame_b, row->frame_b, "frame B")) return problem;
  return std::nullopt;
}

Check edit_bone(const Located& at, Patch& patch, const HkxRagdollBoneEdit& edit, HkxRagdollModel& model) {
  const std::string who = "bone " + std::to_string(edit.bone);
  if (edit.bone < 0 || static_cast<std::size_t>(edit.bone) >= at.bone_map.count ||
      static_cast<std::size_t>(edit.bone) >= model.bone_bindings.size()) {
    return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a slot of the bone-to-body map");
  }
  if (edit.body < 0 || static_cast<std::size_t>(edit.body) >= at.bodies.count) {
    return refuse(HkxRagdollEditStatus::BadValue, who + " must be bound to an existing body");
  }
  if (!patch.i32(at.bone_map.first + static_cast<std::size_t>(edit.bone) * 4, edit.body)) return write_failed(who);
  model.bone_bindings[static_cast<std::size_t>(edit.bone)].body_id = edit.body;
  return std::nullopt;
}

HkxPhysicsShape* find_shape(HkxRagdollModel& model, std::int32_t id) {
  for (HkxPhysicsShape& shape : model.shapes) {
    if (shape.id == id) return &shape;
  }
  return nullptr;
}

HkxRagdollCapsuleMeasure* find_capsule(HkxRagdollMeasures& measures, std::int32_t id) {
  for (HkxRagdollCapsuleMeasure& capsule : measures.capsules) {
    if (capsule.shape == id) return &capsule;
  }
  return nullptr;
}

Check edit_shape(const Located& at, Patch& patch, const HkxRagdollShapeEdit& edit, HkxRagdollModel& model,
                 HkxRagdollMeasures& measures) {
  const std::string who = "shape " + std::to_string(edit.shape);
  const HkxPackfile& pack = *at.pack;
  if (edit.shape < 0 || static_cast<std::size_t>(edit.shape) >= pack.objects.size()) {
    return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a shape of this ragdoll");
  }
  const auto object = static_cast<std::size_t>(edit.shape);
  if (pack.objects[object].class_name != kCapsuleClass) {
    return refuse(HkxRagdollEditStatus::ShapeGeometryUnsupported, who + " is not a capsule; only capsule geometry is editable");
  }
  HkxPhysicsShape* shape = find_shape(model, edit.shape);
  HkxRagdollCapsuleMeasure* capsule = find_capsule(measures, edit.shape);
  if (!shape || !capsule) return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a capsule of this ragdoll");

  std::vector<std::size_t> owners;
  for (std::size_t body = 0; body < at.bodies.count; ++body) {
    const std::size_t field = at.bodies.first + body * at.body_size + at.body_shape;
    if (in_section(*at.section, field, 8) && pointer_target(pack, *at.section, field) == pack.objects[object].offset) {
      owners.push_back(body);
    }
  }
  if (owners.size() > 1) {
    return refuse(HkxRagdollEditStatus::SharedStorage, who + " is the shape of " + std::to_string(owners.size()) +
                                                           " bodies, so its geometry was not changed");
  }

  detail::CapsuleChange change;
  if (const auto problem = detail::derive_capsule(pack, object, edit.a, edit.b, edit.radius, change)) {
    return refuse(problem->shared ? HkxRagdollEditStatus::SharedStorage : HkxRagdollEditStatus::ShapeGeometryUnsupported,
                  who + ": " + problem->reason);
  }

  if (!change.writes.empty() && owners.size() == 1) {
    const auto motion = motion_of(at, owners.front());
    HkxRagdollBodyMeasure* body = find_measure(measures, static_cast<std::int32_t>(owners.front()));
    if (!motion || !body) return refuse(HkxRagdollEditStatus::BadTarget, who + " owner names no motion row to update");
    for (std::size_t other = 0; other < at.bodies.count; ++other) {
      if (other != owners.front() && motion_of(at, other) == motion) {
        return refuse(HkxRagdollEditStatus::SharedStorage, who + " owner shares motion " + std::to_string(*motion) +
                                                               " with another body, so its inertia was not rescaled");
      }
    }
    HkxVec3 inertia{};
    for (std::size_t lane = 0; lane < 3; ++lane) {
      inertia[lane] = static_cast<float>(static_cast<double>(body->inverse_inertia[lane]) * change.inertia_ratio[lane]);
    }
    if (!finite_all(inertia.data(), 3)) return refuse(HkxRagdollEditStatus::BadValue, who + " would give a non-finite inertia");
    const std::size_t motion_base = at.motions.first + *motion * at.motion_size;
    if (!patch.floats(motion_base + at.motion_inertia, inertia.data(), 3)) return write_failed(who);
    body->inverse_inertia = inertia;
  }
  for (const detail::ByteWrite& write : change.writes) {
    if (!patch.put(write.at, write.bytes.data(), write.bytes.size())) return write_failed(who);
  }
  shape->vertices = change.vertices;
  capsule->a = change.a;
  capsule->b = change.b;
  capsule->radius = change.radius;
  return std::nullopt;
}

constexpr float kHalfMax = 65504.0f;

float half_to_float(std::uint16_t half) {
  const std::uint32_t bits = static_cast<std::uint32_t>(half) << 16;
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

std::uint16_t float_to_half(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  return static_cast<std::uint16_t>(bits >> 16);
}

Check edit_material(const Located& at, Patch& patch, const HkxRagdollMaterialEdit& edit, HkxRagdollMeasures& measures,
                    std::string& notes) {
  const std::string who = "material " + std::to_string(edit.material);
  if (!at.materials_ok || edit.material < 0 || static_cast<std::size_t>(edit.material) >= at.materials.count ||
      static_cast<std::size_t>(edit.material) >= measures.materials.size()) {
    return refuse(HkxRagdollEditStatus::BadTarget, who + " is not a material of this ragdoll");
  }
  if (!edit.dynamic_friction && !edit.static_friction && !edit.restitution) {
    return refuse(HkxRagdollEditStatus::BadValue, who + " names no friction or restitution to change");
  }
  const auto index = static_cast<std::size_t>(edit.material);
  HkxRagdollMaterialMeasure& row = measures.materials[index];
  if (row.shared && !edit.allow_shared) {
    return refuse(HkxRagdollEditStatus::SharedStorage,
                  who + " is a shared material, so it was not changed; allow_shared changes it for every body that uses it");
  }
  const std::size_t base = at.materials.first + index * at.material_size;

  struct Field {
    const std::optional<float>& value;
    std::size_t offset;
    float& target;
    const char* label;
  };
  const Field fields[] = {{edit.dynamic_friction, at.material_dynamic, row.dynamic_friction, "dynamic friction"},
                          {edit.static_friction, at.material_static, row.static_friction, "static friction"},
                          {edit.restitution, at.material_restitution, row.restitution, "restitution"}};
  for (const Field& field : fields) {
    if (!field.value) continue;
    if (!std::isfinite(*field.value) || *field.value < 0.0f || *field.value > kHalfMax) {
      return refuse(HkxRagdollEditStatus::BadValue, who + " " + field.label + " must be between 0 and 65504");
    }
    const std::uint16_t half = float_to_half(*field.value);
    if (!patch.put(base + field.offset, &half, sizeof half)) return write_failed(who);
    field.target = half_to_float(half);
    notes += (notes.empty() ? "" : ", ") + who + " " + field.label + " stored as " + std::to_string(field.target);
  }
  return std::nullopt;
}

template <typename Edit, typename Key>
Check unique_targets(const std::vector<Edit>& edits, Key key, const char* label) {
  std::set<std::int32_t> seen;
  for (const Edit& edit : edits) {
    if (!seen.insert(key(edit)).second) {
      return refuse(HkxRagdollEditStatus::BadValue, std::string(label) + " " + std::to_string(key(edit)) + " is edited twice");
    }
  }
  return std::nullopt;
}

bool bodies_equal(const HkxRigidBody& a, const HkxRigidBody& b) {
  return a.id == b.id && a.name == b.name && a.shape_id == b.shape_id && a.bone_index == b.bone_index && a.mass == b.mass &&
         a.center_of_mass == b.center_of_mass && a.position == b.position && a.rotation == b.rotation;
}

// Constraint: floats compare by bit pattern, so a stored NaN equals itself and an untouched constraint never fails the read-back.
bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

bool same_bits(const std::optional<float>& a, const std::optional<float>& b) {
  return a.has_value() == b.has_value() && (!a || same_bits(*a, *b));
}

bool same_bits(const std::optional<std::vector<float>>& a, const std::optional<std::vector<float>>& b) {
  if (a.has_value() != b.has_value()) return false;
  return !a || (a->size() == b->size() && (a->empty() || std::memcmp(a->data(), b->data(), a->size() * sizeof(float)) == 0));
}

bool constraints_equal(const HkxPhysicsConstraint& a, const HkxPhysicsConstraint& b) {
  return a.id == b.id && a.kind == b.kind && a.body_a == b.body_a && a.body_b == b.body_b &&
         same_bits(a.min_angle, b.min_angle) && same_bits(a.max_angle, b.max_angle) &&
         same_bits(a.twist_min_angle, b.twist_min_angle) && same_bits(a.twist_max_angle, b.twist_max_angle) &&
         same_bits(a.cone_max_angle, b.cone_max_angle) && a.twist_axis == b.twist_axis && a.twist_ref_axis == b.twist_ref_axis &&
         a.cone_twist_axis == b.cone_twist_axis && a.cone_ref_axis == b.cone_ref_axis && a.limit_axis == b.limit_axis &&
         a.has_preview_limits == b.has_preview_limits && same_bits(a.frame_a, b.frame_a) && same_bits(a.frame_b, b.frame_b) &&
         a.cone_angle_mode == b.cone_angle_mode && a.plane_angle_mode == b.plane_angle_mode &&
         same_bits(a.plane_min_angle, b.plane_min_angle) && same_bits(a.plane_max_angle, b.plane_max_angle) &&
         same_bits(a.friction, b.friction);
}

bool shapes_equal(const HkxPhysicsShape& a, const HkxPhysicsShape& b) {
  return a.id == b.id && a.kind == b.kind && a.half_extents == b.half_extents && a.radius == b.radius &&
         a.children == b.children && a.vertices == b.vertices && a.face_rings == b.face_rings;
}

template <typename T, typename Same>
bool lists_equal(const std::vector<T>& a, const std::vector<T>& b, Same same) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same);
}

bool models_equal(const HkxRagdollModel& a, const HkxRagdollModel& b) {
  const auto bindings_same = [](const HkxRagdollBoneBinding& x, const HkxRagdollBoneBinding& y) {
    return x.bone_index == y.bone_index && x.body_id == y.body_id;
  };
  return lists_equal(a.shapes, b.shapes, shapes_equal) && lists_equal(a.bodies, b.bodies, bodies_equal) &&
         lists_equal(a.constraints, b.constraints, constraints_equal) &&
         lists_equal(a.bone_bindings, b.bone_bindings, bindings_same);
}

std::set<std::pair<std::string, std::string>> error_findings(const HkxRagdollModel& model) {
  std::set<std::pair<std::string, std::string>> out;
  for (const HkxPhysicsValidationFinding& finding : check_physics_model(model)) {
    if (finding.level == HkxPhysicsValidationLevel::Error) out.emplace(finding.where, finding.message);
  }
  return out;
}

HkxRagdollEditResult failed(const Refusal& refusal) {
  HkxRagdollEditResult result;
  result.status = refusal.status;
  result.message = refusal.message;
  return result;
}

HkxRagdollEditResult failed(HkxRagdollEditStatus status, std::string message) {
  return failed(Refusal{status, std::move(message)});
}

}  // namespace

const char* ragdoll_edit_status_name(HkxRagdollEditStatus status) {
  switch (status) {
    case HkxRagdollEditStatus::Ok: return "ok";
    case HkxRagdollEditStatus::NotARagdoll: return "not_a_ragdoll";
    case HkxRagdollEditStatus::UnsupportedLayout: return "unsupported_layout";
    case HkxRagdollEditStatus::BadTarget: return "bad_target";
    case HkxRagdollEditStatus::BadValue: return "bad_value";
    case HkxRagdollEditStatus::FieldAbsent: return "field_absent";
    case HkxRagdollEditStatus::SharedStorage: return "shared_storage";
    case HkxRagdollEditStatus::ShapeGeometryUnsupported: return "shape_geometry_unsupported";
    case HkxRagdollEditStatus::VerifyFailed: return "verify_failed";
    case HkxRagdollEditStatus::AngleModeUnsupported: return "angle_mode_unsupported";
  }
  return "unknown";
}

HkxStatus read_ragdoll_measures(const HkxPackfile& packfile, HkxRagdollMeasures& out) {
  out = HkxRagdollMeasures{};
  Located at;
  if (const Check problem = locate(packfile, at)) {
    return problem->status == HkxRagdollEditStatus::NotARagdoll ? HkxStatus::MissingObject : HkxStatus::UnsupportedLayout;
  }
  const HkxSection& section = *at.section;

  for (std::size_t i = 0; i < at.bodies.count; ++i) {
    HkxRagdollBodyMeasure measure;
    measure.body = static_cast<std::int32_t>(i);
    if (const auto motion = motion_of(at, i)) {
      measure.motion = static_cast<std::int32_t>(*motion);
      const std::size_t base = at.motions.first + *motion * at.motion_size;
      if (in_section(section, base + at.motion_inverse_mass, 4)) {
        measure.inverse_mass = value_at<float>(section.data, base + at.motion_inverse_mass);
      }
      if (in_section(section, base + at.motion_inertia, 12)) {
        for (std::size_t lane = 0; lane < 3; ++lane) {
          measure.inverse_inertia[lane] = value_at<float>(section.data, base + at.motion_inertia + lane * 4);
        }
      }
      if (in_section(section, base + at.motion_center, 12)) {
        for (std::size_t lane = 0; lane < 3; ++lane) {
          measure.center_of_mass[lane] = value_at<float>(section.data, base + at.motion_center + lane * 4);
        }
      }
    }
    out.bodies.push_back(measure);
  }

  if (at.materials_ok) {
    for (std::size_t i = 0; i < at.materials.count; ++i) {
      const std::size_t base = at.materials.first + i * at.material_size;
      if (!in_section(section, base, at.material_size)) break;
      HkxRagdollMaterialMeasure measure;
      measure.material = static_cast<std::int32_t>(i);
      measure.dynamic_friction = half_to_float(value_at<std::uint16_t>(section.data, base + at.material_dynamic));
      measure.static_friction = half_to_float(value_at<std::uint16_t>(section.data, base + at.material_static));
      measure.restitution = half_to_float(value_at<std::uint16_t>(section.data, base + at.material_restitution));
      measure.shared = value_at<std::uint8_t>(section.data, base + at.material_shared) != 0;
      out.materials.push_back(measure);
    }
  }

  const HkxClassLayout* capsule = find_layout(kCapsuleClass);
  const auto a_at = capsule ? offset_in(*capsule, "a") : std::nullopt;
  const auto b_at = capsule ? offset_in(*capsule, "b") : std::nullopt;
  const auto radius_at = offset_of("hknpShape", "convexRadius");
  if (!capsule || !a_at || !b_at || !radius_at) return HkxStatus::Ok;
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    const HkxObjectView& object = packfile.objects[i];
    if (object.class_name != kCapsuleClass || !holds_layout(object, *capsule)) continue;
    HkxRagdollCapsuleMeasure measure;
    measure.shape = static_cast<std::int32_t>(i);
    for (std::size_t lane = 0; lane < 3; ++lane) {
      measure.a[lane] = value_at<float>(object.data, *a_at + lane * 4);
      measure.b[lane] = value_at<float>(object.data, *b_at + lane * 4);
    }
    measure.radius = value_at<float>(object.data, *radius_at);
    if (const auto problem = detail::check_capsule(packfile, i)) {
      measure.reason = problem->reason;
    } else {
      measure.editable = true;
    }
    out.capsules.push_back(measure);
  }
  return HkxStatus::Ok;
}

HkxRagdollEditResult apply_ragdoll_edits(std::span<const std::byte> source, const HkxRagdollEdits& edits) {
  if (edits.bodies.empty() && edits.constraints.empty() && edits.bone_map.empty() && edits.shapes.empty() &&
      edits.materials.empty()) {
    return failed(HkxRagdollEditStatus::BadValue, "no edits were requested");
  }
  if (const Check problem = unique_targets(edits.bodies, [](const HkxRagdollBodyEdit& e) { return e.body; }, "body")) {
    return failed(*problem);
  }
  if (const Check problem =
          unique_targets(edits.constraints, [](const HkxRagdollConstraintEdit& e) { return e.constraint; }, "constraint")) {
    return failed(*problem);
  }
  if (const Check problem = unique_targets(edits.bone_map, [](const HkxRagdollBoneEdit& e) { return e.bone; }, "bone")) {
    return failed(*problem);
  }
  if (const Check problem = unique_targets(edits.shapes, [](const HkxRagdollShapeEdit& e) { return e.shape; }, "shape")) {
    return failed(*problem);
  }
  if (const Check problem =
          unique_targets(edits.materials, [](const HkxRagdollMaterialEdit& e) { return e.material; }, "material")) {
    return failed(*problem);
  }

  HkxPackfile pack;
  if (read_packfile(source, pack) != HkxStatus::Ok) {
    return failed(HkxRagdollEditStatus::NotARagdoll, "the bytes are not a readable Havok packfile");
  }
  HkxRagdollModel before;
  HkxPhysicsLoadReport report;
  if (read_ragdoll(pack, before, report) != HkxStatus::Ok || before.bodies.empty()) {
    return failed(HkxRagdollEditStatus::NotARagdoll, "the file holds no usable ragdoll: " + report.headline());
  }
  HkxRagdollMeasures measures;
  if (read_ragdoll_measures(pack, measures) != HkxStatus::Ok) {
    return failed(HkxRagdollEditStatus::UnsupportedLayout, "the ragdoll rows could not be located in the data section");
  }
  Located at;
  if (const Check problem = locate(pack, at)) return failed(*problem);

  std::vector<Slots> slots;
  slots.reserve(at.constraints.count);
  for (std::size_t i = 0; i < at.constraints.count; ++i) slots.push_back(slots_for(at, i));

  std::vector<std::byte> patched(source.begin(), source.end());
  Patch patch{patched, *at.section, {}};
  HkxRagdollModel expected = before;
  HkxRagdollMeasures expected_measures = measures;

  for (const HkxRagdollBodyEdit& edit : edits.bodies) {
    if (const Check problem = edit_body(at, patch, edit, expected, expected_measures)) return failed(*problem);
  }
  for (const HkxRagdollConstraintEdit& edit : edits.constraints) {
    if (const Check problem = edit_constraint(at, patch, edit, slots, expected)) return failed(*problem);
  }
  for (const HkxRagdollBoneEdit& edit : edits.bone_map) {
    if (const Check problem = edit_bone(at, patch, edit, expected)) return failed(*problem);
  }
  for (const HkxRagdollShapeEdit& edit : edits.shapes) {
    if (const Check problem = edit_shape(at, patch, edit, expected, expected_measures)) return failed(*problem);
  }
  std::string notes;
  for (const HkxRagdollMaterialEdit& edit : edits.materials) {
    if (const Check problem = edit_material(at, patch, edit, expected_measures, notes)) return failed(*problem);
  }

  // Constraint: this only proves every changed byte lies in a range the patch recorded; it does not prove the ranges are the intended fields.
  std::vector<std::byte> outside = patched;
  for (const auto& [begin, end] : patch.ranges) {
    std::copy(source.begin() + static_cast<std::ptrdiff_t>(begin), source.begin() + static_cast<std::ptrdiff_t>(end),
              outside.begin() + static_cast<std::ptrdiff_t>(begin));
  }
  if (outside.size() != source.size() || !std::equal(outside.begin(), outside.end(), source.begin())) {
    return failed(HkxRagdollEditStatus::VerifyFailed, "the edit changed bytes outside the ranges it recorded");
  }

  HkxPackfile reopened;
  HkxRagdollModel after;
  HkxPhysicsLoadReport after_report;
  HkxRagdollMeasures after_measures;
  if (read_packfile(patched, reopened) != HkxStatus::Ok || read_ragdoll(reopened, after, after_report) != HkxStatus::Ok ||
      read_ragdoll_measures(reopened, after_measures) != HkxStatus::Ok) {
    return failed(HkxRagdollEditStatus::VerifyFailed, "the edited file could not be read back");
  }
  if (!models_equal(after, expected) || after_measures.bodies != expected_measures.bodies ||
      after_measures.capsules != expected_measures.capsules || after_measures.materials != expected_measures.materials) {
    return failed(HkxRagdollEditStatus::VerifyFailed, "the edited file does not read back as the requested edits");
  }
  for (const HkxRagdollShapeEdit& edit : edits.shapes) {
    if (const auto problem = detail::check_capsule(reopened, static_cast<std::size_t>(edit.shape))) {
      return failed(HkxRagdollEditStatus::VerifyFailed,
                    "the edited capsule " + std::to_string(edit.shape) + " does not match the capsule model: " + problem->reason);
    }
  }
  const auto known = error_findings(before);
  for (const auto& finding : error_findings(after)) {
    if (!known.contains(finding)) {
      return failed(HkxRagdollEditStatus::VerifyFailed,
                    "the edit would introduce a validation error at " + finding.first + ": " + finding.second);
    }
  }

  HkxRagdollEditResult result;
  result.message = "applied " +
                   std::to_string(edits.bodies.size() + edits.constraints.size() + edits.bone_map.size() +
                                  edits.shapes.size() + edits.materials.size()) +
                   " edit(s)" + (notes.empty() ? "" : "; " + notes);
  result.bytes = std::move(patched);
  return result;
}

}  // namespace fo4::hkx
