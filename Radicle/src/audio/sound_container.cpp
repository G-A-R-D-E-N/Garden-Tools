#include "fo4/audio/sound_container.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::audio {
namespace {

constexpr Signature k_fuze = Signature::from_string("FUZE");
constexpr Signature k_riff = Signature::from_string("RIFF");
constexpr Signature k_xwma = Signature::from_string("XWMA");
constexpr Signature k_fmt = Signature::from_string("fmt ");
constexpr Signature k_dpds = Signature::from_string("dpds");
constexpr Signature k_data = Signature::from_string("data");

constexpr std::size_t k_fuz_header_size = 12;
constexpr std::size_t k_riff_size_base = 8;
constexpr std::size_t k_chunk_header_size = 8;
constexpr std::size_t k_wma_v2_format_size = 18;
constexpr std::uint32_t k_fuz_version = 1;

std::size_t padded(std::size_t size) { return size + (size & 1u); }

Signature peek_signature(const binary::Cursor& cursor) {
  const auto bytes = cursor.peek(4);
  return Signature{static_cast<char>(std::to_integer<std::uint8_t>(bytes[0])),
                   static_cast<char>(std::to_integer<std::uint8_t>(bytes[1])),
                   static_cast<char>(std::to_integer<std::uint8_t>(bytes[2])),
                   static_cast<char>(std::to_integer<std::uint8_t>(bytes[3]))};
}

[[noreturn]] void fail(std::size_t offset, const char* message) {
  throw parse_error(offset, message);
}

}  // namespace

SoundContainer parse_sound_container(std::span<const std::byte> bytes) {
  SoundContainer container;
  binary::Cursor cursor{bytes};

  if (peek_signature(cursor) == k_fuze) {
    container.fuz = true;
    cursor.seek(4);
    if (cursor.read_u32() != k_fuz_version) {
      fail(4, "FUZE version is not 1");
    }
    const auto lip_size = cursor.read_u32();
    if (lip_size > cursor.remaining()) {
      fail(8, "FUZE lip size exceeds the remaining data");
    }
    container.lip_offset = k_fuz_header_size;
    container.lip_size = lip_size;
    cursor.seek(k_fuz_header_size + lip_size);
  }

  container.riff_offset = cursor.position();
  if (peek_signature(cursor) != k_riff) {
    fail(container.riff_offset, "sound payload is not a RIFF stream");
  }
  cursor.seek(container.riff_offset + 4);
  container.riff_size = cursor.read_u32();
  const auto riff_end = container.riff_offset + k_riff_size_base +
                        static_cast<std::size_t>(container.riff_size);
  if (riff_end > bytes.size()) {
    fail(container.riff_offset + 4, "RIFF size runs past the end of the buffer");
  }
  if (riff_end != bytes.size()) {
    fail(riff_end, "trailing bytes after the RIFF stream");
  }
  if (cursor.read_signature() != k_xwma) {
    fail(container.riff_offset + 8, "RIFF form is not XWMA");
  }

  bool have_fmt = false;
  bool have_dpds = false;
  bool have_data = false;
  while (cursor.position() < riff_end) {
    const auto chunk_offset = cursor.position();
    if (riff_end - chunk_offset < k_chunk_header_size) {
      fail(chunk_offset, "truncated RIFF chunk header");
    }
    const auto id = cursor.read_signature();
    const auto size = cursor.read_u32();
    const auto payload_offset = cursor.position();
    const auto padded_size = padded(static_cast<std::size_t>(size));
    if (padded_size > riff_end - payload_offset) {
      fail(chunk_offset, "RIFF chunk runs past the end of the stream");
    }
    if (id == k_fmt) {
      if (have_fmt) {
        fail(chunk_offset, "duplicate fmt chunk");
      }
      if (size != k_wma_v2_format_size) {
        fail(chunk_offset + 4, "fmt chunk is not a bare 18-byte WAVEFORMATEX");
      }
      auto& format = container.format;
      format.format_tag = cursor.read_u16();
      format.channels = cursor.read_u16();
      format.sample_rate = cursor.read_u32();
      format.average_bytes_per_second = cursor.read_u32();
      format.block_align = cursor.read_u16();
      format.bits_per_sample = cursor.read_u16();
      if (format.format_tag != k_wma_v2_format_tag) {
        fail(payload_offset, "format tag is not WMA v2");
      }
      if (format.channels == 0) {
        fail(payload_offset + 2, "channel count is zero");
      }
      if (format.sample_rate == 0) {
        fail(payload_offset + 4, "sample rate is zero");
      }
      if (format.average_bytes_per_second == 0) {
        fail(payload_offset + 8, "byte rate is zero");
      }
      if (format.block_align == 0) {
        fail(payload_offset + 12, "block alignment is zero");
      }
      if (format.bits_per_sample != 16) {
        fail(payload_offset + 14, "bits per sample is not 16");
      }
      have_fmt = true;
    } else if (id == k_dpds) {
      if (have_dpds) {
        fail(chunk_offset, "duplicate dpds chunk");
      }
      if (size == 0 || (size % 4) != 0) {
        fail(chunk_offset + 4, "dpds chunk is not a whole number of packets");
      }
      container.packet_count = static_cast<std::size_t>(size) / 4;
      have_dpds = true;
    } else if (id == k_data) {
      if (have_data) {
        fail(chunk_offset, "duplicate data chunk");
      }
      if (size == 0) {
        fail(chunk_offset + 4, "data chunk is empty");
      }
      container.data_offset = payload_offset;
      container.data_size = static_cast<std::size_t>(size);
      have_data = true;
    } else {
      fail(chunk_offset, "unexpected RIFF chunk in a sound payload");
    }
    cursor.seek(payload_offset + padded_size);
  }

  if (!have_fmt) {
    fail(riff_end, "sound payload has no fmt chunk");
  }
  if (!have_dpds) {
    fail(riff_end, "sound payload has no dpds chunk");
  }
  if (!have_data) {
    fail(riff_end, "sound payload has no data chunk");
  }
  return container;
}

}  // namespace fo4::audio
