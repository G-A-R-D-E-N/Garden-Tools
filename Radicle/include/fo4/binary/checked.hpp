#pragma once

#include "fo4/binary/cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::binary::checked {

inline Cursor at(std::span<const std::byte> data, std::size_t offset) {
  Cursor cursor(data);
  cursor.seek(offset);
  return cursor;
}

inline std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset = 0) {
  return at(data, offset).read_u16();
}

inline std::int16_t read_i16(std::span<const std::byte> data, std::size_t offset = 0) {
  return static_cast<std::int16_t>(read_u16(data, offset));
}

inline std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset = 0) {
  return at(data, offset).read_u32();
}

inline std::int32_t read_i32(std::span<const std::byte> data, std::size_t offset = 0) {
  return at(data, offset).read_i32();
}

inline float read_f32(std::span<const std::byte> data, std::size_t offset = 0) {
  return at(data, offset).read_f32();
}

}  // namespace fo4::binary::checked
