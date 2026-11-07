#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::hknp_detail {

// Constraint: every pack object starts on a 16-byte boundary, and so does each of its inline payloads.
inline constexpr std::size_t k_pack_align = 16;

// Constraint: 0xf530800080008000 is the packed orientation stored beside a box's mass tensor.
inline constexpr std::uint64_t k_identity_mass_orientation = 0xf530800080008000ULL;

// Constraint: a compressed mesh is exactly these four objects, in this order.
inline constexpr std::uint32_t k_mesh_object_count = 4;

// Constraint: each array's capacity word is this bit or-ed with its count.
inline constexpr std::uint32_t k_array_capacity_bit = 0x80000000U;
// Constraint: a static body's motion index is this; a dynamic body's is its ordinal among dynamic bodies.
inline constexpr std::uint32_t k_static_motion_index = 0x7fffffffU;
// Constraint: inertia records store this stamp at +0x0c and +0x10.
inline constexpr std::uint32_t k_inertia_stamp = 0x5f7ffff0U;

// Constraint: a dynamic body's motion record is these 64 measured bytes.
inline constexpr std::array<std::uint8_t, 64> k_measured_motion{{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0xc0, 0xd0, 0x42, 0x00, 0x90, 0xfc, 0x41, 0x00, 0x00, 0xcd, 0x3d, 0x00, 0x00, 0x4d, 0x3d,
    0x7b, 0x14, 0x2e, 0x3e, 0xd2, 0x22, 0xfb, 0x3e, 0x0b, 0xd7, 0x23, 0x3b, 0x0b, 0xd7, 0x23, 0x3b,
    0x00, 0x00, 0x80, 0x3f, 0xff, 0x67, 0x9b, 0x99, 0x04, 0x73, 0x75, 0x06, 0x73, 0x00, 0x00, 0x00,
}};

// Requires a mesh kind with at least one section; returns the four objects in shape, properties, material table, data order.
// Invariant: global fixups are relative to first_object, so the caller places the objects contiguously from there.
std::vector<HknpPackObject> encode_mesh_objects(const HknpShape& shape, std::uint32_t first_object);

// Requires a sphere, capsule, or convex shape; a compound is built by append_compound_shape instead.
HknpPackObject encode_shape_object(const HknpShape& shape);

// Invariant: the shape object is appended at the pack's end, followed by its mass properties and mass objects when it has mass.
// Constraint: the returned index is the shape's object index, which every cinfo and shape reference names.
// Constraint: a shape this encoder does not author raises std::invalid_argument.
std::uint32_t append_shape(HknpPackfile& pack, const HknpShape& shape);

// Invariant: the compound object comes first, then its children, then its data object.
// Constraint: the returned index is the compound's object index, which its body's cinfo names.
std::uint32_t append_compound_shape(HknpPackfile& pack, const std::vector<HknpCompoundChild>& children);

// Constraint: a shape reaches the writer only when every float it stores is finite.
bool shape_is_finite(const HknpShape& shape);

// Invariant: a class is added once, and its hash comes from the measured table, so an unknown class name is skipped.
void add_pack_class(HknpPackfile& pack, const std::string& name);

HknpPackObject make_mass_object(const HknpMassProperties& mass);

// Invariant: the returned object holds one global fixup at +0x10 to mass_object.
HknpPackObject make_properties_object(std::uint32_t mass_object);

inline std::uint16_t to_u16(std::size_t value) {
  if (value > 0xFFFFU) {
    throw std::invalid_argument("packfile offset or count does not fit in 16 bits");
  }
  return static_cast<std::uint16_t>(value);
}

inline std::size_t align16(std::size_t value) { return (value + k_pack_align - 1) / k_pack_align * k_pack_align; }

inline HknpPackObject make_object(std::string_view class_name, std::size_t size) {
  HknpPackObject object;
  object.class_name = std::string(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

inline void put_le(HknpPackObject& object, std::size_t at, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    object.bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  }
}

inline void put_u8(HknpPackObject& object, std::size_t at, std::uint8_t value) {
  object.bytes[at] = std::byte{value};
}

inline void put_u16(HknpPackObject& object, std::size_t at, std::uint16_t value) { put_le(object, at, value, 2); }

inline void put_u32(HknpPackObject& object, std::size_t at, std::uint32_t value) { put_le(object, at, value, 4); }

inline void put_f32(HknpPackObject& object, std::size_t at, float value) {
  put_u32(object, at, std::bit_cast<std::uint32_t>(value));
}

inline void put_vec3(HknpPackObject& object, std::size_t at, const HknpVec3& value) {
  for (std::size_t k = 0; k < 3; ++k) put_f32(object, at + 4 * k, value[k]);
}

inline void put_vec4(HknpPackObject& object, std::size_t at, const HknpVec4& value) {
  for (std::size_t k = 0; k < 4; ++k) put_f32(object, at + 4 * k, value[k]);
}

inline void add_local(HknpPackObject& object, std::uint32_t source, std::uint32_t target) {
  object.local.push_back(HknpLocalFixup{source, target});
}

inline void add_global(HknpPackObject& object, std::uint32_t source, std::uint32_t target_object) {
  object.global.push_back(HknpGlobalFixup{source, target_object});
}

// Constraint: an hkArray descriptor keeps its payload pointer at field, its count at field + 8 and its capacity at field + 12.
inline void put_array_descriptor(HknpPackObject& object, std::size_t field, std::uint32_t count,
                                 std::uint32_t capacity) {
  put_u32(object, field + 8, count);
  put_u32(object, field + 12, capacity);
}

inline float dot(const HknpVec3& a, const HknpVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

inline HknpVec3 scaled(const HknpVec3& v, float factor) {
  return {v[0] * factor, v[1] * factor, v[2] * factor};
}

inline HknpVec3 added(const HknpVec3& a, const HknpVec3& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

inline HknpVec3 subtracted(const HknpVec3& a, const HknpVec3& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline HknpVec3 cross(const HknpVec3& a, const HknpVec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

}  // namespace fo4::nif::hknp_detail
