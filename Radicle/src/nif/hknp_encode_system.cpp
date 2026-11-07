#include "fo4/nif/hknp_encode.hpp"

#include "hknp_encode_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using namespace hknp_detail;

// Constraint: root arrays lay out from 0x80 in this order with these strides, padded to 16.
constexpr std::size_t k_root_array_base = 0x80;
constexpr std::size_t k_body_prop_stride = 0x50;
constexpr std::size_t k_body_motion_stride = 0x40;
constexpr std::size_t k_body_inertia_stride = 0x70;
constexpr std::size_t k_body_cinfo_stride = 0x60;
constexpr std::size_t k_body_shape_ref_stride = 8;
// Constraint: the cinfo's body index byte at +0x12 caps a root at 255 bodies.
constexpr std::size_t k_max_system_bodies = 255;
// Constraint: dynamic body flags are 0x80000000, the common vanilla value.
constexpr std::uint32_t k_dynamic_body_flags = 0x80000000U;

template <std::size_t N>
bool all_finite(const std::array<float, N>& values) {
  return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

struct RootBody {
  std::uint32_t shape_object{0};
  HknpVec3 position{};
  HknpVec4 orientation{{0, 0, 0, 1}};
  std::uint32_t packed_filter{1};
  // Constraint: motion is the body's index in the motion and inertia arrays, or -1 for a static body.
  int motion{-1};
  HknpMassProperties mass{};
  float body_mass{0.0f};
  HknpVec4 inertia_orientation{{0, 0, 0, 1}};
};

void put_body_properties(HknpPackObject& root, std::size_t at) {
  put_u16(root, at + 0x10, 0xff00);
  put_u16(root, at + 0x12, 0x3f00);
  put_u16(root, at + 0x14, 0x3f00);
  put_u16(root, at + 0x16, 0x3ecd);
  put_u32(root, at + 0x18, 0x3d4c0201U);
  put_u32(root, at + 0x1c, 0x7f7fffeeU);
  put_f32(root, at + 0x20, 1.0f);
  put_f32(root, at + 0x24, 1.0f);
  put_u32(root, at + 0x38, 0x000040a0U);
}

std::uint32_t offset_of(std::size_t value) { return static_cast<std::uint32_t>(value); }

// Constraint: the stored centre is the shape centre rotated by the body and moved to it.
HknpVec3 rotated(const HknpVec4& quaternion, const HknpVec3& vector) {
  const float x = quaternion[0];
  const float y = quaternion[1];
  const float z = quaternion[2];
  const float w = quaternion[3];
  const float tx = 2.0f * (y * vector[2] - z * vector[1]);
  const float ty = 2.0f * (z * vector[0] - x * vector[2]);
  const float tz = 2.0f * (x * vector[1] - y * vector[0]);
  return {vector[0] + w * tx + (y * tz - z * ty), vector[1] + w * ty + (z * tx - x * tz),
          vector[2] + w * tz + (x * ty - y * tx)};
}

// Invariant: the returned root holds one property record and one cinfo per body, and one shape reference per body.
// Constraint: motion and inertia records follow body order, and each array sits where vanilla puts it.
HknpPackObject make_system_root(const std::vector<RootBody>& bodies) {
  const std::size_t count = bodies.size();
  std::size_t dynamic = 0;
  for (const RootBody& body : bodies) {
    dynamic += body.motion >= 0 ? 1 : 0;
  }
  const std::size_t props_at = k_root_array_base;
  const std::size_t motions_at = props_at + k_body_prop_stride * count;
  const std::size_t inertias_at = motions_at + k_body_motion_stride * dynamic;
  const std::size_t cinfos_at = inertias_at + k_body_inertia_stride * dynamic;
  const std::size_t shapes_at = cinfos_at + k_body_cinfo_stride * count;
  HknpPackObject root =
      make_object("hknpPhysicsSystemData", align16(shapes_at + k_body_shape_ref_stride * count));
  const std::array<std::array<std::size_t, 3>, 5> arrays{{
      {{0x10, props_at, count}},
      {{0x20, motions_at, dynamic}},
      {{0x30, inertias_at, dynamic}},
      {{0x40, cinfos_at, count}},
      {{0x60, shapes_at, count}},
  }};
  for (const auto& array : arrays) {
    const auto fields = static_cast<std::uint32_t>(array[2]);
    put_array_descriptor(root, array[0], fields, k_array_capacity_bit | fields);
    if (array[2] > 0) add_local(root, offset_of(array[0]), offset_of(array[1]));
  }
  put_array_descriptor(root, 0x50, 0, k_array_capacity_bit);

  for (std::size_t i = 0; i < count; ++i) put_body_properties(root, props_at + k_body_prop_stride * i);
  for (const RootBody& body : bodies) {
    if (body.motion < 0) continue;
    const std::size_t at = motions_at + k_body_motion_stride * static_cast<std::size_t>(body.motion);
    for (std::size_t k = 0; k < k_measured_motion.size(); ++k) put_u8(root, at + k, k_measured_motion[k]);
  }
  for (const RootBody& body : bodies) {
    if (body.motion < 0) continue;
    // Constraint: the stored density is body mass over shape volume, and inverse inertia is taken at that scale.
    const float density = body.body_mass / body.mass.volume;
    const std::size_t at = inertias_at + k_body_inertia_stride * static_cast<std::size_t>(body.motion);
    put_u16(root, at, static_cast<std::uint16_t>(body.motion));
    put_u16(root, at + 2, 1);
    put_f32(root, at + 4, 1.0f / body.body_mass);
    put_f32(root, at + 8, density);
    put_u32(root, at + 0x0c, k_inertia_stamp);
    put_u32(root, at + 0x10, k_inertia_stamp);
    for (std::size_t k = 0; k < 3; ++k) {
      put_f32(root, at + 0x20 + 4 * k, 1.0f / (body.mass.inertia_raw[k] * density));
    }
    const HknpVec3 world = rotated(body.orientation, body.mass.center);
    put_vec3(root, at + 0x30, HknpVec3{world[0] + body.position[0], world[1] + body.position[1],
                                      world[2] + body.position[2]});
    // Constraint: the inverse-inertia w lane is 1 and the centre's w lane is 0.
    put_f32(root, at + 0x2c, 1.0f);
    put_vec4(root, at + 0x40, body.inertia_orientation);
  }
  // Ordering: cinfo shape pointers are fixed up before the shape references, not interleaved per body.
  for (std::size_t i = 0; i < count; ++i) {
    add_global(root, offset_of(cinfos_at + k_body_cinfo_stride * i), bodies[i].shape_object);
  }
  for (std::size_t i = 0; i < count; ++i) {
    add_global(root, offset_of(shapes_at + k_body_shape_ref_stride * i), bodies[i].shape_object);
  }
  for (std::size_t i = 0; i < count; ++i) {
    const RootBody& body = bodies[i];
    const std::size_t cinfo = cinfos_at + k_body_cinfo_stride * i;
    put_u32(root, cinfo + 0x08, k_static_motion_index);
    put_u32(root, cinfo + 0x0c,
            body.motion >= 0 ? static_cast<std::uint32_t>(body.motion) : k_static_motion_index);
    put_u8(root, cinfo + 0x10, 0xff);
    // Constraint: byte +0x12 is the body's index in the root.
    put_u8(root, cinfo + 0x12, static_cast<std::uint8_t>(i));
    put_u32(root, cinfo + 0x14, body.packed_filter);
    put_u32(root, cinfo + 0x18, body.motion >= 0 ? k_dynamic_body_flags : 0);
    put_vec3(root, cinfo + 0x30, body.position);
    put_vec4(root, cinfo + 0x40, body.orientation);
  }
  return root;
}

void check_dynamic_mass(const HknpMassProperties& mass, float body_mass) {
  if (!(mass.volume > 0.0f) || !std::isfinite(mass.volume)) {
    throw std::invalid_argument("a dynamic body needs a positive volume in its mass properties");
  }
  if (!all_finite(mass.center)) throw std::invalid_argument("a dynamic body needs a finite mass centre");
  const float density = body_mass / mass.volume;
  if (!(density > 0.0f) || !std::isfinite(density)) {
    throw std::invalid_argument("a dynamic body's mass and volume give no finite density");
  }
  if (!std::isfinite(1.0f / body_mass)) {
    throw std::invalid_argument("a dynamic body's mass has no finite inverse");
  }
  for (const float inertia : mass.inertia_raw) {
    if (!(inertia > 0.0f) || !std::isfinite(inertia)) {
      throw std::invalid_argument("a dynamic body needs a positive inertia on every axis");
    }
    // Constraint: the writer stores 1 / (inertia * density), so that product must not underflow.
    const float scaled = inertia * density;
    if (!(scaled > 0.0f) || !std::isfinite(scaled)) {
      throw std::invalid_argument("a dynamic body's inertia times its density must not underflow");
    }
    if (!std::isfinite(1.0f / scaled)) {
      throw std::invalid_argument("a dynamic body's inertia times its density has no finite inverse");
    }
  }
}

}  // namespace

HknpPackfile author_hknp_system(const std::vector<HknpSystemBody>& bodies) {
  if (bodies.empty()) throw std::invalid_argument("a physics system needs at least one body");
  if (bodies.size() > k_max_system_bodies) {
    throw std::invalid_argument("a physics system holds at most 255 bodies");
  }
  HknpPackfile pack;
  // Ordering: the root is object 0 and is filled last, after the shape objects it names.
  pack.objects.push_back(make_object("hknpPhysicsSystemData", 0));
  std::vector<RootBody> roots;
  roots.reserve(bodies.size());
  std::size_t dynamic = 0;
  for (const HknpSystemBody& body : bodies) {
    if (!all_finite(body.position) || !all_finite(body.orientation)) {
      throw std::invalid_argument("a body placement must be finite");
    }
    if (!std::isfinite(body.mass) || body.mass < 0.0f) {
      throw std::invalid_argument("a body mass must be finite and not negative");
    }
    RootBody root;
    root.position = body.position;
    root.orientation = body.orientation;
    root.packed_filter = body.packed_filter;
    root.inertia_orientation = body.inertia_orientation;
    if (body.children.empty()) {
      if (!hknp_detail::shape_is_finite(body.shape)) {
        throw std::invalid_argument("non-finite geometry cannot be authored");
      }
      root.shape_object = hknp_detail::append_shape(pack, body.shape);
    } else {
      for (const HknpCompoundChild& child : body.children) {
        if (!hknp_detail::shape_is_finite(child.shape)) {
          throw std::invalid_argument("non-finite geometry cannot be authored");
        }
      }
      root.shape_object = hknp_detail::append_compound_shape(pack, body.children);
    }
    if (body.mass > 0.0f) {
      // Constraint: a compound body has no shape mass object, so it takes the body's mass properties.
      const HknpMassProperties& mass =
          body.mass_properties.volume > 0.0f ? body.mass_properties : body.shape.mass_properties;
      check_dynamic_mass(mass, body.mass);
      root.motion = static_cast<int>(dynamic);
      root.mass = mass;
      root.body_mass = body.mass;
      ++dynamic;
    }
    roots.push_back(root);
  }
  pack.objects.front() = make_system_root(roots);
  for (const char* name : {"hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"}) {
    add_pack_class(pack, name);
  }
  for (const auto& object : pack.objects) add_pack_class(pack, object.class_name);
  return pack;
}

namespace hknp_detail {

std::uint32_t append_shape(HknpPackfile& pack, const HknpShape& shape) {
  if (shape.kind == HknpShapeKind::compound) {
    throw std::invalid_argument("a compound body is authored from its children");
  }
  const auto first = static_cast<std::uint32_t>(pack.objects.size());
  if (shape.kind == HknpShapeKind::compressed_mesh) {
    std::vector<HknpPackObject> objects = encode_mesh_objects(shape, first);
    for (auto& object : objects) pack.objects.push_back(std::move(object));
    return first;
  }
  HknpPackObject object = encode_shape_object(shape);
  const bool with_mass = shape.kind == HknpShapeKind::convex && shape.has_mass_properties;
  if (with_mass) add_global(object, 0x20, first + 1);
  pack.objects.push_back(std::move(object));
  if (with_mass) {
    pack.objects.push_back(make_properties_object(first + 2));
    pack.objects.push_back(make_mass_object(shape.mass_properties));
  }
  return first;
}

}  // namespace hknp_detail

}  // namespace fo4::nif
