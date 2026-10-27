#include "fo4/swf/swf.hpp"

#include "swf_internal.hpp"

#include <zlib.h>

#include <algorithm>
#include <bit>
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
    const auto packed = lzma_encode_swf(body);
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

}  // namespace fo4::swf
