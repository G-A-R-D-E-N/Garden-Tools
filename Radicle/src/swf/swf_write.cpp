#include "fo4/swf/swf.hpp"

#include "swf_internal.hpp"

#include <zlib.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fo4::swf {

namespace {

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 24) & 0xFFu));
}

std::byte sound_format_byte(unsigned high, unsigned rate, bool sample_16bit, bool stereo) noexcept {
  return static_cast<std::byte>(((high & 0x0Fu) << 4) | ((rate & 0x03u) << 2) |
                                (sample_16bit ? 0x02u : 0u) | (stereo ? 0x01u : 0u));
}

unsigned bits_for(std::int32_t value) noexcept {
  if (value == 0) {
    return 0;
  }
  const auto magnitude =
      value < 0 ? static_cast<std::uint32_t>(~value) : static_cast<std::uint32_t>(value);
  return 33u - static_cast<unsigned>(std::countl_zero(magnitude));
}

unsigned rect_bits(const SwfRect& rect) noexcept {
  unsigned bits = 0;
  for (const auto value : {rect.x_min, rect.x_max, rect.y_min, rect.y_max}) {
    bits = std::max(bits, bits_for(value));
  }
  return bits;
}

class BitWriter {
public:
  explicit BitWriter(std::vector<std::byte>& out) noexcept : out_(out) {}

  void write_bits(std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
      const auto bit = (value >> (count - 1u - index)) & 1u;
      pending_ = static_cast<std::uint8_t>((pending_ << 1) | bit);
      ++filled_;
      if (filled_ == 8) {
        out_.push_back(static_cast<std::byte>(pending_));
        pending_ = 0;
        filled_ = 0;
      }
    }
  }

  void align(std::uint32_t fill) { write_bits(fill, (8u - filled_) % 8u); }

private:
  std::vector<std::byte>& out_;
  std::uint8_t pending_{0};
  unsigned filled_{0};
};

void append_rect(std::vector<std::byte>& out, const SwfRect& rect) {
  const auto needed = rect_bits(rect);
  unsigned nbits = rect.nbits;
  if (nbits < needed) {
    nbits = needed;
  }
  if (nbits > 31) {
    throw std::runtime_error("SWF frame size rectangle needs more than 31 bits per field");
  }
  const auto mask = nbits == 0 ? 0u : ((1u << nbits) - 1u);
  BitWriter writer(out);
  writer.write_bits(nbits, 5);
  writer.write_bits(static_cast<std::uint32_t>(rect.x_min) & mask, nbits);
  writer.write_bits(static_cast<std::uint32_t>(rect.x_max) & mask, nbits);
  writer.write_bits(static_cast<std::uint32_t>(rect.y_min) & mask, nbits);
  writer.write_bits(static_cast<std::uint32_t>(rect.y_max) & mask, nbits);
  writer.align(rect.padding);
}

std::vector<std::byte> deflate_body(std::span<const std::byte> body) {
  if (body.empty()) {
    return {};
  }
  if (body.size() > std::numeric_limits<uLong>::max()) {
    throw std::length_error("SWF body exceeds the zlib size limit");
  }
  uLongf packed_size = compressBound(static_cast<uLong>(body.size()));
  std::vector<std::byte> packed(static_cast<std::size_t>(packed_size));
  const auto result = compress2(reinterpret_cast<Bytef*>(packed.data()), &packed_size,
                                reinterpret_cast<const Bytef*>(body.data()),
                                static_cast<uLong>(body.size()), Z_BEST_COMPRESSION);
  if (result != Z_OK) {
    throw std::runtime_error("zlib failed to compress the SWF body");
  }
  packed.resize(static_cast<std::size_t>(packed_size));
  return packed;
}

}  // namespace

std::vector<std::byte> write_swf(const SwfFile& file, SwfCompression compression) {
  std::vector<std::byte> body;
  append_rect(body, file.header.frame_size);
  append_u16(body, file.header.frame_rate);
  append_u16(body, file.header.frame_count);
  for (const auto& current : file.tags) {
    const auto length = current.payload.size();
    const auto code = static_cast<std::uint16_t>((current.code & 0x3FFu) << 6);
    if (current.long_header || length >= 0x3Fu) {
      if (length > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("SWF tag payload exceeds the 32-bit tag length field");
      }
      append_u16(body, static_cast<std::uint16_t>(code | 0x3Fu));
      append_u32(body, static_cast<std::uint32_t>(length));
    } else {
      append_u16(body, static_cast<std::uint16_t>(code | (length & 0x3Fu)));
    }
    body.insert(body.end(), current.payload.begin(), current.payload.end());
  }
  body.insert(body.end(), file.trailing_bytes.begin(), file.trailing_bytes.end());

  const std::size_t actual = body.size() + 8;
  if (actual > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("SWF file exceeds the 32-bit file length field");
  }
  const bool unchanged = !file.source_body.empty() && body.size() == file.source_body.size() &&
                         std::equal(body.begin(), body.end(), file.source_body.begin());
  const auto length_field =
      unchanged ? file.header.file_length : static_cast<std::uint32_t>(actual);

  const bool gfx = file.header.format == SwfFormat::gfx;
  if (gfx && compression == SwfCompression::lzma) {
    throw std::runtime_error("GFx movies have no LZMA (ZWS) signature");
  }
  const char* signature = nullptr;
  if (gfx) {
    signature = compression == SwfCompression::zlib ? "CFX" : "GFX";
  } else if (compression == SwfCompression::lzma) {
    signature = "ZWS";
  } else {
    signature = compression == SwfCompression::zlib ? "CWS" : "FWS";
  }

  std::vector<std::byte> out;
  out.reserve(actual);
  out.push_back(static_cast<std::byte>(signature[0]));
  out.push_back(static_cast<std::byte>(signature[1]));
  out.push_back(static_cast<std::byte>(signature[2]));
  out.push_back(static_cast<std::byte>(file.header.version));
  append_u32(out, length_field);
  if (compression == SwfCompression::none) {
    out.insert(out.end(), body.begin(), body.end());
    return out;
  }
  if (unchanged && file.source_compressed_body_known && compression == file.header.compression) {
    out.insert(out.end(), file.source_compressed_body.begin(), file.source_compressed_body.end());
    return out;
  }
  if (compression == SwfCompression::lzma) {
    const auto packed = detail::lzma_encode_swf(body);
    if (packed.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("SWF LZMA stream exceeds the 32-bit compressed length field");
    }
    append_u32(out, static_cast<std::uint32_t>(packed.size()));
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
  }
  const auto packed = deflate_body(body);
  out.insert(out.end(), packed.begin(), packed.end());
  return out;
}

std::vector<std::byte> write_swf(const SwfFile& file) {
  return write_swf(file, file.header.compression);
}

namespace {

// Constraint: an edge delta requires 2..17 bits and a MoveTo requires 0..31 bits, so wider values are refused.
constexpr unsigned kMaxEdgeBits = 17;
constexpr unsigned kMaxMoveBits = 31;

unsigned edge_bits(std::int32_t value) noexcept { return std::max(2u, bits_for(value)); }

bool write_index(BitWriter& writer, std::uint16_t index, unsigned bits, std::string& message) {
  if ((static_cast<std::uint32_t>(index) >> bits) != 0) {
    message = "a glyph style index does not fit the SHAPE header bit width";
    return false;
  }
  writer.write_bits(index, bits);
  return true;
}

bool write_style_change(BitWriter& writer, const SwfGlyph& glyph, const SwfShapeRecord* move,
                        const SwfShapeRecord* style, std::string& message) {
  std::uint32_t flags = move != nullptr ? 0x01u : 0u;
  if (style != nullptr) {
    flags |= style->fill0 != 0 ? 0x02u : 0u;
    flags |= style->fill1 != 0 ? 0x04u : 0u;
    flags |= style->line != 0 ? 0x08u : 0u;
  }
  if (flags == 0) {
    flags = 0x02u;
  }
  writer.write_bits(0, 1);
  writer.write_bits(flags, 5);
  if (move != nullptr) {
    const unsigned bits = std::max(bits_for(move->x), bits_for(move->y));
    if (bits > kMaxMoveBits) {
      message = "a glyph MoveTo needs more than 31 bits";
      return false;
    }
    writer.write_bits(bits, 5);
    writer.write_bits(static_cast<std::uint32_t>(move->x), bits);
    writer.write_bits(static_cast<std::uint32_t>(move->y), bits);
  }
  if ((flags & 0x02u) != 0 && !write_index(writer, style->fill0, glyph.fill_bits, message)) {
    return false;
  }
  if ((flags & 0x04u) != 0 && !write_index(writer, style->fill1, glyph.fill_bits, message)) {
    return false;
  }
  if ((flags & 0x08u) != 0 && !write_index(writer, style->line, glyph.line_bits, message)) {
    return false;
  }
  return true;
}

bool write_edge(BitWriter& writer, const SwfShapeRecord& record, std::string& message) {
  if (record.kind == SwfShapeRecordKind::straight_edge) {
    const bool vertical = record.x == 0 && record.y != 0;
    const bool horizontal = record.y == 0 && record.x != 0;
    const unsigned width = vertical     ? edge_bits(record.y)
                           : horizontal ? edge_bits(record.x)
                                        : std::max(edge_bits(record.x), edge_bits(record.y));
    if (width > kMaxEdgeBits) {
      message = "a glyph edge needs more than 17 bits";
      return false;
    }
    writer.write_bits(1, 1);
    writer.write_bits(1, 1);
    writer.write_bits(width - 2u, 4);
    if (vertical || horizontal) {
      writer.write_bits(0, 1);
      writer.write_bits(vertical ? 1u : 0u, 1);
      writer.write_bits(static_cast<std::uint32_t>(vertical ? record.y : record.x), width);
    } else {
      writer.write_bits(1, 1);
      writer.write_bits(static_cast<std::uint32_t>(record.x), width);
      writer.write_bits(static_cast<std::uint32_t>(record.y), width);
    }
    return true;
  }
  const unsigned width =
      std::max(std::max(edge_bits(record.control_x), edge_bits(record.control_y)),
               std::max(edge_bits(record.x), edge_bits(record.y)));
  if (width > kMaxEdgeBits) {
    message = "a glyph edge needs more than 17 bits";
    return false;
  }
  writer.write_bits(1, 1);
  writer.write_bits(0, 1);
  writer.write_bits(width - 2u, 4);
  writer.write_bits(static_cast<std::uint32_t>(record.control_x), width);
  writer.write_bits(static_cast<std::uint32_t>(record.control_y), width);
  writer.write_bits(static_cast<std::uint32_t>(record.x), width);
  writer.write_bits(static_cast<std::uint32_t>(record.y), width);
  return true;
}

bool append_offset(std::vector<std::byte>& out, std::size_t value, bool wide,
                   std::string& message) {
  if (wide) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      message = "a glyph offset does not fit a UI32 table entry";
      return false;
    }
    append_u32(out, static_cast<std::uint32_t>(value));
    return true;
  }
  if (value > 0xFFFFu) {
    message = "a glyph offset does not fit a UI16 table entry";
    return false;
  }
  append_u16(out, static_cast<std::uint16_t>(value));
  return true;
}

bool append_font_code(std::vector<std::byte>& out, std::uint16_t code, bool wide,
                      std::string& message) {
  if (wide) {
    append_u16(out, code);
    return true;
  }
  if (code > 0xFFu) {
    message = "a font code does not fit a UI8 entry";
    return false;
  }
  out.push_back(static_cast<std::byte>(code));
  return true;
}

bool append_codes(std::vector<std::byte>& out, const SwfFont& font, bool wide,
                  std::string& message) {
  for (const std::uint16_t code : font.glyph_codes) {
    if (!append_font_code(out, code, wide, message)) {
      return false;
    }
  }
  return true;
}

bool append_layout(std::vector<std::byte>& out, const SwfFont& font, bool wide,
                   std::string& message) {
  const std::size_t count = font.glyphs.size();
  append_u16(out, font.ascent);
  append_u16(out, font.descent);
  append_u16(out, static_cast<std::uint16_t>(font.leading));
  for (std::size_t index = 0; index < count; ++index) {
    const long long scaled = std::llround(font.advances[index] * font.units_per_em);
    if (scaled < std::numeric_limits<std::int16_t>::min() ||
        scaled > std::numeric_limits<std::int16_t>::max()) {
      message = "a glyph advance does not fit an SI16 field";
      return false;
    }
    append_u16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
  }
  for (const SwfRect& rect : font.glyph_bounds) {
    if (std::max(rect_bits(rect), static_cast<unsigned>(rect.nbits)) > 31) {
      message = "a glyph bounds rectangle needs more than 31 bits per field";
      return false;
    }
    append_rect(out, rect);
  }
  if (font.kerning.size() > 0xFFFFu) {
    message = "the kerning table holds more than 65535 pairs";
    return false;
  }
  append_u16(out, static_cast<std::uint16_t>(font.kerning.size()));
  for (const SwfKerning& pair : font.kerning) {
    if (!append_font_code(out, pair.code1, wide, message) ||
        !append_font_code(out, pair.code2, wide, message)) {
      return false;
    }
    append_u16(out, static_cast<std::uint16_t>(pair.adjustment));
  }
  return true;
}

}  // namespace

std::vector<std::byte> encode_define_sound(const SwfSound& sound, std::span<const std::byte> data) {
  std::vector<std::byte> out;
  append_u16(out, sound.sound_id);
  out.push_back(sound_format_byte(sound.format, sound.rate, sound.sample_16bit, sound.stereo));
  append_u32(out, sound.sample_count);
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

std::vector<std::byte> encode_sound_stream_head(const SwfSoundStreamHead& head) {
  std::vector<std::byte> out;
  out.push_back(sound_format_byte(head.reserved, head.playback_rate, head.playback_16bit,
                                  head.playback_stereo));
  out.push_back(sound_format_byte(head.compression, head.rate, head.sample_16bit, head.stereo));
  append_u16(out, head.sample_count);
  if (head.latency_seek.has_value()) {
    append_u16(out, static_cast<std::uint16_t>(*head.latency_seek));
  }
  return out;
}

bool encode_glyph_shape(const SwfGlyph& glyph, std::vector<std::byte>& out, std::string& message) {
  out.clear();
  message.clear();
  if (glyph.fill_bits > 15 || glyph.line_bits > 15) {
    message = "a glyph SHAPE header nibble holds at most 15 bits";
    return false;
  }
  BitWriter writer(out);
  writer.write_bits((static_cast<std::uint32_t>(glyph.fill_bits) << 4) | glyph.line_bits, 8);
  for (std::size_t index = 0; index < glyph.records.size(); ++index) {
    const SwfShapeRecord& record = glyph.records[index];
    if (record.kind == SwfShapeRecordKind::move || record.kind == SwfShapeRecordKind::style_change) {
      const SwfShapeRecord* move = nullptr;
      const SwfShapeRecord* style = nullptr;
      if (record.kind == SwfShapeRecordKind::move) {
        move = &record;
        if (index + 1 < glyph.records.size() &&
            glyph.records[index + 1].kind == SwfShapeRecordKind::style_change) {
          style = &glyph.records[++index];
        }
      } else {
        style = &record;
      }
      if (!write_style_change(writer, glyph, move, style, message)) {
        return false;
      }
    } else if (!write_edge(writer, record, message)) {
      return false;
    }
  }
  writer.write_bits(0, 6);
  writer.align(0);
  return true;
}

bool encode_define_font(const SwfFont& font, std::uint16_t code, std::vector<std::byte>& out,
                        std::string& message) {
  out.clear();
  message.clear();
  if (code != tag::kDefineFont && code != tag::kDefineFont2 && code != tag::kDefineFont3) {
    message = "the tag is not a DefineFont, DefineFont2 or DefineFont3";
    return false;
  }
  const std::size_t count = font.glyphs.size();
  const bool define_font = code == tag::kDefineFont;
  const bool wide_codes = code == tag::kDefineFont3 || font.wide_codes;
  const bool layout = !define_font && font.has_layout;
  if (count != font.glyph_count || (define_font && count > 0x7FFFu)) {
    message = "the glyph list does not match the glyph count";
    return false;
  }
  if ((!define_font && font.glyph_codes.size() != count) ||
      (layout && (font.advances.size() != count || font.glyph_bounds.size() != count))) {
    message = "the code and layout tables do not match the glyph count";
    return false;
  }
  const bool wide_offsets = !define_font && font.wide_offsets;
  std::vector<std::vector<std::byte>> shapes(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (!encode_glyph_shape(font.glyphs[index], shapes[index], message)) {
      return false;
    }
  }

  append_u16(out, font.font_id);
  if (!define_font) {
    if (font.name.size() > 0xFFu) {
      message = "the font name does not fit a UI8 length";
      return false;
    }
    out.push_back(static_cast<std::byte>(font.flags));
    out.push_back(static_cast<std::byte>(font.language));
    out.push_back(static_cast<std::byte>(font.name.size()));
    out.insert(out.end(), reinterpret_cast<const std::byte*>(font.name.data()),
               reinterpret_cast<const std::byte*>(font.name.data()) + font.name.size());
    append_u16(out, static_cast<std::uint16_t>(count));
  }

  const std::size_t entry = wide_offsets ? 4u : 2u;
  std::size_t shape_offset = count * entry + (define_font ? 0u : entry);
  for (const auto& shape : shapes) {
    if (!append_offset(out, shape_offset, wide_offsets, message)) {
      return false;
    }
    shape_offset += shape.size();
  }
  if (!define_font && !append_offset(out, shape_offset, wide_offsets, message)) {
    return false;
  }
  for (const auto& shape : shapes) {
    out.insert(out.end(), shape.begin(), shape.end());
  }
  if (!define_font && !append_codes(out, font, wide_codes, message)) {
    return false;
  }
  if (layout && !append_layout(out, font, wide_codes, message)) {
    return false;
  }
  return out.size() <= std::numeric_limits<std::uint32_t>::max();
}

}  // namespace fo4::swf
