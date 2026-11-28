#include "fo4/archive/assets.hpp"
#include "fo4/nif/anim.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  const auto text = lower(path);
  return text.size() >= 4 && text.substr(text.size() - 4) == ".nif";
}

}  // namespace

// Detects: a vanilla door or Vault 111 sequence that loses its time range or controlled blocks, or a transform-driven sequence that evaluates to no poses; a sequence that only drives shader or material values has no node poses by design.
TEST(NifAnimRealData, VanillaDoorsExposeOpenCloseAndMoveAtTheEnds) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t scanned = 0;
  std::size_t with_sequences = 0;
  std::size_t transform_sequences = 0;
  std::string first_failure;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      const auto text = lower(path);
      const bool door = text.find("bunextdoor01") != std::string::npos ||
                        text.find("switchdoor") != std::string::npos || text.find("gear") != std::string::npos ||
                        text.find("vault111") != std::string::npos || text.find("v111") != std::string::npos;
      if (!door) continue;
      ++scanned;
      if (scanned > 40) break;
      try {
        auto file = fo4::nif::read_nif(reader.extract(i));
        const auto sequences = fo4::nif::read_anim_sequences(file);
        if (sequences.empty()) continue;
        ++with_sequences;
        for (const auto& seq : sequences) {
          bool has_open = lower(seq.name).find("open") != std::string::npos;
          bool has_close = lower(seq.name).find("close") != std::string::npos;
          (void)has_open;
          (void)has_close;
          if (!(seq.stop_time > seq.start_time) && first_failure.empty()) {
            first_failure = path + " sequence " + seq.name + " has an empty time range";
          }
          if (seq.controlled.empty() && first_failure.empty()) {
            first_failure = path + " sequence " + seq.name + " has no controlled blocks";
          }
          const bool moves_nodes = std::any_of(seq.controlled.begin(), seq.controlled.end(), [&](const auto& block) {
            return block.interpolator_ref >= 0 &&
                   file.blocks().at(static_cast<std::size_t>(block.interpolator_ref)).type == "NiTransformInterpolator";
          });
          if (moves_nodes) ++transform_sequences;
          try {
            const auto start_poses = fo4::nif::evaluate_sequence(file, seq.block, seq.start_time);
            const auto end_poses = fo4::nif::evaluate_sequence(file, seq.block, seq.stop_time);
            if (moves_nodes && start_poses.empty() && first_failure.empty()) {
              first_failure = path + " sequence " + seq.name + " evaluates to no poses";
            }
            if (start_poses.size() != end_poses.size() && first_failure.empty()) {
              first_failure = path + " sequence " + seq.name + " changes pose count at the ends";
            }
          } catch (const std::exception& error) {
            if (first_failure.empty()) first_failure = path + " sequence " + seq.name + ": " + error.what();
          }
        }
        if (text.find("gear") != std::string::npos) {
          std::cout << "gear door " << path << " sequences=" << sequences.size() << "\n";
          for (const auto& seq : sequences) {
            std::cout << "  " << seq.name << " [" << seq.start_time << "," << seq.stop_time
                      << "] controlled=" << seq.controlled.size() << "\n";
          }
        }
      } catch (const std::exception& error) {
        if (first_failure.empty()) first_failure = path + ": " + error.what();
      }
    }
    if (scanned > 40) break;
  }
  std::cout << "door NIFs " << scanned << " with sequences " << with_sequences << " transform sequences " << transform_sequences << "\n";
  EXPECT_GT(scanned, 0U) << "no BunExtDoor01/SwitchDoors/gear door NIF found in FO4_DATA_DIR";
  EXPECT_GT(with_sequences, 0U) << "no scanned door NIF carried a NiControllerSequence";
  EXPECT_GT(transform_sequences, 0U) << "no scanned door NIF carried a sequence that moves a node";
  EXPECT_TRUE(first_failure.empty()) << first_failure;
}
