#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fo4::binary::raw {

// Invariant: no bounds checks; callers must have validated offset + width against the buffer.

inline std::uint8_t read_u8(std::span<const std::byte> data, std::size_t offset) {
  return std::to_integer<std::uint8_t>(data[offset]);
}

inline std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::int16_t read_i16(std::span<const std::byte> data, std::size_t offset) {
  std::int16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::int32_t read_i32(std::span<const std::byte> data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline float read_f32(std::span<const std::byte> data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline void write_u8(std::span<std::byte> data, std::size_t offset, std::uint8_t value) {
  data[offset] = static_cast<std::byte>(value);
}

inline void write_u16(std::span<std::byte> data, std::size_t offset, std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

inline void write_i16(std::span<std::byte> data, std::size_t offset, std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

inline void write_u32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

inline void write_i32(std::span<std::byte> data, std::size_t offset, std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

inline void write_f32(std::span<std::byte> data, std::size_t offset, float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

}  // namespace fo4::binary::raw
