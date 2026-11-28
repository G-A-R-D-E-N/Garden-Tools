#include "fo4/c_api_collision_names.h"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

TEST(CapiCollisionNamesReal, NamesLayersAndMaterialsFromTheBaseGame) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto master = (std::filesystem::path{dir} / "Fallout4.esm").string();
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;

  const char* paths[] = {master.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_collision_names* names = nullptr;
  ASSERT_EQ(radicle_collision_names_build(env, &names), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_env_destroy(env);

  size_t layers = 0;
  size_t materials = 0;
  ASSERT_EQ(radicle_collision_names_counts(names, &layers, &materials), RADICLE_STATUS_OK);
  std::cout << "[collision names] layers=" << layers << " materials=" << materials << "\n";
  EXPECT_GT(layers, 0u);
  EXPECT_GT(materials, 0u);

  size_t named = 0;
  for (std::uint32_t id = 0; id < layers; ++id) {
    const char* name = nullptr;
    ASSERT_EQ(radicle_collision_names_layer(names, id, &name), RADICLE_STATUS_OK);
    if (name != nullptr) {
      EXPECT_NE(std::string{name}.size(), 0u);
      ++named;
    }
  }
  EXPECT_GT(named, 0u);
  radicle_collision_names_destroy(names);
}

// Detects: the collides-with-nothing flag drifting from the COLL CollidesWith data on the base game: only L_NAVCUT (49) has an empty list nobody lists, so the flagged set must be exactly {49}; it must also stay inside the 21 layers the engine freezes as collides-with-nothing (HavokLayers.VanillaCollidesWithNothing, used here only as an oracle), which a read of the lists wrongly widened or a parse failure that flags every layer would break.
TEST(CapiCollisionNamesReal, FlagsOnlyL_NavcutAsCollidingWithNothingInTheBaseGame) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto master = (std::filesystem::path{dir} / "Fallout4.esm").string();
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;

  const char* paths[] = {master.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_collision_names* names = nullptr;
  ASSERT_EQ(radicle_collision_names_build(env, &names), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_env_destroy(env);

  size_t layers = 0;
  size_t materials = 0;
  ASSERT_EQ(radicle_collision_names_counts(names, &layers, &materials), RADICLE_STATUS_OK);
  constexpr std::array<std::uint32_t, 21> engine_list{12, 16, 18, 19, 21, 22, 23, 24, 25, 34, 36,
                                                      37, 42, 44, 45, 47, 48, 49, 51, 52, 54};
  std::vector<std::uint32_t> flagged;
  for (std::uint32_t id = 0; id < layers; ++id) {
    std::int32_t flag = -1;
    ASSERT_EQ(radicle_collision_names_layer_collides_with_nothing(names, id, &flag), RADICLE_STATUS_OK);
    if (flag == 1) flagged.push_back(id);
  }
  radicle_collision_names_destroy(names);
  std::cout << "[collision names] flagged layers=" << flagged.size() << '\n';
  EXPECT_EQ(flagged, (std::vector<std::uint32_t>{49}));
  for (const auto id : flagged) EXPECT_NE(std::find(engine_list.begin(), engine_list.end(), id), engine_list.end()) << id;
}

// Detects (invariant): the per-layer CollidesWith read drifting from the base game's COLL data: a read scan of Fallout4.esm shows the lists are symmetric (A lists B exactly when B lists A), L_NAVCUT (49) is empty, L_ACOUSTIC_SPACE (21) lists only L_CAMERASPHERE (36) and L_DEADACTORZONE (47) only L_DEADBIP (32); a wrong index mapping or a dropped entry breaks the symmetry or those rows.
TEST(CapiCollisionNamesReal, ReturnsSymmetricCollidesWithListsInTheBaseGame) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto master = (std::filesystem::path{dir} / "Fallout4.esm").string();
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;

  const char* paths[] = {master.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_collision_names* names = nullptr;
  ASSERT_EQ(radicle_collision_names_build(env, &names), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_env_destroy(env);

  size_t layers = 0;
  size_t materials = 0;
  ASSERT_EQ(radicle_collision_names_counts(names, &layers, &materials), RADICLE_STATUS_OK);
  std::vector<std::set<std::uint32_t>> lists(layers);
  for (std::uint32_t id = 0; id < layers; ++id) {
    const std::uint32_t* values = nullptr;
    size_t count = 0;
    ASSERT_EQ(radicle_collision_names_layer_collides_with(names, id, &values, &count), RADICLE_STATUS_OK);
    lists[id].insert(values, values + count);
    EXPECT_EQ(lists[id].size(), count) << "layer " << id << " list is not unique";
  }
  radicle_collision_names_destroy(names);
  size_t asymmetric = 0;
  for (std::uint32_t id = 0; id < layers; ++id)
    for (const auto other : lists[id])
      if (other >= layers || !lists[other].contains(id)) ++asymmetric;
  EXPECT_EQ(asymmetric, 0u);
  ASSERT_GT(layers, 54u);
  EXPECT_TRUE(lists[49].empty());
  EXPECT_EQ(lists[21], (std::set<std::uint32_t>{36}));
  EXPECT_EQ(lists[47], (std::set<std::uint32_t>{32}));
}
