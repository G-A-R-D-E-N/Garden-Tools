#include "fo4/archive/ba2.hpp"
#include "fo4/nif/tri_file.hpp"
#include "real_data.hpp"
#include "real_data_build.hpp"
#include "real_meshes_archive.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool has_suffix(const std::string& text, const char* suffix) {
  const std::string tail{suffix};
  return text.size() >= tail.size() &&
         std::equal(tail.rbegin(), tail.rend(), text.rbegin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

}  // namespace

TEST(TriFileRealData, EveryShippedTriParsesAndTheHeadsCarryTheJawMorph) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const auto* shared = fo4::test::shared_meshes_archive(dir);
  if (shared == nullptr) GTEST_SKIP() << "no Fallout4 - Meshes.ba2 in FO4_DATA_DIR";
  const auto& archive = *shared;

  std::size_t parsed = 0;
  std::size_t with_jaw = 0;
  std::size_t morph_total = 0;
  for (const auto& entry : archive.entries) {
    if (!has_suffix(entry.path, ".tri")) continue;
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    fo4::nif::TriFile tri;
    ASSERT_NO_THROW(tri = fo4::nif::parse_tri(bytes)) << entry.path;
    ++parsed;
    morph_total += tri.morphs.size();
    EXPECT_EQ(tri.positions.size(), static_cast<std::size_t>(tri.vertex_count) * 3) << entry.path;
    for (const auto& morph : tri.morphs) {
      EXPECT_EQ(morph.deltas.size(), static_cast<std::size_t>(tri.vertex_count) * 3) << entry.path << " " << morph.name;
      EXPECT_FALSE(morph.name.empty()) << entry.path;
      EXPECT_TRUE(std::isfinite(morph.scale)) << entry.path << " " << morph.name;
    }
    if (tri.find("JawOpen") != nullptr) ++with_jaw;
  }
  std::cout << "tri files parsed " << parsed << " with JawOpen " << with_jaw << " morphs " << morph_total << '\n';
  EXPECT_GT(parsed, 0u);
  EXPECT_GT(with_jaw, 0u);
}

TEST(TriFileRealData, JawOpenMovesTheLowerFaceDownwardInTheEngineFrame) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  const auto* shared = fo4::test::shared_meshes_archive(dir);
  if (shared == nullptr) GTEST_SKIP() << "no Fallout4 - Meshes.ba2 in FO4_DATA_DIR";
  const auto& archive = *shared;
  const auto* entry = fo4::archive::find_ba2_general_entry(archive, "Meshes\\Actors\\Character\\CharacterAssets\\BaseMaleHead.tri");
  ASSERT_NE(entry, nullptr);
  const auto tri = fo4::nif::parse_tri(fo4::archive::extract_ba2_general_entry(*entry));
  const auto* jaw = tri.find("JawOpen");
  ASSERT_NE(jaw, nullptr);
  const auto positions = fo4::nif::tri_to_engine_frame(tri.positions, 0.0f);
  const auto deltas = fo4::nif::tri_to_engine_frame(fo4::nif::tri_morph_deltas(*jaw), 0.0f);
  ASSERT_EQ(positions.size(), deltas.size());

  double moved_below = 0.0;
  double moved_above = 0.0;
  double lowest_y = 1e9;
  double highest_y = -1e9;
  for (std::size_t v = 0; v < positions.size() / 3; ++v) {
    lowest_y = std::min<double>(lowest_y, positions[v * 3 + 1]);
    highest_y = std::max<double>(highest_y, positions[v * 3 + 1]);
  }
  const double middle = (lowest_y + highest_y) * 0.5;
  for (std::size_t v = 0; v < positions.size() / 3; ++v) {
    const double dy = deltas[v * 3 + 1];
    (positions[v * 3 + 1] < middle ? moved_below : moved_above) += std::abs(dy);
  }
  std::cout << "BaseMaleHead height " << (highest_y - lowest_y) << " m, jaw motion below middle " << moved_below << " above " << moved_above << '\n';
  EXPECT_GT(highest_y - lowest_y, 0.15);
  EXPECT_LT(highest_y - lowest_y, 0.45);
  EXPECT_GT(moved_below, moved_above * 4.0);
}
