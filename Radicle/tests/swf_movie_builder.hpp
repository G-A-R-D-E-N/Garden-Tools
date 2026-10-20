#pragma once

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace swf_test {

struct SwfBuilder {
  std::vector<std::byte> body;
  std::uint32_t pending{0};
  unsigned filled{0};

  void push_bits(std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
      pending = (pending << 1) | ((value >> (count - 1u - index)) & 1u);
      ++filled;
      if (filled == 8) {
        body.push_back(static_cast<std::byte>(pending));
        pending = 0;
        filled = 0;
      }
    }
  }

  void align(std::uint32_t padding = 0) { push_bits(padding, (8u - filled) % 8u); }

  void u8(std::uint32_t value) { body.push_back(static_cast<std::byte>(value & 0xFFu)); }

  void u16(std::uint32_t value) {
    u8(value);
    u8(value >> 8);
  }

  void u32(std::uint32_t value) {
    u16(value);
    u16(value >> 16);
  }

  void bytes(std::span<const std::byte> data) { body.insert(body.end(), data.begin(), data.end()); }

  void text(std::string_view value) {
    for (const auto ch : value) {
      body.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    body.push_back(std::byte{0});
  }

  void sized_text(std::string_view value) {
    u8(static_cast<std::uint32_t>(value.size()));
    for (const auto ch : value) {
      body.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
  }

  void rect(std::int32_t x_min, std::int32_t x_max, std::int32_t y_min, std::int32_t y_max,
            unsigned nbits, std::uint32_t padding = 0) {
    const auto mask = nbits == 0 ? 0u : ((1u << nbits) - 1u);
    push_bits(nbits, 5);
    push_bits(static_cast<std::uint32_t>(x_min) & mask, nbits);
    push_bits(static_cast<std::uint32_t>(x_max) & mask, nbits);
    push_bits(static_cast<std::uint32_t>(y_min) & mask, nbits);
    push_bits(static_cast<std::uint32_t>(y_max) & mask, nbits);
    align(padding);
  }

  void tag(std::uint16_t code, std::span<const std::byte> payload, bool long_header) {
    const auto length = static_cast<std::uint32_t>(payload.size());
    const auto code_bits = static_cast<std::uint32_t>((code & 0x3FFu) << 6);
    if (long_header || length >= 0x3Fu) {
      u16(code_bits | 0x3Fu);
      u32(length);
    } else {
      u16(code_bits | (length & 0x3Fu));
    }
    bytes(payload);
  }

  void tag(std::uint16_t code) { tag(code, std::span<const std::byte>{}, false); }
};

inline std::vector<std::byte> wrap_body(std::span<const std::byte> body, std::uint8_t version,
                                        bool gfx, bool compressed) {
  std::vector<std::byte> out;
  const char* signature = gfx ? (compressed ? "CFX" : "GFX") : (compressed ? "CWS" : "FWS");
  for (int index = 0; index < 3; ++index) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(signature[index])));
  }
  out.push_back(static_cast<std::byte>(version));
  const auto total = static_cast<std::uint32_t>(body.size() + 8);
  for (int index = 0; index < 4; ++index) {
    out.push_back(static_cast<std::byte>((total >> (8 * index)) & 0xFFu));
  }
  if (!compressed) {
    out.insert(out.end(), body.begin(), body.end());
    return out;
  }
  uLongf packed = compressBound(static_cast<uLong>(body.size()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(packed));
  const auto result = compress2(reinterpret_cast<Bytef*>(buffer.data()), &packed,
                                reinterpret_cast<const Bytef*>(body.data()),
                                static_cast<uLong>(body.size()), Z_BEST_COMPRESSION);
  EXPECT_EQ(result, Z_OK);
  out.insert(out.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(packed));
  return out;
}

}  // namespace swf_test
