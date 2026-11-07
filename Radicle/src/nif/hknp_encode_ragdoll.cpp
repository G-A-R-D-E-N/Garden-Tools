#include "fo4/nif/hknp_encode_ragdoll.hpp"

#include "fo4/nif/hknp_encode_constraints.hpp"
#include "hknp_encode_internal.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using namespace hknp_detail;

constexpr std::size_t k_root_header = 0x90;
constexpr std::size_t k_skeleton_header = 0x88;
constexpr std::size_t k_material_stride = 0x50;
constexpr std::size_t k_motion_stride = 0x40;
constexpr std::size_t k_inertia_stride = 0x70;
constexpr std::size_t k_cinfo_stride = 0x60;
constexpr std::size_t k_binding_stride = 0x18;
constexpr std::size_t k_shape_ref_stride = 8;
constexpr std::size_t k_link_stride = 4;
constexpr std::size_t k_bone_stride = 16;
constexpr std::size_t k_pose_stride = 48;
constexpr std::array<std::size_t, 4> k_empty_skeleton_arrays{{0x48, 0x58, 0x68, 0x78}};

std::uint32_t to_u32(std::size_t value) {
  if (value > 0xFFFFFFFFU) throw std::invalid_argument("packfile offset or index does not fit in 32 bits");
  return static_cast<std::uint32_t>(value);
}

// Constraint: friction and restitution are the upper half of a float32, rounded to nearest even.
std::uint16_t bfloat16_bits(float value) {
  std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  bits += 0x7fffU + ((bits >> 16) & 1U);
  return static_cast<std::uint16_t>(bits >> 16);
}

void put_material(HknpPackObject& root, std::size_t at, const HknpBody& body) {
  put_u32(root, at + 0x0c, body.material_flags);
  put_u8(root, at + 0x10, body.trigger_type);
  put_u8(root, at + 0x11, 0xff);
  put_u16(root, at + 0x12, bfloat16_bits(body.friction));
  put_u16(root, at + 0x14, bfloat16_bits(body.static_friction));
  put_u16(root, at + 0x16, bfloat16_bits(body.restitution));
  put_u8(root, at + 0x18, 1);
  put_u8(root, at + 0x19, 2);
  put_u16(root, at + 0x1a, 0x3d4c);
  put_u32(root, at + 0x1c, 0x7f7fffeeU);
  put_f32(root, at + 0x20, 1.0f);
  put_u16(root, at + 0x26, 0x3f80);
  put_u16(root, at + 0x38, 0x40a0);
}

void put_motion(HknpPackObject& root, std::size_t at, const HknpDynMotion& motion) {
  for (std::size_t k = 0; k < k_measured_motion.size(); ++k) put_u8(root, at + k, k_measured_motion[k]);
  put_f32(root, at + 0x08, motion.gravity_factor);
  put_f32(root, at + 0x10, motion.max_linear_velocity);
  put_f32(root, at + 0x14, motion.max_angular_velocity);
  put_f32(root, at + 0x18, motion.linear_damping);
  put_f32(root, at + 0x1c, motion.angular_damping);
}

void put_inertia(HknpPackObject& root, std::size_t at, const HknpDynInertia& inertia) {
  put_u16(root, at, inertia.motion_properties_id);
  put_u8(root, at + 0x02, 1);
  put_f32(root, at + 0x04, inertia.inverse_mass);
  put_f32(root, at + 0x08, inertia.density);
  put_u32(root, at + 0x0c, k_inertia_stamp);
  put_u32(root, at + 0x10, k_inertia_stamp);
  put_vec3(root, at + 0x20, inertia.inverse_inertia);
  put_f32(root, at + 0x2c, inertia.inverse_inertia_w);
  put_vec3(root, at + 0x30, inertia.center);
  put_f32(root, at + 0x3c, inertia.center_w);
  put_vec4(root, at + 0x40, inertia.orientation);
}

void put_cinfo(HknpPackObject& root, std::size_t at, const HknpBody& body) {
  put_u32(root, at + 0x08, k_static_motion_index);
  put_u32(root, at + 0x0c, body.motion_index);
  put_u16(root, at + 0x10, body.quality);
  put_u16(root, at + 0x12, body.material_slot);
  put_u32(root, at + 0x14, body.packed_filter);
  put_u32(root, at + 0x18, body.flags);
  put_vec3(root, at + 0x30, body.position);
  put_f32(root, at + 0x3c, body.position_w);
  put_vec4(root, at + 0x40, body.orientation);
}

void put_binding(HknpPackObject& root, std::size_t at, const HknpConstraint& constraint) {
  put_u32(root, at + 0x08, static_cast<std::uint32_t>(constraint.child_body));
  put_u32(root, at + 0x0c, static_cast<std::uint32_t>(constraint.parent_body));
}

HknpPackObject make_skeleton(const HknpSystem& sys) {
  const std::size_t bones = sys.bones.size();
  const std::size_t parents_at = align16(k_skeleton_header);
  const std::size_t bones_at = align16(parents_at + 2 * bones);
  const std::size_t poses_at = align16(bones_at + k_bone_stride * bones);
  HknpPackObject skeleton = make_object("hkaSkeleton", align16(poses_at + k_pose_stride * bones));
  const auto count = to_u32(bones);
  put_array_descriptor(skeleton, 0x18, count, k_array_capacity_bit | count);
  put_array_descriptor(skeleton, 0x28, count, k_array_capacity_bit | count);
  put_array_descriptor(skeleton, 0x38, count, k_array_capacity_bit | count);
  add_local(skeleton, 0x18, to_u32(parents_at));
  add_local(skeleton, 0x28, to_u32(bones_at));
  add_local(skeleton, 0x38, to_u32(poses_at));
  for (const std::size_t field : k_empty_skeleton_arrays) {
    put_array_descriptor(skeleton, field, 0, k_array_capacity_bit);
  }
  for (std::size_t i = 0; i < bones; ++i) {
    const HknpBone& bone = sys.bones[i];
    put_u16(skeleton, parents_at + 2 * i, static_cast<std::uint16_t>(static_cast<std::int16_t>(bone.parent)));
    put_u8(skeleton, bones_at + k_bone_stride * i + 8, static_cast<std::uint8_t>(bone.lock_translation ? 1 : 0));
    const std::size_t pose = poses_at + k_pose_stride * i;
    put_vec3(skeleton, pose, bone.translation);
    put_f32(skeleton, pose + 0x0c, bone.translation_w);
    put_vec4(skeleton, pose + 0x10, bone.rotation);
    put_vec3(skeleton, pose + 0x20, bone.scale);
    put_f32(skeleton, pose + 0x2c, bone.scale_w);
  }
  return skeleton;
}

// Invariant: the root's arrays follow its 0x90-byte header in field order, each starting on a 16-byte boundary.
HknpPackObject make_root(const HknpSystem& sys) {
  const std::size_t bodies = sys.bodies.size();
  constexpr std::array<std::size_t, 7> fields{{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x80}};
  const std::array<std::size_t, 7> strides{
      {k_material_stride, k_motion_stride, k_inertia_stride, k_cinfo_stride, k_binding_stride, k_shape_ref_stride,
       k_link_stride}};
  const std::array<std::size_t, 7> counts{{bodies, sys.motions.size(), sys.inertias.size(), bodies,
                                           sys.constraints.size(), sys.referenced_objects.size(),
                                           sys.bone_to_body.size()}};
  std::array<std::size_t, 7> payloads{};
  std::size_t end = k_root_header;
  for (std::size_t k = 0; k < fields.size(); ++k) {
    payloads[k] = align16(end);
    end = payloads[k] + strides[k] * counts[k];
  }
  HknpPackObject root = make_object("hknpRagdollData", align16(end));
  for (std::size_t k = 0; k < fields.size(); ++k) {
    const auto count = to_u32(counts[k]);
    put_array_descriptor(root, fields[k], count, k_array_capacity_bit | count);
    if (counts[k] > 0) add_local(root, to_u32(fields[k]), to_u32(payloads[k]));
  }

  for (const HknpBody& body : sys.bodies) {
    put_material(root, payloads[0] + k_material_stride * static_cast<std::size_t>(body.material_slot), body);
  }
  for (std::size_t k = 0; k < sys.motions.size(); ++k) {
    put_motion(root, payloads[1] + k_motion_stride * k, sys.motions[k]);
  }
  for (std::size_t k = 0; k < sys.inertias.size(); ++k) {
    put_inertia(root, payloads[2] + k_inertia_stride * k, sys.inertias[k]);
  }
  for (std::size_t i = 0; i < bodies; ++i) put_cinfo(root, payloads[3] + k_cinfo_stride * i, sys.bodies[i]);
  for (std::size_t k = 0; k < sys.constraints.size(); ++k) {
    put_binding(root, payloads[4] + k_binding_stride * k, sys.constraints[k]);
  }
  for (std::size_t k = 0; k < sys.bone_to_body.size(); ++k) {
    put_u32(root, payloads[6] + k_link_stride * k, static_cast<std::uint32_t>(sys.bone_to_body[k]));
  }

  for (std::size_t i = 0; i < bodies; ++i) {
    add_global(root, to_u32(payloads[3] + k_cinfo_stride * i), to_u32(sys.bodies[i].shape_object));
  }
  for (std::size_t k = 0; k < sys.constraints.size(); ++k) {
    add_global(root, to_u32(payloads[4] + k_binding_stride * k), to_u32(sys.constraints[k].binding_object));
  }
  for (std::size_t k = 0; k < sys.referenced_objects.size(); ++k) {
    add_global(root, to_u32(payloads[5] + k_shape_ref_stride * k), to_u32(sys.referenced_objects[k]));
  }
  add_global(root, 0x78, to_u32(sys.skeleton_object));
  return root;
}

void check_ragdoll(const HknpSystem& sys) {
  const auto& objects = sys.packfile.objects;
  const std::size_t bodies = sys.bodies.size();
  const std::size_t bones = sys.bones.size();
  if (objects.empty() || objects.front().class_name != "hknpRagdollData") {
    throw std::invalid_argument("a ragdoll writer needs a hknpRagdollData root at object 0");
  }
  if (bones == 0) throw std::invalid_argument("a ragdoll needs at least one bone");
  if (sys.bone_to_body.size() != bones) throw std::invalid_argument("a ragdoll needs one body index per bone");
  for (const auto body_index : sys.bone_to_body) {
    if (body_index < 0 || static_cast<std::size_t>(body_index) >= bodies) {
      throw std::invalid_argument("a bone names a body outside the ragdoll");
    }
  }
  for (const auto& bone : sys.bones) {
    if (bone.parent < -1 || bone.parent >= static_cast<int>(bones)) {
      throw std::invalid_argument("a bone's parent is outside the skeleton");
    }
  }
  if (sys.skeleton_object >= objects.size() || objects[sys.skeleton_object].class_name != "hkaSkeleton") {
    throw std::invalid_argument("a ragdoll needs an hkaSkeleton object");
  }
  std::vector<bool> material_used(bodies, false);
  for (const auto& body : sys.bodies) {
    const auto slot = static_cast<std::size_t>(body.material_slot);
    if (slot >= bodies || material_used[slot]) {
      throw std::invalid_argument("material ids must be a permutation of the bodies");
    }
    material_used[slot] = true;
    if (body.shape_object >= objects.size()) throw std::invalid_argument("a body names a shape outside the packfile");
    if (!std::isfinite(body.friction) || !std::isfinite(body.static_friction) || !std::isfinite(body.restitution)) {
      throw std::invalid_argument("a body's friction and restitution must be finite");
    }
  }
  for (const auto object : sys.referenced_objects) {
    if (object >= objects.size()) throw std::invalid_argument("a referenced object lies outside the packfile");
  }
  for (const auto& constraint : sys.constraints) {
    if (constraint.object >= objects.size() || constraint.binding_object >= objects.size()) {
      throw std::invalid_argument("a constraint names an object outside the packfile");
    }
  }
}

std::optional<std::vector<std::byte>> encoded_constraint(const HknpConstraint& constraint) {
  if (constraint.kind == "hkpRagdollConstraintData") return encode_ragdoll_constraint(constraint);
  if (constraint.kind == "hkpLimitedHingeConstraintData") return encode_limited_hinge_constraint(constraint);
  if (constraint.kind == "hkpBallAndSocketConstraintData") return encode_ball_and_socket_constraint(constraint);
  if (constraint.kind == "hkpPrismaticConstraintData") return encode_prismatic_constraint(constraint);
  return std::nullopt;
}

}  // namespace

HknpPackfile encode_hknp_ragdoll(const HknpSystem& sys) {
  check_ragdoll(sys);
  HknpPackfile out = sys.packfile;
  for (const auto& constraint : sys.constraints) {
    if (auto bytes = encoded_constraint(constraint)) out.objects[constraint.object].bytes = std::move(*bytes);
    if (constraint.breakable) {
      out.objects[constraint.binding_object].bytes = encode_breakable_constraint(constraint);
    }
  }
  out.objects.front() = make_root(sys);
  out.objects[sys.skeleton_object] = make_skeleton(sys);
  return out;
}

}  // namespace fo4::nif
