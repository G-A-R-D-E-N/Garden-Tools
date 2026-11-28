#include "fo4/environment/base_records.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/formid.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

TEST(LodStatSlotsReal, CountsStatDistantLodSlotsInFallout4Master) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);

  constexpr std::uint32_t k_has_distant_lod = 0x8000u;
  const auto stats = env.winners_of(fo4::Signature::from_string("STAT"));
  std::array<std::size_t, fo4::env::k_distant_lod_levels> filled{};
  std::size_t with_any_slot = 0;
  std::size_t any_slot_without_flag = 0;
  for (const auto* ref : stats) {
    const auto lod = fo4::env::decode_static_distant_lod(env, *ref);
    bool any = false;
    for (std::size_t slot = 0; slot < lod.meshes.size(); ++slot) {
      if (!lod.meshes[slot]) continue;
      ++filled[slot];
      any = true;
    }
    if (any) ++with_any_slot;
    if (any && (ref->record->header.flags.value() & k_has_distant_lod) == 0) {
      ++any_slot_without_flag;
    }
  }
  std::cout << "STAT winners " << stats.size() << ", any slot " << with_any_slot << ", slot 0 "
            << filled[0] << ", slot 1 " << filled[1] << ", slot 2 " << filled[2] << ", slot 3 "
            << filled[3] << '\n';
  EXPECT_GT(stats.size(), 1000u);
  EXPECT_GT(with_any_slot, 0u);
  EXPECT_EQ(filled[2], 456u);
  EXPECT_EQ(filled[3], 51u);
  EXPECT_EQ(any_slot_without_flag, 0u);
  for (const char* text : {"SCOL", "TREE", "ACTI"}) {
    const auto winners = env.winners_of(fo4::Signature::from_string(text));
    EXPECT_FALSE(winners.empty()) << text;
    std::size_t flagged = 0;
    for (const auto* ref : winners) {
      if (ref->record->header.flags.value() & k_has_distant_lod) ++flagged;
    }
    EXPECT_EQ(flagged, 0u) << text;
  }
}

// Detects (invariant): XILW offsets must stay interior-only and no placed tree may reach vanilla object LOD.
TEST(LodExteriorOffsetsReal, XilwIsInteriorOnlyAndNoPlacedTreeReachesObjectLod) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);

  constexpr std::uint16_t k_cell_interior = 1u;
  std::size_t interior_with_lod = 0;
  std::size_t exterior_with_lod = 0;
  std::size_t other_world = 0;
  const fo4::FormKey commonwealth{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{0x3C}};
  for (const auto* cell : env.winners_of(fo4::Signature::from_string("CELL"))) {
    const auto data = fo4::env::decode_cell(env, *cell);
    if (!data.exterior_lod) continue;
    if (data.flags.value_or(0) & k_cell_interior) {
      ++interior_with_lod;
    } else {
      ++exterior_with_lod;
    }
    if (data.exterior_lod->worldspace != commonwealth) ++other_world;
  }
  EXPECT_EQ(interior_with_lod, 23u);
  EXPECT_EQ(exterior_with_lod, 0u);
  EXPECT_EQ(other_world, 0u);

  const auto trees = env.winners_of(fo4::Signature::from_string("TREE"));
  EXPECT_EQ(trees.size(), 1u);
  const auto* world = env.winner(commonwealth);
  ASSERT_NE(world, nullptr);
  const auto collection = fo4::lod::collect_object_lod_candidates(env, commonwealth, 8);
  EXPECT_GT(collection.counts.included, 0u);
  for (const auto& candidate : collection.candidates) {
    for (const auto* tree : trees) ASSERT_FALSE(candidate.base == tree->record->form_key);
  }
}
