#include "fo4/audio/pitch_shift.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::audio {
namespace {

constexpr double k_pi = 3.14159265358979323846;
constexpr double k_max_semitones = 48.0;

std::vector<float> hann_window(std::size_t size) {
  std::vector<float> window(size);
  for (std::size_t index = 0; index < size; ++index) {
    const auto phase = 2.0 * k_pi * static_cast<double>(index) / static_cast<double>(size);
    window[index] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
  }
  return window;
}

float sample_at(std::span<const float> samples, double position) {
  if (samples.empty() || position < 0.0 || position > static_cast<double>(samples.size() - 1)) {
    return 0.0f;
  }
  const auto index = static_cast<std::size_t>(position);
  const auto fraction = static_cast<float>(position - static_cast<double>(index));
  const auto next = index + 1 < samples.size() ? samples[index + 1] : samples[index];
  return samples[index] + (next - samples[index]) * fraction;
}

std::vector<std::size_t> grain_starts(std::size_t length, std::size_t window, std::size_t hop) {
  std::vector<std::size_t> starts;
  for (std::size_t start = 0; start + window <= length; start += hop) {
    starts.push_back(start);
  }
  if (starts.empty()) {
    starts.push_back(0);
  } else if (starts.back() + window < length) {
    starts.push_back(length - window);
  }
  return starts;
}

std::vector<float> time_stretch(std::span<const float> samples,
                                double factor,
                                std::size_t window,
                                std::size_t hop) {
  const auto target = static_cast<std::size_t>(
      std::llround(static_cast<double>(samples.size()) * factor));
  const auto length = std::max(target, window);
  std::vector<float> output(length, 0.0f);
  std::vector<float> weight(length, 0.0f);
  const auto taper = hann_window(window);
  const auto reach = static_cast<long>(window / 8);
  const auto overlap = window - hop;
  for (const auto start : grain_starts(length, window, hop)) {
    const auto nominal = static_cast<double>(start) / factor;
    auto read_start = nominal;
    if (start > 0) {
      double best_score = -std::numeric_limits<double>::infinity();
      for (long offset = -reach; offset <= reach; ++offset) {
        const auto candidate = nominal + static_cast<double>(offset);
        double score = 0.0;
        for (std::size_t index = 0; index < overlap; ++index) {
          score += static_cast<double>(output[start + index]) *
                   sample_at(samples, candidate + static_cast<double>(index));
        }
        if (score > best_score) {
          best_score = score;
          read_start = candidate;
        }
      }
    }
    for (std::size_t index = 0; index < window; ++index) {
      output[start + index] += taper[index] * sample_at(samples, read_start + static_cast<double>(index));
      weight[start + index] += taper[index];
    }
  }
  for (std::size_t index = 0; index < length; ++index) {
    if (weight[index] > 1.0e-6f) {
      output[index] /= weight[index];
    }
  }
  return output;
}

std::vector<float> resample_linear(std::span<const float> samples, std::size_t length, double factor) {
  std::vector<float> output(length);
  for (std::size_t index = 0; index < length; ++index) {
    output[index] = sample_at(samples, static_cast<double>(index) * factor);
  }
  return output;
}

}  // namespace

std::vector<float> pitch_shift(std::span<const float> samples, const PitchShiftSettings& settings) {
  if (!std::isfinite(settings.semitones) || std::abs(settings.semitones) > k_max_semitones) {
    throw std::invalid_argument("pitch shift semitones is out of range");
  }
  if (settings.window < 16) {
    throw std::invalid_argument("pitch shift window is smaller than the minimum grain");
  }
  if (settings.hop == 0 || settings.hop > settings.window / 2) {
    throw std::invalid_argument("pitch shift hop must be between one and half the window");
  }
  std::vector<float> unchanged{samples.begin(), samples.end()};
  if (samples.size() < settings.window || settings.semitones == 0.0) {
    return unchanged;
  }
  const auto ratio = std::pow(2.0, settings.semitones / 12.0);
  const auto stretched = time_stretch(samples, ratio, settings.window, settings.hop);
  return resample_linear(stretched, samples.size(), ratio);
}

}  // namespace fo4::audio
