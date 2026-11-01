#include "hknp_decode_internal.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::hknp_detail {
namespace {

constexpr std::size_t k_body_cinfo_stride = 0x60;
constexpr std::size_t k_body_prop_stride = 0x50;
constexpr std::size_t k_dyn_motion_stride = 0x40;
constexpr std::size_t k_dyn_inertia_stride = 0x70;
constexpr std::size_t k_constraint_binding_stride = 0x18;
constexpr std::uint32_t k_static_motion_index = 0x7fffffffU;
constexpr std::size_t k_atom_start = 0x20;
constexpr std::size_t k_atom_header = 16;
constexpr float k_inverse_mass_floor = 1.0e-12f;

enum AtomType : std::uint16_t {
  atom_set_local_transforms = 0x02,
  atom_ball_socket = 0x05,
  atom_two_d_ang = 0x0c,
  atom_ang_limit = 0x0e,
  atom_twist_limit = 0x0f,
  atom_cone_limit = 0x10,
  atom_ang_friction = 0x11,
  atom_ang_motor = 0x12,
  atom_ragdoll_motor = 0x13,
  atom_setup_stabilization = 0x17,
};

// Constraint: an atom type with length 0 is unknown, and it ends the atom walk.
std::size_t atom_length(std::uint16_t type) {
  switch (type) {
    case atom_set_local_transforms:
      return 144;
    case atom_ragdoll_motor:
      return 96;
    case atom_ang_motor:
      return 40;
    case atom_twist_limit:
    case atom_cone_limit:
      return 32;
    case atom_ball_socket:
    case atom_two_d_ang:
    case atom_ang_limit:
    case atom_ang_friction:
    case atom_setup_stabilization:
      return 16;
    default:
      return 0;
  }
}

bool has_prefix(const std::string& text, std::string_view prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

bool has_suffix(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

float truncated_float16(const HknpPackObject& object, std::size_t offset) {
  const std::uint32_t bits = std::uint32_t{u16_at(object, offset)} << 16;
  float value{};
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

float inverse_to_mass(float inverse) {
  return inverse > k_inverse_mass_floor ? 1.0f / inverse : 0.0f;
}

void decode_motions(const HknpPackObject& root, HknpSystem& sys) {
  if (const auto motions = array_at(root, 0x20)) {
    for (std::uint32_t i = 0; i < motions->count; ++i) {
      const std::size_t m = motions->offset + k_dyn_motion_stride * i;
      HknpDynMotion motion;
      motion.gravity_factor = f32_at(root, m + 0x08);
      motion.max_linear_velocity = f32_at(root, m + 0x10);
      motion.max_angular_velocity = f32_at(root, m + 0x14);
      motion.linear_damping = f32_at(root, m + 0x18);
      motion.angular_damping = f32_at(root, m + 0x1c);
      sys.motions.push_back(motion);
    }
  }
  if (const auto inertias = array_at(root, 0x30)) {
    for (std::uint32_t i = 0; i < inertias->count; ++i) {
      const std::size_t n = inertias->offset + k_dyn_inertia_stride * i;
      HknpDynInertia inertia;
      inertia.inverse_mass = f32_at(root, n + 0x04);
      inertia.mass = inverse_to_mass(inertia.inverse_mass);
      inertia.density = f32_at(root, n + 0x08);
      inertia.inverse_inertia = vec3_at(root, n + 0x20);
      inertia.center = vec3_at(root, n + 0x30);
      inertia.orientation = vec4_at(root, n + 0x40);
      sys.inertias.push_back(inertia);
    }
  }
}

void decode_bodies(DecodeContext& ctx, const HknpPackObject& root) {
  const auto cinfos = array_at(root, 0x40);
  if (!cinfos) {
    return;
  }
  const auto props = array_at(root, 0x10);
  for (std::uint32_t i = 0; i < cinfos->count; ++i) {
    const std::size_t c = cinfos->offset + k_body_cinfo_stride * i;
    HknpBody body;
    body.cinfo_offset = c;
    if (const auto shape_object = global_target(ctx.pack, root, c)) {
      body.shape_object = *shape_object;
      body.shape = decode_shape(ctx, *shape_object);
    }
    body.packed_filter = u32_at(root, c + 0x14);
    body.flags = u32_at(root, c + 0x18);
    body.motion_index = u32_at(root, c + 0x0c);
    body.dynamic = body.motion_index != k_static_motion_index;
    body.position = vec3_at(root, c + 0x30);
    body.orientation = vec4_at(root, c + 0x40);
    if (props) {
      const std::size_t p = props->offset + k_body_prop_stride * i;
      body.friction = truncated_float16(root, p + 0x12);
      body.restitution = truncated_float16(root, p + 0x16);
      body.material_flags = u32_at(root, p + 0x0c);
      body.trigger_type = u8_at(root, p + 0x10);
    }
    ctx.sys.bodies.push_back(body);
  }
}

HknpAngularLimit limit_at(const HknpPackObject& object, std::size_t atom, std::size_t field) {
  return HknpAngularLimit{true, f32_at(object, atom + field), f32_at(object, atom + field + 4),
                          f32_at(object, atom + field + 8)};
}

void decode_atoms(const HknpPackObject& object, HknpConstraint& constraint) {
  const std::size_t size = object.bytes.size();
  int cones = 0;
  for (std::size_t a = k_atom_start; a + k_atom_header <= size;) {
    const std::uint16_t type = u16_at(object, a);
    const std::size_t length = atom_length(type);
    if (type == 0) {
      break;
    }
    if (length == 0) {
      constraint.unread_atom = type;
      break;
    }
    if (a + length > size) {
      throw fo4::parse_error(a, "constraint atom runs past its object");
    }
    switch (type) {
      case atom_set_local_transforms:
        for (std::size_t k = 0; k < 3; ++k) {
          constraint.rotation_a[k] = vec3_at(object, a + 0x10 + 16 * k);
          constraint.rotation_b[k] = vec3_at(object, a + 0x50 + 16 * k);
        }
        constraint.pivot_a = vec3_at(object, a + 0x10 + 48);
        constraint.pivot_b = vec3_at(object, a + 0x50 + 48);
        constraint.has_frames = true;
        break;
      case atom_twist_limit:
        constraint.twist = limit_at(object, a, 0x08);
        break;
      case atom_cone_limit:
        if (cones++ == 0) {
          constraint.cone = limit_at(object, a, 0x08);
        } else {
          constraint.plane = limit_at(object, a, 0x08);
        }
        break;
      case atom_ang_limit:
        constraint.hinge = limit_at(object, a, 0x04);
        break;
      case atom_ang_friction:
        constraint.friction = f32_at(object, a + 0x08);
        break;
      case atom_ang_motor:
      case atom_ragdoll_motor:
        constraint.motor_enabled = u8_at(object, a + 0x02) != 0;
        break;
      default:
        break;
    }
    a += length;
  }
}

void unwrap_breakable(const HknpPackfile& pack, HknpConstraint& constraint) {
  const HknpPackObject& wrapper = pack.objects[constraint.object];
  for (const auto& fixup : wrapper.global) {
    if (fixup.object >= pack.objects.size()) {
      throw fo4::parse_error(fixup.source, "global fixup names no object in the packfile");
    }
    const std::string& name = pack.objects[fixup.object].class_name;
    if (has_prefix(name, "hkp") && has_suffix(name, "ConstraintData")) {
      constraint.breakable = true;
      constraint.break_threshold = f32_at(wrapper, 0x20);
      constraint.object = fixup.object;
      constraint.kind = name;
      return;
    }
  }
}

void decode_constraints(const HknpPackfile& pack, const HknpPackObject& root, HknpSystem& sys) {
  const auto bindings = array_at(root, 0x50);
  if (!bindings) {
    return;
  }
  for (std::uint32_t i = 0; i < bindings->count; ++i) {
    const std::size_t e = bindings->offset + k_constraint_binding_stride * i;
    HknpConstraint constraint;
    constraint.child_body = static_cast<int>(u32_at(root, e + 0x08));
    constraint.parent_body = static_cast<int>(u32_at(root, e + 0x0c));
    if (const auto data = global_target(pack, root, e)) {
      constraint.object = *data;
      constraint.kind = pack.objects[*data].class_name;
      if (has_prefix(constraint.kind, "hknp")) {
        unwrap_breakable(pack, constraint);
      }
      if (has_prefix(constraint.kind, "hkp")) {
        decode_atoms(pack.objects[constraint.object], constraint);
      }
    }
    sys.constraints.push_back(constraint);
  }
}

void decode_skeleton(const HknpPackfile& pack, HknpSystem& sys) {
  const HknpPackObject* skeleton = nullptr;
  for (const auto& object : pack.objects) {
    if (object.class_name == "hkaSkeleton") {
      skeleton = &object;
      break;
    }
  }
  if (skeleton == nullptr) {
    return;
  }
  const auto parents = local_target(*skeleton, 0x18);
  if (!parents) {
    return;
  }
  const std::uint32_t count = u32_at(*skeleton, 0x20);
  const auto poses = local_target(*skeleton, 0x38);
  for (std::uint32_t i = 0; i < count; ++i) {
    HknpBone bone;
    bone.parent = static_cast<std::int16_t>(u16_at(*skeleton, *parents + std::size_t{2} * i));
    if (poses) {
      const std::size_t e = *poses + std::size_t{48} * i;
      bone.translation = vec3_at(*skeleton, e);
      bone.rotation = vec4_at(*skeleton, e + 0x10);
      bone.scale = vec3_at(*skeleton, e + 0x20);
    }
    sys.bones.push_back(bone);
  }
}

}  // namespace
}  // namespace fo4::nif::hknp_detail

namespace fo4::nif {

HknpSystem decode_hknp_system(HknpPackfile pack) {
  HknpSystem sys;
  sys.packfile = std::move(pack);
  if (sys.packfile.objects.empty()) {
    throw fo4::parse_error(0, "packfile has no root object");
  }
  const HknpPackObject& root = sys.packfile.objects.front();
  if (root.class_name != "hknpPhysicsSystemData" && root.class_name != "hknpRagdollData") {
    throw fo4::parse_error(0, "packfile root is not a physics system or a ragdoll");
  }
  sys.root_class = root.class_name;
  hknp_detail::DecodeContext ctx{sys.packfile, sys,
                                 std::vector<int>(sys.packfile.objects.size(), hknp_detail::k_shape_unvisited), 0};
  hknp_detail::decode_motions(root, sys);
  hknp_detail::decode_bodies(ctx, root);
  hknp_detail::decode_constraints(sys.packfile, root, sys);
  hknp_detail::decode_skeleton(sys.packfile, sys);
  return sys;
}

}  // namespace fo4::nif
