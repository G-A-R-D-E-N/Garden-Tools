#pragma once

#include <fo4/avm2/object.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::avm2 {

inline constexpr std::size_t kMaxByteArraySize = std::size_t{1} << 28;

class ByteArrayObject : public Object {
 public:
  explicit ByteArrayObject(Class* byte_array_class);

  std::size_t length() const noexcept { return bytes_.size(); }
  void resize(std::size_t count) { bytes_.resize(count); }

  std::uint8_t byte_at(std::size_t index) const noexcept {
    return index < bytes_.size() ? bytes_[index] : std::uint8_t{0};
  }
  void set_byte_at(std::size_t index, std::uint8_t value) {
    if (index >= kMaxByteArraySize) {
      return;
    }
    if (index >= bytes_.size()) {
      bytes_.resize(index + 1);
    }
    bytes_[index] = value;
  }

  std::size_t position() const noexcept { return position_; }
  void set_position(std::size_t offset) noexcept { position_ = offset; }

  bool little_endian() const noexcept { return little_endian_; }
  void set_little_endian(bool enabled) noexcept { little_endian_ = enabled; }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t position_{0};
  bool little_endian_{false};
};

}  // namespace fo4::avm2
