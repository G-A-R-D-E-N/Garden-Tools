#include "hknp_decode_internal.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::nif::hknp_detail {
namespace {

constexpr std::size_t k_constraint_binding_stride = 0x18;
constexpr std::size_t k_atom_start = 0x20;
constexpr std::size_t k_atom_header = 16;

enum AtomType : std::uint16_t {
  atom_bridge = 0x01,
  atom_set_local_transforms = 0x02,
  atom_set_local_translations = 0x03,
  atom_set_local_rotations = 0x04,
  atom_ball_socket = 0x05,
  atom_stiff_spring = 0x06,
  atom_lin = 0x07,
  atom_lin_soft = 0x08,
  atom_lin_limit = 0x09,
  atom_lin_friction = 0x0a,
  atom_lin_motor = 0x0b,
  atom_two_d_ang = 0x0c,
  atom_ang = 0x0d,
  atom_ang_limit = 0x0e,
  atom_twist_limit = 0x0f,
  atom_cone_limit = 0x10,
  atom_ang_friction = 0x11,
  atom_ang_motor = 0x12,
  atom_ragdoll_motor = 0x13,
  atom_pulley = 0x14,
  atom_rack_and_pinion = 0x15,
  atom_cog_wheel = 0x16,
  atom_setup_stabilization = 0x17,
  atom_three_d_ang = 0x18,
  atom_deformable_lin = 0x19,
  atom_deformable_ang = 0x1a,
  atom_overwrite_pivot = 0x1b,
  atom_wheel_friction = 0x1c,
};

// Constraint: an atom type with length 0 is unknown, and it ends the atom walk.
std::size_t atom_length(std::uint16_t type) {
  switch (type) {
    case atom_bridge:
    case atom_twist_limit:
    case atom_cone_limit:
      return 32;
    case atom_set_local_transforms:
      return 144;
    case atom_set_local_translations:
    case atom_wheel_friction:
      return 48;
    case atom_set_local_rotations:
      return 112;
    case atom_stiff_spring:
      return 12;
    case atom_lin_motor:
      return 24;
    case atom_pulley:
      return 64;
    case atom_ang_motor:
      return 40;
    case atom_ragdoll_motor:
    case atom_deformable_lin:
    case atom_deformable_ang:
      return 96;
    case atom_ball_socket:
    case atom_lin:
    case atom_lin_soft:
    case atom_lin_limit:
    case atom_lin_friction:
    case atom_two_d_ang:
    case atom_ang:
    case atom_ang_limit:
    case atom_ang_friction:
    case atom_rack_and_pinion:
    case atom_cog_wheel:
    case atom_setup_stabilization:
    case atom_three_d_ang:
    case atom_overwrite_pivot:
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

HknpAngularLimit limit_at(const HknpPackObject& object, std::size_t atom, std::size_t field) {
  return HknpAngularLimit{true, f32_at(object, atom + field), f32_at(object, atom + field + 4),
                          f32_at(object, atom + field + 8)};
}

void read_frames(const HknpPackObject& object, std::size_t atom, HknpConstraint& constraint) {
  for (std::size_t k = 0; k < 3; ++k) {
    constraint.rotation_a[k] = vec3_at(object, atom + 0x10 + 16 * k);
    constraint.rotation_b[k] = vec3_at(object, atom + 0x50 + 16 * k);
    constraint.frame_a[k] = vec4_at(object, atom + 0x10 + 16 * k);
    constraint.frame_b[k] = vec4_at(object, atom + 0x50 + 16 * k);
  }
  constraint.pivot_a = vec3_at(object, atom + 0x10 + 48);
  constraint.pivot_b = vec3_at(object, atom + 0x50 + 48);
  constraint.frame_a[3] = vec4_at(object, atom + 0x10 + 48);
  constraint.frame_b[3] = vec4_at(object, atom + 0x50 + 48);
  constraint.has_frames = true;
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
    constraint.atom_types.push_back(type);
    switch (type) {
      case atom_set_local_transforms:
        read_frames(object, a, constraint);
        break;
      case atom_set_local_translations:
        constraint.translation_a = vec4_at(object, a + 0x10);
        constraint.translation_b = vec4_at(object, a + 0x20);
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
      case atom_lin_limit:
        constraint.linear_limit = HknpLinearLimit{true, f32_at(object, a + 0x04), f32_at(object, a + 0x08), 0.0f};
        break;
      case atom_ang_friction:
        constraint.friction = f32_at(object, a + 0x08);
        break;
      case atom_ang_motor:
      case atom_ragdoll_motor:
        constraint.motor_enabled = u8_at(object, a + 0x02) != 0;
        break;
      case atom_lin_motor:
        constraint.linear_motor_enabled = u8_at(object, a + 0x02) != 0;
        break;
      case atom_setup_stabilization:
        constraint.setup_stabilization = u8_at(object, a + 0x02) != 0;
        break;
      case atom_ball_socket:
        constraint.ball_socket_method = u8_at(object, a + 0x02);
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

}  // namespace

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
      constraint.binding_object = *data;
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

}  // namespace fo4::nif::hknp_detail
