#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::swf::detail {

namespace {

bool read_matrix_at(std::span<const std::byte> data, std::size_t offset,
                    std::size_t& next) noexcept {
  BitReader bits(data, offset * 8);
  std::uint32_t flag = 0;
  std::uint32_t nbits = 0;
  std::int32_t value = 0;
  if (!bits.read_unsigned(1, flag)) {
    return false;
  }
  if (flag != 0) {
    if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, value) ||
        !bits.read_signed(nbits, value)) {
      return false;
    }
  }
  if (!bits.read_unsigned(1, flag)) {
    return false;
  }
  if (flag != 0) {
    if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, value) ||
        !bits.read_signed(nbits, value)) {
      return false;
    }
  }
  if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, value) ||
      !bits.read_signed(nbits, value)) {
    return false;
  }
  next = bits.byte_position();
  return true;
}

bool decode_text_records(std::span<const std::byte> data, std::size_t offset, bool rgba,
                         unsigned glyph_bits, unsigned advance_bits, SwfText& out) noexcept {
  std::size_t position = offset;
  std::size_t records = 0;
  std::size_t glyphs = 0;
  while (position < data.size()) {
    const auto flags = byte_at(data, position);
    ++position;
    if (flags == 0) {
      out.record_count = records;
      out.glyph_count = glyphs;
      return true;
    }
    if ((flags & 0x80u) == 0) {
      return false;
    }
    const bool has_font = (flags & 0x08u) != 0;
    const bool has_color = (flags & 0x04u) != 0;
    const bool has_y_offset = (flags & 0x02u) != 0;
    const bool has_x_offset = (flags & 0x01u) != 0;
    std::size_t needed = has_font ? 4u : 0u;
    if (has_color) {
      needed += rgba ? 4u : 3u;
    }
    if (has_x_offset) {
      needed += 2u;
    }
    if (has_y_offset) {
      needed += 2u;
    }
    if (data.size() - position < needed + 1u) {
      return false;
    }
    position += needed;
    const auto count = static_cast<std::size_t>(byte_at(data, position));
    ++position;
    BitReader bits(data, position * 8);
    for (std::size_t index = 0; index < count; ++index) {
      std::uint32_t glyph_index = 0;
      std::int32_t advance = 0;
      if (!bits.read_unsigned(glyph_bits, glyph_index) ||
          !bits.read_signed(advance_bits, advance)) {
        return false;
      }
    }
    position = bits.byte_position();
    glyphs += count;
    ++records;
  }
  return false;
}

}  // namespace

SwfStatus decode_text_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  switch (out_tag.code) {
    case tag::kDefineFont: {
      SwfReader reader(data, 0);
      SwfFont font;
      std::uint16_t first_offset = 0;
      if (!reader.read_u16(font.font_id) || !reader.read_u16(first_offset) ||
          (first_offset % 2u) != 0) {
        return SwfStatus::malformed_tag;
      }
      font.glyph_count = static_cast<std::uint32_t>(first_offset / 2u);
      if (reader.remaining() < static_cast<std::size_t>(font.glyph_count) * 2u) {
        return SwfStatus::malformed_tag;
      }
      out_tag.font = font;
      return SwfStatus::ok;
    }
    case tag::kDefineFont2:
    case tag::kDefineFont3: {
      SwfReader reader(data, 0);
      SwfFont font;
      std::uint8_t name_length = 0;
      std::uint16_t glyph_count = 0;
      if (!reader.read_u16(font.font_id) || !reader.read_u8(font.flags) ||
          !reader.read_u8(font.language) || !reader.read_u8(name_length) ||
          reader.remaining() < static_cast<std::size_t>(name_length)) {
        return SwfStatus::malformed_tag;
      }
      if (name_length != 0) {
        font.name.assign(reinterpret_cast<const char*>(data.data() + reader.position()),
                         name_length);
        reader.skip(name_length);
      }
      if (!reader.read_u16(glyph_count)) {
        return SwfStatus::malformed_tag;
      }
      font.glyph_count = glyph_count;
      font.wide_offsets = (font.flags & 0x08u) != 0;
      font.wide_codes = (font.flags & 0x04u) != 0;
      const auto entry = font.wide_offsets ? std::size_t{4} : std::size_t{2};
      if (reader.remaining() < static_cast<std::size_t>(glyph_count) * entry + entry) {
        return SwfStatus::malformed_tag;
      }
      out_tag.font = font;
      return SwfStatus::ok;
    }
    case tag::kDefineText:
    case tag::kDefineText2: {
      SwfReader reader(data, 0);
      SwfText text;
      std::size_t bounds_next = 0;
      std::size_t matrix_next = 0;
      std::uint8_t glyph_bits = 0;
      std::uint8_t advance_bits = 0;
      if (!reader.read_u16(text.character_id) ||
          !read_rect(data, reader.position(), text.bounds, bounds_next) ||
          !read_matrix_at(data, bounds_next, matrix_next)) {
        return SwfStatus::malformed_tag;
      }
      SwfReader after_matrix(data, matrix_next);
      if (!after_matrix.read_u8(glyph_bits) || !after_matrix.read_u8(advance_bits) ||
          !decode_text_records(data, after_matrix.position(),
                               out_tag.code == tag::kDefineText2, glyph_bits, advance_bits, text)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.text = text;
      return SwfStatus::ok;
    }
    default:
      return SwfStatus::ok;
  }
}

}  // namespace fo4::swf::detail
