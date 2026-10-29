#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::swf {

enum class SwfSoundStatus : std::uint8_t { ok, truncated, unknown_compression };

struct SwfSoundEnvelopePoint {
  std::uint32_t position{0};
  std::uint16_t left{0};
  std::uint16_t right{0};
};

// Invariant: each optional is present exactly when its SOUNDINFO flag bit is set.
struct SwfSoundInfo {
  bool sync_stop{false};
  bool sync_no_multiple{false};
  std::optional<std::uint32_t> in_point;
  std::optional<std::uint32_t> out_point;
  std::optional<std::uint16_t> loop_count;
  std::optional<std::vector<SwfSoundEnvelopePoint>> envelope;
};

struct SwfStartSound {
  std::uint16_t sound_id{0};
  SwfSoundInfo info;
};

struct SwfStartSound2 {
  std::string class_name;
  SwfSoundInfo info;
};

struct SwfDefineSoundData {
  SwfSound header;
  std::span<const std::byte> data;
};

/* Requires the decoded payload to be read-only and any refusal leaves out empty. */
SwfSoundStatus decode_start_sound(std::span<const std::byte> payload, SwfStartSound& out);

/* Requires the decoded payload to be read-only and any refusal leaves out empty. */
SwfSoundStatus decode_start_sound2(std::span<const std::byte> payload, SwfStartSound2& out);

/* Requires the payload to outlive out.data, which is a view into it and is not decoded. */
SwfSoundStatus decode_define_sound(std::span<const std::byte> payload, SwfDefineSoundData& out);

/* Requires the stream compression to be a defined code, and MP3 heads to carry their latency. */
SwfSoundStatus decode_sound_stream_head(std::span<const std::byte> payload,
                                        SwfSoundStreamHead& out);

}  // namespace fo4::swf
