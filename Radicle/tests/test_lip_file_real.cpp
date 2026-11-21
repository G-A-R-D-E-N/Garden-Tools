#include "fo4/archive/ba2.hpp"
#include "fo4/audio/lip_file.hpp"
#include "fo4/audio/sound_container.hpp"
#include "fo4/audio/xwma_decoder.hpp"
#include "fo4/error.hpp"
#include "real_data.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_target_samples = 1200;
constexpr std::size_t k_gap_measurements = 40;
constexpr double k_max_gap_seconds = 3.0;
constexpr float k_max_plausible_overshoot = 1.5f;
constexpr double k_max_clamped_cell_fraction = 1e-4;

bool has_suffix(const std::string& text, const char* suffix) {
  const std::string tail{suffix};
  return text.size() >= tail.size() &&
         std::equal(tail.rbegin(), tail.rend(), text.rbegin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

std::uint32_t block_u32(std::span<const std::byte> block, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + i])) << (8 * i);
  return value;
}

bool is_intended_rejection(const std::string& message) {
  return message.find("lip block is empty") != std::string::npos || message.find("TOM3 lip files are not supported") != std::string::npos ||
         message.find("uncompressed lip files are not supported") != std::string::npos;
}

}  // namespace

TEST(LipFileRealData, EveryShippedCompressedVoiceLipDecodesToItsDeclaredGrid) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Voices.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Voices.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);

  std::vector<const fo4::archive::Ba2GeneralEntry*> fuz;
  for (const auto& entry : archive.entries) {
    if (has_suffix(entry.path, ".fuz")) fuz.push_back(&entry);
  }
  ASSERT_GT(fuz.size(), 0u);
  const bool whole_archive = std::getenv("FO4_LIP_FULL") != nullptr;
  const std::size_t step = whole_archive ? 1 : std::max<std::size_t>(1, fuz.size() / k_target_samples);

  std::map<std::string, std::size_t> rejected;
  std::map<std::uint32_t, std::size_t> decoded_by_flags;
  std::size_t sampled = 0;
  std::size_t decoded = 0;
  std::size_t clamped_files = 0;
  std::size_t clamped_cells = 0;
  float worst_overshoot = 0.0f;
  std::size_t cells = 0;
  std::size_t measured = 0;
  double worst_gap = 0.0;

  for (std::size_t index = 0; index < fuz.size(); index += step) {
    const auto& entry = *fuz[index];
    ++sampled;
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    fo4::audio::LipAnimation lip;
    try {
      lip = fo4::audio::parse_lip(bytes);
    } catch (const fo4::parse_error& error) {
      const std::string message = error.parse_message();
      ++rejected[message];
      EXPECT_TRUE(is_intended_rejection(message)) << entry.path << ": " << message;
      continue;
    }
    ++decoded;
    ++decoded_by_flags[lip.flags];
    const auto block = fo4::audio::fuz_lip_block(bytes);
    EXPECT_EQ(lip.weights.size(), static_cast<std::size_t>(lip.frame_count) * lip.slot_count()) << entry.path;
    EXPECT_EQ(block_u32(block, 4), lip.weights.size() * sizeof(float) + 28) << entry.path;
    EXPECT_EQ(lip.modifier_count, (lip.flags & fo4::audio::k_lip_flag_has_modifiers) != 0 ? 17u : 0u) << entry.path;
    cells += lip.weights.size();
    clamped_cells += lip.clamped_cells;
    if (lip.clamped_cells != 0) {
      ++clamped_files;
      worst_overshoot = std::max(worst_overshoot, lip.max_clamped_magnitude);
      std::cout << "clamped " << lip.clamped_cells << " cells (max magnitude " << lip.max_clamped_magnitude << ") in " << entry.path << '\n';
      EXPECT_LE(lip.max_clamped_magnitude, k_max_plausible_overshoot) << entry.path;
    }
    EXPECT_TRUE(std::any_of(lip.weights.begin(), lip.weights.end(), [](float weight) { return weight != 0.0f; })) << entry.path;

    if (measured < k_gap_measurements && decoded % 25 == 1) {
      const auto container = fo4::audio::parse_sound_container(bytes);
      const auto pcm = fo4::audio::decode_xwma(bytes);
      const double audio = static_cast<double>(pcm.samples.size()) / pcm.channels / pcm.sample_rate;
      const double lip_seconds = (static_cast<double>(lip.frame_count) + lip.preroll_frames) / fo4::audio::k_lip_frames_per_second;
      const double gap = lip_seconds - audio;
      std::cout << entry.path << " flags " << lip.flags << " frames " << lip.frame_count << " preroll " << lip.preroll_frames
                << " lip_s " << lip_seconds << " raw_s " << lip.frame_count / 30.0 << " audio_s " << audio << " gap " << gap << '\n';
      worst_gap = std::max(worst_gap, std::abs(gap));
      EXPECT_LT(std::abs(gap), k_max_gap_seconds) << entry.path;
      EXPECT_EQ(container.lip_size, block.size()) << entry.path;
      ++measured;
    }
  }

  std::cout << "archive fuz " << fuz.size() << " step " << step << " sampled " << sampled << " decoded " << decoded << " rejected "
            << sampled - decoded << " clamped files " << clamped_files << " clamped cells " << clamped_cells << " of " << cells << " worst overshoot " << worst_overshoot
            << " worst gap " << worst_gap << '\n';
  for (const auto& [flags, count] : decoded_by_flags) std::cout << "decoded flags " << flags << ": " << count << '\n';
  for (const auto& [reason, count] : rejected) std::cout << "rejected: " << reason << ": " << count << '\n';

  EXPECT_EQ(sampled, decoded + [&] {
    std::size_t total = 0;
    for (const auto& item : rejected) total += item.second;
    return total;
  }());
  EXPECT_GT(decoded, 0u);
  EXPECT_LE(static_cast<double>(clamped_cells), static_cast<double>(cells) * k_max_clamped_cell_fraction);
}
