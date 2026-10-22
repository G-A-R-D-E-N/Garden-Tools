#pragma once

#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::swf::detail {

inline constexpr unsigned kMaxTagDepth = 64;

inline constexpr std::size_t kMaxBodyBytes = 256u * 1024u * 1024u;

inline constexpr std::size_t kMaxTags = 1u << 18;

class SwfReader {
public:
  SwfReader(std::span<const std::byte> data, std::size_t start) noexcept
      : data_(data), position_(start) {}

  std::size_t position() const noexcept { return position_; }
  std::size_t remaining() const noexcept { return data_.size() - position_; }

  bool read_u8(std::uint8_t& out) noexcept;
  bool read_u16(std::uint16_t& out) noexcept;
  bool read_u32(std::uint32_t& out) noexcept;
  bool read_zstring(std::string& out);
  bool read_sized_string(std::string& out);
  bool skip(std::size_t count) noexcept;

private:
  std::span<const std::byte> data_;
  std::size_t position_{0};
};

class BitReader {
public:
  BitReader(std::span<const std::byte> data, std::size_t start_bit) noexcept
      : data_(data), bit_position_(start_bit) {}

  bool read_unsigned(unsigned count, std::uint32_t& out) noexcept;
  bool read_signed(unsigned count, std::int32_t& out) noexcept;

  std::size_t bit_position() const noexcept { return bit_position_; }
  std::size_t byte_position() const noexcept { return (bit_position_ + 7) / 8; }

private:
  std::span<const std::byte> data_;
  std::size_t bit_position_{0};
};

std::string to_text(std::span<const std::byte> bytes);

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t index) noexcept;

bool read_rect(std::span<const std::byte> data, std::size_t offset, SwfRect& out,
               std::size_t& next) noexcept;

SwfStatus decode_tag_payload(const std::vector<std::byte>& payload, SwfTag& out_tag,
                             unsigned depth, std::size_t& tags_left);

SwfStatus decode_tag_stream(std::span<const std::byte> data, std::size_t begin, std::size_t end,
                            unsigned depth, std::vector<SwfTag>& out, std::size_t& stop,
                            std::size_t& tags_left);

}  // namespace fo4::swf::detail
