#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hknp {

inline fo4::nif::HknpPackObject make_object(std::string class_name, std::size_t size) {
  fo4::nif::HknpPackObject object;
  object.class_name = std::move(class_name);
  object.bytes.assign(size, std::byte{0});
  return object;
}

inline void put_le(fo4::nif::HknpPackObject& object, std::size_t offset, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    object.bytes.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
  }
}

inline void put_u8(fo4::nif::HknpPackObject& object, std::size_t offset, std::uint8_t value) {
  object.bytes.at(offset) = std::byte{value};
}

inline void put_u16(fo4::nif::HknpPackObject& object, std::size_t offset, std::uint16_t value) {
  put_le(object, offset, value, 2);
}

inline void put_u32(fo4::nif::HknpPackObject& object, std::size_t offset, std::uint32_t value) {
  put_le(object, offset, value, 4);
}

inline void put_f32(fo4::nif::HknpPackObject& object, std::size_t offset, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(object, offset, bits);
}

inline void put_vec3(fo4::nif::HknpPackObject& object, std::size_t offset, float x, float y, float z) {
  put_f32(object, offset, x);
  put_f32(object, offset + 4, y);
  put_f32(object, offset + 8, z);
}

inline void put_array(fo4::nif::HknpPackObject& object, std::size_t field, std::uint16_t count, std::size_t payload) {
  put_u16(object, field, count);
  put_u16(object, field + 2, static_cast<std::uint16_t>(payload - field));
}

inline void put_local(fo4::nif::HknpPackObject& object, std::size_t source, std::size_t target) {
  object.local.push_back(fo4::nif::HknpLocalFixup{static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

inline void put_global(fo4::nif::HknpPackObject& object, std::size_t source, std::size_t target) {
  object.global.push_back(fo4::nif::HknpGlobalFixup{static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(target)});
}

// Constraint: objects[0] is the root, and its one body names objects[1], the first shape given here.
inline fo4::nif::HknpPackfile system_with_body(std::vector<fo4::nif::HknpPackObject> shapes) {
  fo4::nif::HknpPackObject root = make_object("hknpPhysicsSystemData", 0x200);
  put_local(root, 0x40, 0x80);
  put_u32(root, 0x48, 1);
  put_global(root, 0x80, 1);
  put_u32(root, 0x8c, 0x7fffffffU);
  fo4::nif::HknpPackfile pack;
  pack.objects.push_back(std::move(root));
  for (auto& shape : shapes) {
    pack.objects.push_back(std::move(shape));
  }
  return pack;
}

inline fo4::nif::HknpPackObject make_sphere(float radius, float x = 0.0f, float y = 0.0f, float z = 0.0f) {
  fo4::nif::HknpPackObject shape = make_object("hknpSphereShape", 0xA0);
  put_f32(shape, 0x14, radius);
  put_array(shape, 0x30, 4, 0x60);
  for (std::size_t v = 0; v < 4; ++v) {
    put_vec3(shape, 0x60 + 16 * v, x, y, z);
  }
  return shape;
}

inline std::uint32_t u32_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(object.bytes.at(offset + i))) << (8 * i);
  }
  return value;
}

inline float f32_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  const std::uint32_t bits = u32_in_object(object, offset);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

inline fo4::nif::HknpVec3 vec3_in_object(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  return {f32_in_object(object, offset), f32_in_object(object, offset + 4), f32_in_object(object, offset + 8)};
}

}  // namespace fo4::test::hknp
