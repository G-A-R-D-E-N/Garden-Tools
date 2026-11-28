#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace fo4::audio {

struct PitchShiftSettings {
  double semitones{0.0};
  std::size_t window{1024};
  std::size_t hop{256};
};

std::vector<float> pitch_shift(std::span<const float> samples, const PitchShiftSettings& settings);

}  // namespace fo4::audio
