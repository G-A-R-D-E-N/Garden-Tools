#pragma once

#include <fo4/avm2/sound.hpp>

#include <cstdint>
#include <vector>

namespace fo4::avm2 {

/* A decoded body must carry interleaved signed 16-bit samples so the host can feed an audio device. */
struct DecodedSound {
  std::uint32_t sample_rate{44100};
  std::uint16_t channels{1};
  std::vector<std::int16_t> samples;
};

/* Requires a DefineSound body; an unsupported codec must refuse instead of guessing. */
bool decode_sound(const SoundResource& resource, DecodedSound& out);

}  // namespace fo4::avm2
