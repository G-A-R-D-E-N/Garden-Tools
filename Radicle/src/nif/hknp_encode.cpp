#include "fo4/nif/hknp_encode.hpp"

#include "hknp_decode_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_align = 16;
constexpr std::uint32_t k_vertex_w = 0x3f000000U;
constexpr std::uint32_t k_one_w = 0x3f800000U;
constexpr std::uint32_t k_sphere_flags = 0x01000111U;
constexpr std::uint32_t k_capsule_flags = 0x010001c3U;
constexpr std::uint32_t k_spare_plane_w = 0xff7fffeeU;
constexpr std::uint8_t k_capsule_face_min_half_angle = 4;
constexpr std::uint8_t k_box_face_min_half_angle = 128;
constexpr std::uint32_t k_box_shape_flags = 0x01000143U;
constexpr std::uint64_t k_box_packed_orientation = 0xf530800080008000ULL;
constexpr float k_box_max_radius = 0.01f;
constexpr float k_capsule_pad_divisor = 99.0f;
constexpr std::size_t k_max_convex_vertices = 255;
constexpr std::size_t k_max_loop = 0xFF;

struct ClassHash {
  std::string_view name;
  std::uint32_t hash;
};

constexpr std::array<ClassHash, 11> k_class_hashes{{
    {"hkClass", 0x33d42383U},
    {"hkClassMember", 0xb0efa719U},
    {"hkClassEnum", 0x8a3609cfU},
    {"hkClassEnumItem", 0xce6f8a6cU},
    {"hknpPhysicsSystemData", 0xb857718bU},
    {"hknpConvexPolytopeShape", 0x3ce9b3e3U},
    {"hkRefCountedProperties", 0x7c574867U},
    {"hknpShapeMassProperties", 0xe9191728U},
    {"hknpSphereShape", 0x741e9012U},
    {"hknpCapsuleShape", 0x60a75f4cU},
    {"hknpConvexShape", 0xc8f7c10dU},
}};

std::uint16_t to_u16(std::size_t value) {
  if (value > 0xFFFFU) {
    throw std::invalid_argument("packfile offset or count does not fit in 16 bits");
  }
  return static_cast<std::uint16_t>(value);
}

std::size_t align16(std::size_t value) { return (value + k_align - 1) / k_align * k_align; }

template <std::size_t N>
bool all_finite(const std::array<float, N>& values) {
  return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

float dot(const HknpVec3& a, const HknpVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

HknpVec3 scaled(const HknpVec3& v, float factor) { return {v[0] * factor, v[1] * factor, v[2] * factor}; }

HknpVec3 added(const HknpVec3& a, const HknpVec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

HknpVec3 cross(const HknpVec3& a, const HknpVec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

HknpPackObject make_object(std::string_view class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::string(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

void put_le(HknpPackObject& object, std::size_t at, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    object.bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  }
}

void put_u8(HknpPackObject& object, std::size_t at, std::uint8_t value) { object.bytes[at] = std::byte{value}; }

void put_u16(HknpPackObject& object, std::size_t at, std::uint16_t value) { put_le(object, at, value, 2); }

void put_u32(HknpPackObject& object, std::size_t at, std::uint32_t value) { put_le(object, at, value, 4); }

void put_f32(HknpPackObject& object, std::size_t at, float value) {
  put_u32(object, at, std::bit_cast<std::uint32_t>(value));
}

void put_vec3(HknpPackObject& object, std::size_t at, const HknpVec3& value) {
  for (std::size_t k = 0; k < 3; ++k) put_f32(object, at + 4 * k, value[k]);
}

void put_vec4(HknpPackObject& object, std::size_t at, const HknpVec4& value) {
  for (std::size_t k = 0; k < 4; ++k) put_f32(object, at + 4 * k, value[k]);
}

void put_point(HknpPackObject& object, std::size_t at, const HknpVec3& point, std::uint32_t w_bits) {
  put_vec3(object, at, point);
  put_u32(object, at + 12, w_bits);
}

void put_array(HknpPackObject& object, std::size_t field, std::size_t count, std::size_t payload) {
  put_u16(object, field, to_u16(count));
  put_u16(object, field + 2, to_u16(payload - field));
}

void put_array_descriptor(HknpPackObject& object, std::size_t field, std::uint32_t count, std::uint32_t capacity) {
  put_u32(object, field + 8, count);
  put_u32(object, field + 12, capacity);
}

void add_local(HknpPackObject& object, std::uint32_t source, std::uint32_t target) {
  object.local.push_back(HknpLocalFixup{source, target});
}

void add_global(HknpPackObject& object, std::uint32_t source, std::uint32_t target_object) {
  object.global.push_back(HknpGlobalFixup{source, target_object});
}

void put_packed_vec3(HknpPackObject& object, std::size_t at, const HknpVec3& value) {
  float largest = 0.0f;
  for (const float component : value) largest = std::max(largest, std::fabs(component));
  int exponent = -96;
  if (largest > 0.0f) static_cast<void>(std::frexp(largest, &exponent));
  exponent = std::max(exponent, -96);
  const float scale = std::ldexp(1.0f, -exponent) * 32768.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    const long mantissa = std::clamp(std::lround(value[k] * scale), -32768L, 32767L);
    put_u16(object, at + 2 * k, static_cast<std::uint16_t>(static_cast<std::int16_t>(mantissa)));
  }
  put_u16(object, at + 6, static_cast<std::uint16_t>((exponent + 96) << 7));
}

HknpPackObject make_mass(const HknpMassProperties& mass) {
  HknpPackObject object = make_object("hknpShapeMassProperties", 0x30);
  put_packed_vec3(object, 0x10, mass.center);
  put_packed_vec3(object, 0x18, mass.inertia_raw);
  put_u32(object, 0x20, static_cast<std::uint32_t>(mass.packed_orientation));
  put_u32(object, 0x24, static_cast<std::uint32_t>(mass.packed_orientation >> 32));
  put_f32(object, 0x28, mass.volume);
  put_f32(object, 0x2c, mass.mass);
  return object;
}

HknpPackObject make_refcounted_properties(std::uint32_t mass_object) {
  HknpPackObject object = make_object("hkRefCountedProperties", 0x20);
  put_u32(object, 8, 1);
  put_u32(object, 12, 0x80000001U);
  put_u32(object, 24, 0xF100U);
  add_local(object, 0, 16);
  add_global(object, 16, mass_object);
  return object;
}

HknpPackObject make_root(const HknpStaticBody& body) {
  HknpPackObject root = make_object("hknpPhysicsSystemData", 0x140);
  put_array_descriptor(root, 0x10, 1, 0x80000001U);
  put_array_descriptor(root, 0x20, 0, 0x80000000U);
  put_array_descriptor(root, 0x30, 0, 0x80000000U);
  put_array_descriptor(root, 0x40, 1, 0x80000001U);
  put_array_descriptor(root, 0x50, 0, 0x80000000U);
  put_array_descriptor(root, 0x60, 1, 0x80000001U);
  put_u16(root, 0x90, 0xff00);
  put_u16(root, 0x92, 0x3f00);
  put_u16(root, 0x94, 0x3f00);
  put_u16(root, 0x96, 0x3ecd);
  put_u32(root, 0x98, 0x3d4c0201U);
  put_u32(root, 0x9c, 0x7f7fffeeU);
  put_f32(root, 0xa0, 1.0f);
  put_f32(root, 0xa4, 1.0f);
  put_u32(root, 0xb8, 0x000040a0U);
  put_u32(root, 0xd8, 0x7fffffffU);
  put_u32(root, 0xdc, 0x7fffffffU);
  put_u8(root, 0xe0, 0xff);
  put_u32(root, 0xe4, body.packed_filter);
  put_vec3(root, 0x100, body.position);
  put_vec4(root, 0x110, body.orientation);
  add_local(root, 0x10, 0x80);
  add_local(root, 0x40, 0xd0);
  add_local(root, 0x60, 0x130);
  add_global(root, 0xd0, 1);
  add_global(root, 0x130, 1);
  return root;
}

HknpPackObject make_sphere_object(const HknpShape& shape) {
  HknpPackObject object = make_object("hknpSphereShape", 0x80);
  put_u32(object, 0x10, k_sphere_flags);
  put_f32(object, 0x14, shape.radius);
  put_u32(object, 0x18, shape.material_crc);
  put_array(object, 0x30, 4, 0x40);
  for (std::size_t i = 0; i < 4; ++i) put_point(object, 0x40 + 16 * i, shape.point_a, k_vertex_w);
  return object;
}

HknpPackObject make_convex_object(const HknpShape& shape) {
  const std::size_t count = shape.vertices.size();
  if (count > k_max_convex_vertices) {
    throw std::invalid_argument("a convex shape holds at most 255 vertices");
  }
  HknpPackObject object = make_object("hknpConvexShape", align16(0x40 + 16 * count));
  put_u32(object, 0x10, shape.shape_flags);
  put_f32(object, 0x14, shape.convex_radius);
  put_u32(object, 0x18, shape.material_crc);
  put_array(object, 0x30, count, 0x40);
  for (std::size_t i = 0; i < count; ++i) {
    put_point(object, 0x40 + 16 * i, shape.vertices[i], k_vertex_w | static_cast<std::uint32_t>(i));
  }
  return object;
}

HknpPackObject make_polytope_object(const HknpShape& shape) {
  const std::size_t nv = shape.vertices.size();
  const std::size_t np = shape.planes.size();
  const std::size_t nf = shape.faces.size();
  if (shape.face_min_half_angles.size() != nf) {
    throw std::invalid_argument("each polytope face needs a min half angle");
  }
  std::size_t ni = 0;
  std::size_t used = 0;
  for (const auto& loop : shape.faces) {
    if (loop.size() > k_max_loop) {
      throw std::invalid_argument("a polytope face loop holds at most 255 indices");
    }
    ni += loop.size();
    for (const std::uint8_t index : loop) {
      if (std::size_t{index} >= nv) {
        throw std::invalid_argument("a polytope face names a vertex outside the vertex list");
      }
      used = std::max(used, std::size_t{index} + 1);
    }
  }
  const std::size_t last_used = used == 0 ? 0 : used - 1;
  const std::size_t va = 0x50;
  const std::size_t pa = va + 16 * nv;
  const std::size_t fa = pa + 16 * np;
  const std::size_t ia = fa + 4 * ((nf + 3) / 4 * 4);
  HknpPackObject object = make_object("hknpConvexPolytopeShape", align16(ia + ni));
  put_u32(object, 0x10, shape.shape_flags);
  put_f32(object, 0x14, shape.convex_radius);
  put_u32(object, 0x18, shape.material_crc);
  put_array(object, 0x30, nv, va);
  put_array(object, 0x40, np, pa);
  put_array(object, 0x44, nf, fa);
  put_array(object, 0x48, ni, ia);
  for (std::size_t i = 0; i < nv; ++i) {
    const auto w_index = static_cast<std::uint32_t>(std::min(i, last_used));
    put_point(object, va + 16 * i, shape.vertices[i], k_vertex_w | w_index);
  }
  for (std::size_t i = 0; i < np; ++i) put_vec4(object, pa + 16 * i, shape.planes[i]);
  std::size_t first = 0;
  for (std::size_t f = 0; f < nf; ++f) {
    const auto& loop = shape.faces[f];
    put_u16(object, fa + 4 * f, to_u16(first));
    put_u8(object, fa + 4 * f + 2, static_cast<std::uint8_t>(loop.size()));
    put_u8(object, fa + 4 * f + 3, shape.face_min_half_angles[f]);
    for (std::size_t k = 0; k < loop.size(); ++k) object.bytes[ia + first + k] = std::byte{loop[k]};
    first += loop.size();
  }
  return object;
}

HknpPackObject make_capsule_object(const HknpShape& shape) {
  const float core = shape.radius * hknp_detail::k_capsule_core_factor;
  const float pad = core / k_capsule_pad_divisor;
  const HknpVec3& a = shape.point_a;
  const HknpVec3& b = shape.point_b;
  const HknpVec3 ab = added(b, scaled(a, -1.0f));
  const float length = std::sqrt(dot(ab, ab));
  HknpVec3 axis{0.0f, 0.0f, 1.0f};
  if (length > 1e-12f) axis = scaled(ab, 1.0f / length);
  const HknpVec3 e0 = scaled(axis, -1.0f);
  std::size_t pick_axis = 0;
  for (std::size_t k = 1; k < 3; ++k) {
    if (std::fabs(e0[k]) < std::fabs(e0[pick_axis])) pick_axis = k;
  }
  HknpVec3 pick{};
  pick[pick_axis] = 1.0f;
  const HknpVec3 across = added(pick, scaled(e0, -dot(pick, e0)));
  const HknpVec3 e1 = scaled(across, 1.0f / std::sqrt(dot(across, across)));
  const HknpVec3 e2 = cross(axis, e1);
  const HknpVec3 centre = scaled(added(a, b), 0.5f);
  const float hl = length / 2.0f + pad;

  HknpPackObject object = make_object("hknpCapsuleShape", 0x1b0);
  put_u32(object, 0x10, k_capsule_flags);
  put_f32(object, 0x14, core);
  put_u32(object, 0x18, shape.material_crc);
  put_array(object, 0x30, 8, 0x70);
  put_array(object, 0x40, 8, 0xf0);
  put_array(object, 0x44, 6, 0x170);
  put_array(object, 0x48, 24, 0x190);
  put_point(object, 0x50, a, k_one_w);
  put_point(object, 0x60, b, k_one_w);
  for (std::size_t i = 0; i < 8; ++i) {
    const float along = (i & 1U) != 0 ? hl : -hl;
    const float across_e1 = (i & 2U) != 0 ? pad : -pad;
    const float across_e2 = (i & 4U) != 0 ? pad : -pad;
    const HknpVec3 offset = added(scaled(e0, along), added(scaled(e1, across_e1), scaled(e2, across_e2)));
    const HknpVec3 vertex = added(centre, offset);
    put_point(object, 0x70 + 16 * i, vertex, k_vertex_w | static_cast<std::uint32_t>(i));
  }
  const std::array<HknpVec3, 6> normals{{e1, scaled(e2, -1.0f), e2, scaled(e1, -1.0f), e0, scaled(e0, -1.0f)}};
  const std::array<float, 6> extents{pad, pad, pad, pad, hl, hl};
  for (std::size_t p = 0; p < 6; ++p) {
    const float w = -(dot(normals[p], centre) + extents[p]);
    put_vec4(object, 0xf0 + 16 * p, HknpVec4{normals[p][0], normals[p][1], normals[p][2], w});
  }
  put_u32(object, 0x15c, k_spare_plane_w);
  put_u32(object, 0x16c, k_spare_plane_w);
  constexpr std::array<std::uint8_t, 24> indices{7, 6, 2, 3, 3, 2, 0, 1, 7, 5, 4, 6,
                                                 1, 0, 4, 5, 1, 5, 7, 3, 2, 6, 4, 0};
  for (std::size_t f = 0; f < 6; ++f) {
    put_u16(object, 0x170 + 4 * f, to_u16(f * 4));
    put_u8(object, 0x170 + 4 * f + 2, 4);
    put_u8(object, 0x170 + 4 * f + 3, k_capsule_face_min_half_angle);
  }
  for (std::size_t i = 0; i < indices.size(); ++i) put_u8(object, 0x190 + i, indices[i]);
  return object;
}

bool shape_is_finite(const HknpShape& shape) {
  if (!std::isfinite(shape.radius) || !std::isfinite(shape.convex_radius)) return false;
  if (!all_finite(shape.point_a) || !all_finite(shape.point_b)) return false;
  for (const auto& vertex : shape.vertices) {
    if (!all_finite(vertex)) return false;
  }
  for (const auto& plane : shape.planes) {
    if (!all_finite(plane)) return false;
  }
  if (!shape.has_mass_properties) return true;
  const auto& mass = shape.mass_properties;
  return all_finite(mass.center) && all_finite(mass.inertia_raw) && std::isfinite(mass.volume) &&
         std::isfinite(mass.mass);
}

HknpPackObject encode_shape(const HknpShape& shape) {
  const bool sphere_or_capsule =
      shape.kind == HknpShapeKind::sphere || shape.kind == HknpShapeKind::capsule;
  if (sphere_or_capsule && !(shape.radius > 0.0f)) {
    throw std::invalid_argument("a sphere or capsule needs a positive radius");
  }
  if (shape.kind == HknpShapeKind::sphere) return make_sphere_object(shape);
  if (shape.kind == HknpShapeKind::capsule) return make_capsule_object(shape);
  if (shape.kind == HknpShapeKind::convex && shape.class_name == "hknpConvexShape") return make_convex_object(shape);
  if (shape.kind == HknpShapeKind::convex && shape.class_name == "hknpConvexPolytopeShape") {
    return make_polytope_object(shape);
  }
  throw std::invalid_argument("shape class " + shape.class_name + " cannot be authored");
}

void add_class(HknpPackfile& pack, const std::string& name) {
  for (const auto& known : pack.classes) {
    if (known.name == name) return;
  }
  const auto found = std::find_if(k_class_hashes.begin(), k_class_hashes.end(),
                                  [&name](const ClassHash& entry) { return entry.name == name; });
  if (found != k_class_hashes.end()) pack.classes.push_back(HknpClassName{found->hash, name});
}

}  // namespace

HknpShape make_hknp_box(const HknpVec3& center, const HknpVec3& half_extents, std::uint32_t material_crc) {
  if (!all_finite(half_extents) ||
      std::any_of(half_extents.begin(), half_extents.end(), [](float half) { return !(half > 0.0f); })) {
    throw std::invalid_argument("box half extents must be finite and positive");
  }
  const float smallest_half = std::min({half_extents[0], half_extents[1], half_extents[2]});
  const float radius = std::min(k_box_max_radius, smallest_half / 2.0f);
  const HknpVec3 inner{half_extents[0] - radius, half_extents[1] - radius, half_extents[2] - radius};
  HknpShape shape;
  shape.kind = HknpShapeKind::convex;
  shape.class_name = "hknpConvexPolytopeShape";
  shape.shape_flags = k_box_shape_flags;
  shape.convex_radius = radius;
  shape.material_crc = material_crc;
  constexpr std::array<float, 8> sign_x{1, 1, 1, -1, 1, -1, -1, -1};
  constexpr std::array<float, 8> sign_y{1, -1, 1, 1, -1, 1, -1, -1};
  constexpr std::array<float, 8> sign_z{1, 1, -1, 1, -1, -1, 1, -1};
  for (std::size_t i = 0; i < 8; ++i) {
    shape.vertices.push_back({center[0] + sign_x[i] * inner[0], center[1] + sign_y[i] * inner[1],
                              center[2] + sign_z[i] * inner[2]});
  }
  const std::array<HknpVec3, 6> normals{{{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
  const std::array<std::size_t, 6> axes{2, 2, 0, 0, 1, 1};
  for (std::size_t p = 0; p < 6; ++p) {
    const auto& n = normals[p];
    shape.planes.push_back({n[0], n[1], n[2], -(dot(n, center) + inner[axes[p]])});
  }
  shape.planes.push_back({0.0f, 0.0f, 1.0f, 0.0f});
  shape.planes.push_back({0.0f, 0.0f, 0.0f, 0.0f});
  constexpr std::array<std::array<std::uint8_t, 4>, 6> loops{
      {{0, 3, 6, 1}, {7, 5, 2, 4}, {0, 1, 4, 2}, {3, 5, 7, 6}, {5, 3, 0, 2}, {7, 4, 1, 6}}};
  for (const auto& loop : loops) {
    shape.faces.emplace_back(loop.begin(), loop.end());
    shape.face_min_half_angles.push_back(k_box_face_min_half_angle);
  }
  const float hx = half_extents[0];
  const float hy = half_extents[1];
  const float hz = half_extents[2];
  const float volume = 8.0f * hx * hy * hz;
  shape.has_mass_properties = true;
  shape.mass_properties.center = center;
  shape.mass_properties.volume = volume;
  shape.mass_properties.mass = volume;
  shape.mass_properties.inertia_raw = {volume * (hy * hy + hz * hz) / 2.0f, volume * (hx * hx + hz * hz) / 2.0f,
                                       volume * (hx * hx + hy * hy) / 2.0f};
  shape.mass_properties.packed_orientation = k_box_packed_orientation;
  return shape;
}

HknpPackfile author_hknp_static_body(const HknpShape& shape, const HknpStaticBody& body) {
  if (!shape_is_finite(shape) || !all_finite(body.position) || !all_finite(body.orientation)) {
    throw std::invalid_argument("non-finite geometry cannot be authored");
  }
  HknpPackObject object = encode_shape(shape);
  const bool with_mass = shape.kind == HknpShapeKind::convex && shape.has_mass_properties;
  HknpPackfile pack;
  pack.objects.push_back(make_root(body));
  if (with_mass) add_global(object, 0x20, 2);
  pack.objects.push_back(std::move(object));
  if (with_mass) {
    pack.objects.push_back(make_refcounted_properties(3));
    pack.objects.push_back(make_mass(shape.mass_properties));
  }
  for (const char* name : {"hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"}) {
    add_class(pack, name);
  }
  for (const auto& object_in_pack : pack.objects) add_class(pack, object_in_pack.class_name);
  return pack;
}

}  // namespace fo4::nif
