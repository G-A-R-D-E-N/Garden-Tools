#include "fo4/archive/ba2.hpp"
#include "fo4/audio/sound_container.hpp"
#include "fo4/audio/xwma_decoder.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

bool has_suffix(const std::string& text, const char* suffix) {
  const std::string tail{suffix};
  return text.size() >= tail.size() &&
         std::equal(tail.rbegin(), tail.rend(), text.rbegin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

bool names_silence(const std::string& text) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
  return lower.find("silence") != std::string::npos;
}

}  // namespace

TEST(XwmaDecoderRealData, ShippedXwmaDecodesCleanlyToPlausibleAudio) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Sounds.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Sounds.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  std::size_t decoded = 0;
  for (const auto& entry : archive.entries) {
    if (!has_suffix(entry.path, ".xwm")) {
      continue;
    }
    if (decoded >= 24) {
      break;
    }
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    const auto container = fo4::audio::parse_sound_container(bytes);
    SCOPED_TRACE(entry.path);
    const auto pcm = fo4::audio::decode_xwma(bytes);
    EXPECT_EQ(pcm.bad_packets, 0u);
    EXPECT_EQ(pcm.sample_rate, container.format.sample_rate);
    EXPECT_EQ(pcm.channels, container.format.channels);
    ASSERT_FALSE(pcm.samples.empty());
    double energy = 0.0;
    std::size_t clipped = 0;
    for (const auto sample : pcm.samples) {
      energy += static_cast<double>(sample) * sample;
      clipped += (sample == 32767 || sample == -32768) ? 1u : 0u;
    }
    const double rms = std::sqrt(energy / static_cast<double>(pcm.samples.size()));
    EXPECT_LT(clipped, pcm.samples.size() / 100 + 1);
    if (names_silence(entry.path)) {
      EXPECT_LT(rms, 1.0);
    } else {
      EXPECT_GT(rms, 1.0);
    }
    EXPECT_LT(rms, 20000.0);
    ++decoded;
  }
  EXPECT_GT(decoded, 0u);
}
