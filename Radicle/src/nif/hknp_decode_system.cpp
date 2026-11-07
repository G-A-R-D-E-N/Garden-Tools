#include "hknp_decode_internal.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace fo4::nif::hknp_detail {
namespace {

constexpr std::size_t k_body_cinfo_stride = 0x60;
constexpr std::size_t k_body_prop_stride = 0x50;
constexpr std::size_t k_dyn_motion_stride = 0x40;
constexpr std::size_t k_dyn_inertia_stride = 0x70;
constexpr std::uint32_t k_static_motion_index = 0x7fffffffU;
constexpr float k_inverse_mass_floor = 1.0e-12f;

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
      inertia.motion_properties_id = u16_at(root, n);
      inertia.inverse_inertia = vec3_at(root, n + 0x20);
      inertia.inverse_inertia_w = f32_at(root, n + 0x2c);
      inertia.center = vec3_at(root, n + 0x30);
      inertia.center_w = f32_at(root, n + 0x3c);
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
    body.quality = u8_at(root, c + 0x10);
    body.material_slot = u16_at(root, c + 0x12);
    if (body.shape >= 0) {
      body.resolved_material_crc = ctx.sys.shapes[static_cast<std::size_t>(body.shape)].material_crc;
    }
    body.packed_filter = u32_at(root, c + 0x14);
    body.flags = u32_at(root, c + 0x18);
    body.motion_index = u32_at(root, c + 0x0c);
    body.dynamic = body.motion_index != k_static_motion_index;
    body.position = vec3_at(root, c + 0x30);
    body.position_w = f32_at(root, c + 0x3c);
    body.orientation = vec4_at(root, c + 0x40);
    if (props && static_cast<std::uint32_t>(body.material_slot) < props->count) {
      const std::size_t p = props->offset + k_body_prop_stride * static_cast<std::size_t>(body.material_slot);
      body.friction = truncated_float16(root, p + 0x12);
      body.static_friction = truncated_float16(root, p + 0x14);
      body.restitution = truncated_float16(root, p + 0x16);
      body.material_flags = u32_at(root, p + 0x0c);
      body.trigger_type = u8_at(root, p + 0x10);
    }
    ctx.sys.bodies.push_back(body);
  }
}

void decode_skeleton(const HknpPackfile& pack, HknpSystem& sys) {
  const HknpPackObject* skeleton = nullptr;
  for (std::size_t index = 0; index < pack.objects.size(); ++index) {
    if (pack.objects[index].class_name == "hkaSkeleton") {
      skeleton = &pack.objects[index];
      sys.skeleton_object = index;
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
  const auto locks = local_target(*skeleton, 0x28);
  const std::uint32_t count = u32_at(*skeleton, 0x20);
  const auto poses = local_target(*skeleton, 0x38);
  for (std::uint32_t i = 0; i < count; ++i) {
    HknpBone bone;
    bone.parent = static_cast<std::int16_t>(u16_at(*skeleton, *parents + std::size_t{2} * i));
    if (locks) {
      bone.lock_translation = u8_at(*skeleton, *locks + std::size_t{16} * i + 8) != 0;
    }
    if (poses) {
      const std::size_t e = *poses + std::size_t{48} * i;
      bone.translation = vec3_at(*skeleton, e);
      bone.translation_w = f32_at(*skeleton, e + 0x0c);
      bone.rotation = vec4_at(*skeleton, e + 0x10);
      bone.scale = vec3_at(*skeleton, e + 0x20);
      bone.scale_w = f32_at(*skeleton, e + 0x2c);
    }
    sys.bones.push_back(bone);
  }
}

void decode_root_lists(const HknpPackfile& pack, const HknpPackObject& root, HknpSystem& sys) {
  if (const auto referenced = array_at(root, 0x60)) {
    for (std::uint32_t i = 0; i < referenced->count; ++i) {
      const auto target = global_target(pack, root, referenced->offset + std::size_t{8} * i);
      sys.referenced_objects.push_back(target ? *target : k_no_object);
    }
  }
  if (root.class_name != "hknpRagdollData") {
    return;
  }
  if (const auto map = array_at(root, 0x80)) {
    for (std::uint32_t i = 0; i < map->count; ++i) {
      sys.bone_to_body.push_back(static_cast<std::int32_t>(u32_at(root, map->offset + std::size_t{4} * i)));
    }
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
  hknp_detail::decode_root_lists(sys.packfile, root, sys);
  return sys;
}

}  // namespace fo4::nif
