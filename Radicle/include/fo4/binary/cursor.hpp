#pragma once

#include "fo4/error.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fo4::binary {

class Cursor {
public:
  explicit Cursor(std::span<const std::byte> data, std::size_t base_offset = 0)
      : data_(data), base_offset_(base_offset) {}

  std::size_t position() const noexcept { return pos_; }
  std::size_t absolute_offset() const noexcept { return base_offset_ + pos_; }
  std::size_t size() const noexcept { return data_.size(); }
  std::size_t remaining() const noexcept { return data_.size() - pos_; }
  bool at_end() const noexcept { return pos_ >= data_.size(); }
  std::span<const std::byte> data() const noexcept { return data_; }

  void seek(std::size_t pos) {
    if (pos > data_.size()) {
      throw parse_error(absolute_offset(), "seek past end of buffer");
    }
    pos_ = pos;
  }

  std::uint8_t read_u8() { return std::to_integer<std::uint8_t>(read_byte()); }

  std::uint16_t read_u16() {
    const auto lo = static_cast<std::uint16_t>(read_u8());
    const auto hi = static_cast<std::uint16_t>(read_u8());
    return static_cast<std::uint16_t>(lo | (hi << 8));
  }

  std::uint32_t read_u32() {
    const auto b0 = static_cast<std::uint32_t>(read_u8());
    const auto b1 = static_cast<std::uint32_t>(read_u8());
    const auto b2 = static_cast<std::uint32_t>(read_u8());
    const auto b3 = static_cast<std::uint32_t>(read_u8());
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }

  std::int32_t read_i32() { return static_cast<std::int32_t>(read_u32()); }

  float read_f32() {
    const auto bits = read_u32();
    float value{};
    static_assert(sizeof(float) == 4);
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  Signature read_signature() {
    const auto b0 = static_cast<char>(read_u8());
    const auto b1 = static_cast<char>(read_u8());
    const auto b2 = static_cast<char>(read_u8());
    const auto b3 = static_cast<char>(read_u8());
    return Signature{b0, b1, b2, b3};
  }

  std::span<const std::byte> peek(std::size_t count) const {
    require(count);
    return data_.subspan(pos_, count);
  }

  std::span<const std::byte> read_bytes(std::size_t count) {
    require(count);
    const auto slice = data_.subspan(pos_, count);
    pos_ += count;
    return slice;
  }

  std::vector<std::byte> read_vector(std::size_t count) {
    const auto slice = read_bytes(count);
    return std::vector<std::byte>(slice.begin(), slice.end());
  }

private:
  std::byte read_byte() {
    require(1);
    return data_[pos_++];
  }

  void require(std::size_t count) const {
    if (count > remaining()) {
      throw parse_error(absolute_offset(), "unexpected end of data");
    }
  }

  std::span<const std::byte> data_{};
  std::size_t pos_{0};
  std::size_t base_offset_{0};
};

}  // namespace fo4::binary
