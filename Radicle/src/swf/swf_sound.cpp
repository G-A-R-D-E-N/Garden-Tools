#include "fo4/swf/swf_sound.hpp"

#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

using detail::SwfReader;

bool known_compression(std::uint8_t code) noexcept {
  return code <= 8 || code == 10 || code == 11;
}

bool read_sound_info(SwfReader& reader, SwfSoundInfo& out) {
  std::uint8_t flags = 0;
  if (!reader.read_u8(flags)) {
    return false;
  }
  out.sync_stop = (flags & 0x20u) != 0;
  out.sync_no_multiple = (flags & 0x10u) != 0;
  if ((flags & 0x01u) != 0) {
    std::uint32_t in_point = 0;
    if (!reader.read_u32(in_point)) {
      return false;
    }
    out.in_point = in_point;
  }
  if ((flags & 0x02u) != 0) {
    std::uint32_t out_point = 0;
    if (!reader.read_u32(out_point)) {
      return false;
    }
    out.out_point = out_point;
  }
  if ((flags & 0x04u) != 0) {
    std::uint16_t loops = 0;
    if (!reader.read_u16(loops)) {
      return false;
    }
    out.loop_count = loops;
  }
  if ((flags & 0x08u) != 0) {
    std::uint8_t points = 0;
    if (!reader.read_u8(points)) {
      return false;
    }
    std::vector<SwfSoundEnvelopePoint> envelope;
    for (std::uint8_t i = 0; i < points; ++i) {
      SwfSoundEnvelopePoint point;
      if (!reader.read_u32(point.position) || !reader.read_u16(point.left) ||
          !reader.read_u16(point.right)) {
        return false;
      }
      envelope.push_back(point);
    }
    out.envelope = std::move(envelope);
  }
  return true;
}

}  // namespace

SwfSoundStatus decode_start_sound(std::span<const std::byte> payload, SwfStartSound& out) {
  out = {};
  SwfReader reader(payload, 0);
  SwfStartSound result;
  if (!reader.read_u16(result.sound_id) || !read_sound_info(reader, result.info)) {
    return SwfSoundStatus::truncated;
  }
  out = std::move(result);
  return SwfSoundStatus::ok;
}

SwfSoundStatus decode_start_sound2(std::span<const std::byte> payload, SwfStartSound2& out) {
  out = {};
  SwfReader reader(payload, 0);
  SwfStartSound2 result;
  if (!reader.read_zstring(result.class_name) || !read_sound_info(reader, result.info)) {
    return SwfSoundStatus::truncated;
  }
  out = std::move(result);
  return SwfSoundStatus::ok;
}

SwfSoundStatus decode_define_sound(std::span<const std::byte> payload, SwfDefineSoundData& out) {
  out = {};
  SwfReader reader(payload, 0);
  SwfSound header;
  std::uint8_t flags = 0;
  if (!reader.read_u16(header.sound_id) || !reader.read_u8(flags) ||
      !reader.read_u32(header.sample_count)) {
    return SwfSoundStatus::truncated;
  }
  header.format = static_cast<std::uint8_t>(flags >> 4);
  if (!known_compression(header.format)) {
    return SwfSoundStatus::unknown_compression;
  }
  header.rate = static_cast<std::uint8_t>((flags >> 2) & 0x03u);
  header.sample_16bit = (flags & 0x02u) != 0;
  header.stereo = (flags & 0x01u) != 0;
  header.data_size = reader.remaining();
  out.header = header;
  out.data = payload.subspan(reader.position());
  return SwfSoundStatus::ok;
}

SwfSoundStatus decode_sound_stream_head(std::span<const std::byte> payload,
                                        SwfSoundStreamHead& out) {
  out = {};
  SwfReader reader(payload, 0);
  SwfSoundStreamHead head;
  std::uint8_t playback = 0;
  std::uint8_t stream = 0;
  if (!reader.read_u8(playback) || !reader.read_u8(stream) ||
      !reader.read_u16(head.sample_count)) {
    return SwfSoundStatus::truncated;
  }
  head.reserved = static_cast<std::uint8_t>(playback >> 4);
  head.playback_rate = static_cast<std::uint8_t>((playback >> 2) & 0x03u);
  head.playback_16bit = (playback & 0x02u) != 0;
  head.playback_stereo = (playback & 0x01u) != 0;
  head.compression = static_cast<std::uint8_t>(stream >> 4);
  if (!known_compression(head.compression)) {
    return SwfSoundStatus::unknown_compression;
  }
  head.rate = static_cast<std::uint8_t>((stream >> 2) & 0x03u);
  head.sample_16bit = (stream & 0x02u) != 0;
  head.stereo = (stream & 0x01u) != 0;
  if (head.compression == 2) {
    std::int16_t latency = 0;
    if (!reader.read_s16(latency)) {
      return SwfSoundStatus::truncated;
    }
    head.latency_seek = latency;
  }
  out = std::move(head);
  return SwfSoundStatus::ok;
}

}  // namespace fo4::swf
