#pragma once

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
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

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

inline std::vector<std::byte> movie_of(std::uint16_t frames, const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(frames);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

inline std::vector<std::byte> sprite_tag(std::uint16_t id, std::uint16_t frames,
                                  const std::vector<TagPayload>& children) {
  SwfBuilder body;
  body.u16(id);
  body.u16(frames);
  for (const auto& [code, payload] : children) {
    body.tag(code, payload, false);
  }
  body.tag(0);
  return body.body;
}

inline std::vector<std::byte> place_full(std::uint16_t depth, std::optional<std::uint16_t> character,
                                  const std::vector<std::byte>& matrix,
                                  const std::vector<std::byte>& color,
                                  std::optional<std::uint16_t> clip_depth = std::nullopt) {
  std::uint32_t flags = 0;
  if (character) {
    flags |= 0x02;
  }
  if (!matrix.empty()) {
    flags |= 0x04;
  }
  if (!color.empty()) {
    flags |= 0x08;
  }
  if (clip_depth) {
    flags |= 0x40;
  }
  SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  if (character) {
    body.u16(*character);
  }
  body.bytes(matrix);
  body.bytes(color);
  if (clip_depth) {
    body.u16(*clip_depth);
  }
  return body.body;
}

inline std::vector<std::byte> matrix_full(std::int32_t scale_x, std::int32_t scale_y, std::int32_t rotate0,
                                   std::int32_t rotate1, std::int32_t x, std::int32_t y) {
  SwfBuilder bits;
  bits.push_bits(1, 1);
  bits.push_bits(19, 5);
  bits.push_bits(static_cast<std::uint32_t>(scale_x), 19);
  bits.push_bits(static_cast<std::uint32_t>(scale_y), 19);
  bits.push_bits(1, 1);
  bits.push_bits(19, 5);
  bits.push_bits(static_cast<std::uint32_t>(rotate0), 19);
  bits.push_bits(static_cast<std::uint32_t>(rotate1), 19);
  bits.push_bits(16, 5);
  bits.push_bits(static_cast<std::uint32_t>(x) & 0xFFFFu, 16);
  bits.push_bits(static_cast<std::uint32_t>(y) & 0xFFFFu, 16);
  bits.align();
  return bits.body;
}

inline std::vector<std::byte> square_with_fill(std::uint16_t id, std::int32_t size,
                                        const std::vector<std::byte>& fill, bool stroke) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, size, 0, size, 12);
  body.u8(1);
  body.bytes(fill);
  if (stroke) {
    body.u8(1);
    body.u16(20);
    body.u8(0x00);
    body.u8(0xFF);
    body.u8(0x00);
    body.u8(0xFF);
  } else {
    body.u8(0);
  }
  body.u8(stroke ? 0x11 : 0x10);
  body.push_bits(0, 1);
  body.push_bits(stroke ? 0x0B : 0x03, 5);
  body.push_bits(12, 5);
  body.push_bits(0, 12);
  body.push_bits(0, 12);
  body.push_bits(1, 1);
  if (stroke) {
    body.push_bits(1, 1);
  }
  const std::int32_t edges[8] = {size, 0, 0, size, -size, 0, 0, -size};
  for (std::size_t index = 0; index < 8; index += 2) {
    body.push_bits(1, 1);
    body.push_bits(1, 1);
    body.push_bits(10, 4);
    body.push_bits(1, 1);
    body.push_bits(static_cast<std::uint32_t>(edges[index]) & 0xFFFu, 12);
    body.push_bits(static_cast<std::uint32_t>(edges[index + 1]) & 0xFFFu, 12);
  }
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> square_shape(std::uint16_t id, std::int32_t size, std::uint32_t rgb,
                                    bool stroke = false) {
  SwfBuilder fill;
  fill.u8(0x00);
  fill.u8((rgb >> 16) & 0xFFu);
  fill.u8((rgb >> 8) & 0xFFu);
  fill.u8(rgb & 0xFFu);
  fill.u8(0xFF);
  return square_with_fill(id, size, fill.body, stroke);
}

}  // namespace swf_test
