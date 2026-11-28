#include "fo4/nif/hcl_cloth.hpp"

#include "fo4/error.hpp"
#include "hknp_decode_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::nif {
namespace {

using hknp_detail::array_at;
using hknp_detail::f32_at;
using hknp_detail::global_target;
using hknp_detail::local_target;
using hknp_detail::vec3_at;
using hknp_detail::vec4_at;

constexpr std::size_t k_collidable_size = 144;
constexpr std::size_t k_collidable_name = 16;
constexpr std::size_t k_collidable_transform = 32;
constexpr std::size_t k_collidable_pinch_radius = 132;
constexpr std::size_t k_collidable_shape = 136;
constexpr std::size_t k_capsule_size = 96;
constexpr std::size_t k_tapered_size = 176;
constexpr std::size_t k_skeleton_bones = 0x28;
constexpr std::size_t k_bone_size = 16;

void require_size(const HknpPackObject& object, std::size_t size) {
  if (object.bytes.size() < size) {
    throw fo4::parse_error(0, "cloth object is shorter than its class");
  }
}

std::string string_at(const HknpPackObject& object, std::size_t at) {
  std::string text;
  for (std::size_t i = at; i < object.bytes.size(); ++i) {
    const char ch = std::to_integer<char>(object.bytes[i]);
    if (ch == '\0') return text;
    text.push_back(ch);
  }
  throw fo4::parse_error(at, "string runs past its object");
}

HclCollidable read_collidable(const HknpPackfile& pack, std::size_t index) {
  const auto& object = pack.objects[index];
  require_size(object, k_collidable_size);
  HclCollidable collidable;
  collidable.object = index;
  if (const auto name = local_target(object, k_collidable_name)) {
    collidable.name = string_at(object, *name);
  }
  for (std::size_t row = 0; row < collidable.transform.size(); ++row) {
    collidable.transform[row] = vec4_at(object, k_collidable_transform + 16 * row);
  }
  collidable.pinch_detection_radius = f32_at(object, k_collidable_pinch_radius);
  if (const auto shape = global_target(pack, object, k_collidable_shape)) {
    collidable.shape_object = *shape;
  }
  return collidable;
}

HclCapsuleShape read_capsule(const HknpPackObject& object, std::size_t index) {
  require_size(object, k_capsule_size);
  HclCapsuleShape capsule;
  capsule.object = index;
  capsule.start = vec3_at(object, 32);
  capsule.end = vec3_at(object, 48);
  capsule.dir = vec3_at(object, 64);
  capsule.radius = f32_at(object, 80);
  capsule.cap_len_sqr_inv = f32_at(object, 84);
  return capsule;
}

HclTaperedCapsuleShape read_tapered(const HknpPackObject& object, std::size_t index) {
  require_size(object, k_tapered_size);
  HclTaperedCapsuleShape tapered;
  tapered.object = index;
  tapered.small_radius = f32_at(object, 144);
  tapered.big_radius = f32_at(object, 148);
  tapered.length = f32_at(object, 152);
  tapered.depth = f32_at(object, 156);
  return tapered;
}

void read_bone_names(const HknpPackObject& skeleton, HclCloth& cloth) {
  const auto bones = array_at(skeleton, k_skeleton_bones);
  if (!bones) return;
  for (std::uint32_t i = 0; i < bones->count; ++i) {
    const std::size_t bone = bones->offset + k_bone_size * i;
    if (bone + k_bone_size > skeleton.bytes.size()) {
      throw fo4::parse_error(bone, "skeleton bone runs past its object");
    }
    const auto name = local_target(skeleton, bone);
    cloth.bone_names.push_back(name ? string_at(skeleton, *name) : std::string{});
  }
}

}  // namespace

HclCloth decode_hcl_cloth(const HknpPackfile& pack) {
  HclCloth cloth;
  bool skeleton_read = false;
  for (std::size_t index = 0; index < pack.objects.size(); ++index) {
    const auto& object = pack.objects[index];
    if (object.class_name == "hclCollidable") {
      cloth.collidables.push_back(read_collidable(pack, index));
    } else if (object.class_name == "hclCapsuleShape") {
      cloth.capsules.push_back(read_capsule(object, index));
    } else if (object.class_name == "hclTaperedCapsuleShape") {
      cloth.tapered_capsules.push_back(read_tapered(object, index));
    } else if (object.class_name == "hkaSkeleton" && !skeleton_read) {
      read_bone_names(object, cloth);
      skeleton_read = true;
    }
  }
  return cloth;
}

}  // namespace fo4::nif
