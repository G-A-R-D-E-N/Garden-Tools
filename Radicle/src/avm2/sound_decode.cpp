#include <fo4/avm2/sound_decode.hpp>

#include <fo4/avm2/sound.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::avm2 {

namespace {

/* The SWF ADPCM step/index tables must stay canonical because a wrong table decodes noise. */
constexpr std::int32_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,
    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,
    73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,
    2066,  2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

constexpr std::int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                         -1, -1, -1, -1, 2, 4, 6, 8};

std::int16_t clamp_sample(std::int32_t value) {
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<std::int16_t>(value);
}

std::uint32_t sample_rate_from(std::uint8_t rate) {
  switch (rate) {
    case 0:
      return 5512;
    case 1:
      return 11025;
    case 2:
      return 22050;
    default:
      return 44100;
  }
}

}  // namespace

bool decode_sound(const SoundResource& resource, DecodedSound& out) {
  out = {};
  out.sample_rate = sample_rate_from(resource.rate);
  out.channels = resource.stereo ? 2 : 1;
  if (resource.payload.size() <= 7 || resource.sample_count == 0) return false;
  const std::uint8_t* data = resource.payload.data() + 7;
  const std::size_t size = resource.payload.size() - 7;
  const std::uint32_t channels = out.channels;
  const std::uint32_t sample_count = resource.sample_count;

  if (resource.format == 0 || resource.format == 3) {
    if (resource.sample_16bit) {
      const std::size_t needed = static_cast<std::size_t>(sample_count) * channels * 2;
      if (size < needed) return false;
      out.samples.resize(static_cast<std::size_t>(sample_count) * channels);
      for (std::size_t index = 0; index < out.samples.size(); ++index) {
        const std::uint8_t first = data[index * 2];
        const std::uint8_t second = data[index * 2 + 1];
        const std::uint16_t raw = resource.format == 3
                                      ? static_cast<std::uint16_t>(first | (second << 8))
                                      : static_cast<std::uint16_t>((first << 8) | second);
        out.samples[index] = static_cast<std::int16_t>(raw);
      }
      return true;
    }
    const std::size_t needed = static_cast<std::size_t>(sample_count) * channels;
    if (size < needed) return false;
    out.samples.resize(needed);
    for (std::size_t index = 0; index < needed; ++index) {
      out.samples[index] = static_cast<std::int16_t>(
          static_cast<std::int16_t>(static_cast<std::int8_t>(data[index])) << 8);
    }
    return true;
  }

  if (resource.format != 1) return false;

  constexpr std::uint32_t kBlockSamples = 4096;
  std::size_t position = 0;
  std::uint32_t decoded = 0;
  while (decoded < sample_count) {
    if (position + 3 * channels > size) return false;
    std::vector<std::int32_t> predictors(channels, 0);
    std::vector<std::int32_t> indexes(channels, 0);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      predictors[channel] =
          static_cast<std::int16_t>((data[position] << 8) | data[position + 1]);
      indexes[channel] = data[position + 2];
      position += 3;
      if (indexes[channel] > 88) return false;
    }
    const std::uint32_t block_samples =
        sample_count - decoded < kBlockSamples ? sample_count - decoded : kBlockSamples;
    const std::size_t channel_bytes = block_samples / 2;
    if (position + channel_bytes * channels > size) return false;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      std::int32_t value = predictors[channel];
      std::int32_t index = indexes[channel];
      out.samples.push_back(clamp_sample(value));
      const std::uint8_t* channel_data = data + position + channel_bytes * channel;
      for (std::uint32_t sample = 1; sample < block_samples; ++sample) {
        const std::uint8_t packed = channel_data[(sample - 1) / 2];
        const std::uint8_t nibble = ((sample - 1) % 2 == 0)
                                        ? static_cast<std::uint8_t>(packed >> 4)
                                        : static_cast<std::uint8_t>(packed & 0x0Fu);
        const std::int32_t step = kStepTable[index];
        std::int32_t delta = step >> 3;
        if ((nibble & 4u) != 0) delta += step >> 2;
        if ((nibble & 2u) != 0) delta += step >> 1;
        if ((nibble & 1u) != 0) delta += step;
        if ((nibble & 8u) != 0) delta = -delta;
        value += delta;
        out.samples.push_back(clamp_sample(value));
        std::int32_t next_index = index + kIndexTable[nibble];
        if (next_index < 0) next_index = 0;
        if (next_index > 88) next_index = 88;
        index = next_index;
      }
    }
    position += channel_bytes * channels;
    decoded += block_samples;
  }
  return true;
}

}  // namespace fo4::avm2
