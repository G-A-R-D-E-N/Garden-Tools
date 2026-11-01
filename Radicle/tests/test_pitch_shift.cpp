#include "fo4/audio/pitch_shift.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

using fo4::audio::PitchShiftSettings;
using fo4::audio::pitch_shift;

namespace {

constexpr double k_sample_rate = 44100.0;
constexpr double k_pi = 3.14159265358979323846;

std::vector<float> sine(double frequency, std::size_t length) {
  std::vector<float> samples(length);
  for (std::size_t index = 0; index < length; ++index) {
    samples[index] = static_cast<float>(
        std::sin(2.0 * k_pi * frequency * static_cast<double>(index) / k_sample_rate));
  }
  return samples;
}

double frequency_of(const std::vector<float>& samples) {
  std::size_t crossings = 0;
  for (std::size_t index = 1; index < samples.size(); ++index) {
    if (samples[index - 1] < 0.0f && samples[index] >= 0.0f) ++crossings;
  }
  return static_cast<double>(crossings) * k_sample_rate / static_cast<double>(samples.size());
}

double interior_mean(const std::vector<float>& samples) {
  const auto begin = samples.size() / 4;
  const auto end = samples.size() - begin;
  double total = 0.0;
  for (auto index = begin; index < end; ++index) total += static_cast<double>(samples[index]);
  return total / static_cast<double>(end - begin);
}

bool all_finite(const std::vector<float>& samples) {
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) return false;
  }
  return true;
}

}  // namespace

// Detects: a zero shift that must return the input bytes unchanged instead of running the resampler.
TEST(PitchShift, ZeroSemitonesReturnsTheInputUnchanged) {
  const auto input = sine(220.0, 4410);
  PitchShiftSettings settings;
  settings.semitones = 0.0;
  EXPECT_EQ(pitch_shift(input, settings), input);
}

// Detects: an output that must hold the input length for every shift direction.
TEST(PitchShift, PreservesTheOutputLength) {
  const auto input = sine(220.0, 22051);
  for (const double semitones : {12.0, -12.0, 5.0, -5.0, 48.0, -48.0}) {
    PitchShiftSettings settings;
    settings.semitones = semitones;
    EXPECT_EQ(pitch_shift(input, settings).size(), input.size()) << semitones;
  }
}

// Detects: a shifter that must double the frequency at one octave up.
TEST(PitchShift, RaisesTheFrequencyByAnOctave) {
  const auto input = sine(220.0, 44100);
  PitchShiftSettings settings;
  settings.semitones = 12.0;
  const auto output = pitch_shift(input, settings);
  const auto frequency = frequency_of(output);
  EXPECT_NEAR(frequency, 440.0, 44.0);
}

// Detects: a shifter that must halve the frequency at one octave down.
TEST(PitchShift, LowersTheFrequencyByAnOctave) {
  const auto input = sine(220.0, 44100);
  PitchShiftSettings settings;
  settings.semitones = -12.0;
  const auto output = pitch_shift(input, settings);
  const auto frequency = frequency_of(output);
  EXPECT_NEAR(frequency, 110.0, 11.0);
}

// Detects: a normalised overlap-add that must keep a constant signal constant in the interior.
TEST(PitchShift, KeepsAConstantSignalConstantInTheInterior) {
  const std::vector<float> input(22050, 0.5f);
  PitchShiftSettings settings;
  settings.semitones = 7.0;
  const auto output = pitch_shift(input, settings);
  EXPECT_NEAR(interior_mean(output), 0.5, 0.02);
}

// Detects: extreme shifts that must stay finite instead of overflowing the resampler.
TEST(PitchShift, ProducesFiniteSamplesAtTheRangeLimits) {
  const auto input = sine(220.0, 44100);
  for (const double semitones : {48.0, -48.0}) {
    PitchShiftSettings settings;
    settings.semitones = semitones;
    EXPECT_TRUE(all_finite(pitch_shift(input, settings))) << semitones;
  }
}

// Detects: an input shorter than the window that must still be shifted, not returned as the unshifted input.
TEST(PitchShift, ShiftsAShortInputWithAShrunkWindow) {
  const auto input = sine(220.0, 100);
  PitchShiftSettings settings;
  settings.semitones = 12.0;
  const auto output = pitch_shift(input, settings);
  EXPECT_EQ(output.size(), input.size());
  EXPECT_NE(output, input);
}

// Detects: an input shorter than the minimum grain, which must be refused rather than returned unshifted.
TEST(PitchShift, RefusesInputShorterThanTheMinimumGrain) {
  const auto input = sine(220.0, 10);
  PitchShiftSettings settings;
  settings.semitones = 12.0;
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
}

// Detects: an empty input that must return empty output.
TEST(PitchShift, HandlesEmptyInput) {
  PitchShiftSettings settings;
  settings.semitones = 12.0;
  EXPECT_TRUE(pitch_shift({}, settings).empty());
}

// Detects: settings that must be refused instead of producing a degenerate grain or an overflow.
TEST(PitchShift, RejectsInvalidSettings) {
  const auto input = sine(220.0, 44100);
  PitchShiftSettings settings;
  settings.semitones = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
  settings.semitones = 49.0;
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
  settings.semitones = 12.0;
  settings.window = 15;
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
  settings.window = 1024;
  settings.hop = 0;
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
  settings.hop = 600;
  EXPECT_THROW(pitch_shift(input, settings), std::invalid_argument);
}
