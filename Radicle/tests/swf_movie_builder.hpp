#pragma once

#include "fo4/swf/swf.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

inline std::vector<std::byte> frame_label(std::string_view name) {
  SwfBuilder body;
  body.text(name);
  return body.body;
}

/* Invariant: with no leading tags, flat index 1 is the sprite's placement child and flat index 4 is the root placement after the DefineSprite. */
inline std::vector<std::byte> placement_after_sprite_movie(int leading_tags = 0) {
  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  for (int tag = 0; tag < leading_tags; ++tag) sprite.tag(43, frame_label("pad"), false);
  SwfBuilder child;
  child.u8(0x02);
  child.u16(1);
  child.u16(7);
  sprite.tag(26, child.body, false);
  sprite.tag(1);
  sprite.tag(0);

  SwfBuilder place;
  place.u8(0x02);
  place.u16(2);
  place.u16(7);

  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(39, sprite.body, false);
  body.tag(26, place.body, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 6, false, false);
}

/* Requires a DefineShape fill style that is a linear gradient from opaque red at ratio 0 to opaque blue at ratio 255. */
inline std::vector<std::byte> linear_gradient_fill(const std::vector<std::byte>& matrix) {
  SwfBuilder fill;
  fill.u8(0x10);
  fill.bytes(matrix);
  fill.u8(0x02);
  fill.u8(0x00);
  fill.u8(0xFF);
  fill.u8(0x00);
  fill.u8(0x00);
  fill.u8(0xFF);
  fill.u8(0xFF);
  fill.u8(0x00);
  fill.u8(0x00);
  fill.u8(0xFF);
  fill.u8(0xFF);
  return fill.body;
}

/* Requires a DefineBitsLossless (format 5) payload of opaque white pixels whose zlib stream holds width * height * 4 bytes. */
inline std::vector<std::byte> lossless_bitmap(std::uint16_t id, std::uint16_t width,
                                              std::uint16_t height) {
  SwfBuilder body;
  body.u16(id);
  body.u8(5);
  body.u16(width);
  body.u16(height);
  const std::vector<unsigned char> raw(static_cast<std::size_t>(width) * height * 4u, 0xFFu);
  uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<unsigned char> packed(packed_size);
  EXPECT_EQ(compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  body.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(packed.data()),
                                        static_cast<std::size_t>(packed_size)));
  return body.body;
}

/* Requires a PlaceObject3 of character 7 whose filter list holds a drop shadow, then a blur. */
inline std::vector<std::byte> filtered_placement_movie(bool show_frame = false) {
  SwfBuilder shadow;
  shadow.u8(0x00);
  shadow.u8(0x10);
  shadow.u8(0x20);
  shadow.u8(0x30);
  shadow.u8(0x40);
  shadow.u32(0x00020000u);
  shadow.u32(0x00030000u);
  shadow.u32(0x00010000u);
  shadow.u32(0x00040000u);
  shadow.u16(0x0180u);
  shadow.u8(0x21);
  SwfBuilder blur;
  blur.u8(0x01);
  blur.u32(0x00050000u);
  blur.u32(0x00060000u);
  blur.u8(0x10);
  SwfBuilder place;
  place.u8(0x02);
  place.u8(0x01);
  place.u16(1);
  place.u16(7);
  place.u8(2);
  place.bytes(shadow.body);
  place.bytes(blur.body);
  std::vector<TagPayload> tags{{fo4::swf::tag::kDefineShape3, square_shape(7, 200, 0xFF0000)},
                               {fo4::swf::tag::kPlaceObject3, place.body}};
  if (show_frame) tags.push_back({fo4::swf::tag::kShowFrame, {}});
  return movie_of(1, tags);
}

/* Requires a DefineFont of font 7 with one glyph, a closed triangle in 1024-unit em space. */
inline std::vector<std::byte> one_glyph_font() {
  return {std::byte{0x07}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x10},
          std::byte{0x14}, std::byte{0x88}, std::byte{0x9C}, std::byte{0xC4}, std::byte{0x66},
          std::byte{0xA3}, std::byte{0x2C}, std::byte{0x40}, std::byte{0x00}};
}

// Constraint: a DefineFont3 with one glyph, narrow offsets, a layout block, language 1 and the bold flag, so its flag byte is 0x81.
inline std::vector<std::byte> font3_payload(std::uint16_t id, const std::string& name) {
  SwfBuilder body;
  body.u16(id);
  body.u8(0x81);
  body.u8(1);
  body.sized_text(name);
  body.u16(1);
  body.u16(4);
  body.u16(6);
  body.u8(0x10);
  body.u8(0x00);
  body.u16(0x41);
  body.u16(800);
  body.u16(200);
  body.u16(40);
  body.u16(500);
  body.rect(0, 100, 0, 100, 9);
  body.u16(0);
  return body.body;
}

// Constraint: a DefineFont2 with no glyphs and no layout block, the italic flag only; its empty offset table still holds the code table offset 2.
inline std::vector<std::byte> font2_payload(std::uint16_t id, const std::string& name) {
  SwfBuilder body;
  body.u16(id);
  body.u8(0x02);
  body.u8(0);
  body.sized_text(name);
  body.u16(0);
  body.u16(2);
  return body.body;
}

// Constraint: DefineFont4 is id, a flag byte (0x04 font data, 0x02 italic, 0x01 bold), a null-terminated name, then the font data.
inline std::vector<std::byte> font4_payload(std::uint16_t id, std::uint8_t flags, const std::string& name) {
  SwfBuilder body;
  body.u16(id);
  body.u8(flags);
  body.text(name);
  body.u8(1);
  body.u8(2);
  return body.body;
}

// Constraint: a DefineFont2 with one empty glyph per code, narrow offsets, no layout block; wide selects UI16 codes (flag 0x04), else UI8.
inline std::vector<std::byte> font2_codes_payload(std::uint16_t id, const std::string& name,
                                                  const std::vector<std::uint16_t>& codes, bool wide) {
  SwfBuilder body;
  const auto count = static_cast<std::uint32_t>(codes.size());
  body.u16(id);
  body.u8(wide ? 0x04 : 0x00);
  body.u8(0);
  body.sized_text(name);
  body.u16(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    body.u16(2 * count + 2 + 2 * index);
  }
  body.u16(4 * count + 2);
  for (std::uint32_t index = 0; index < count; ++index) {
    body.u16(0);
  }
  for (const std::uint16_t code : codes) {
    if (wide) {
      body.u16(code);
    } else {
      body.u8(code);
    }
  }
  return body.body;
}

// Constraint: DefineFontInfo is id, sized name, a flag byte (0x01 wide codes, 0x02 bold, 0x04 italic; style carries the rest), then UI8 or UI16 codes to the end of the tag.
inline std::vector<std::byte> font_info_payload(std::uint16_t id, const std::string& name,
                                                const std::vector<std::uint16_t>& codes, bool wide,
                                                std::uint8_t style = 0) {
  SwfBuilder body;
  body.u16(id);
  body.sized_text(name);
  body.u8((wide ? 0x01 : 0x00) | style);
  for (const std::uint16_t code : codes) {
    if (wide) {
      body.u16(code);
    } else {
      body.u8(code);
    }
  }
  return body.body;
}

// Constraint: DefineFontInfo2 is DefineFontInfo with a LanguageCode byte after the flag byte; its codes are always UI16.
inline std::vector<std::byte> font_info2_payload(std::uint16_t id, const std::string& name,
                                                 std::uint8_t language,
                                                 const std::vector<std::uint16_t>& codes,
                                                 std::uint8_t style = 0) {
  SwfBuilder body;
  body.u16(id);
  body.sized_text(name);
  body.u8(0x01 | style);
  body.u8(language);
  for (const std::uint16_t code : codes) {
    body.u16(code);
  }
  return body.body;
}

// Constraint: DefineFontName is id, a null-terminated name, then a null-terminated copyright.
inline std::vector<std::byte> font_name_payload(std::uint16_t id, const std::string& name,
                                                const std::string& copyright) {
  SwfBuilder body;
  body.u16(id);
  body.text(name);
  body.text(copyright);
  return body.body;
}

// Constraint: SymbolClass is a UI16 count then, per symbol, a UI16 character id and a null-terminated class name.
inline std::vector<std::byte> symbol_class_payload(
    const std::vector<std::pair<std::uint16_t, std::string>>& symbols) {
  SwfBuilder body;
  body.u16(static_cast<std::uint32_t>(symbols.size()));
  for (const auto& [id, name] : symbols) {
    body.u16(id);
    body.text(name);
  }
  return body.body;
}

}  // namespace swf_test
