#include "fo4/archive/assets.hpp"
#include "fo4/hkx/character_workspace.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

using namespace fo4::hkx;

TEST(HkxCharacterRealData, TheVanillaCharacterSkeletonReadsAndReportsWithoutRefusal) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  ASSERT_FALSE(archives.empty()) << "no Fallout 4 archives found under FO4_DATA_DIR";
  const auto assets_source = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets_source.locate("meshes/actors/character/characterassets/skeleton.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets_source.read(*location);
  CharacterAssets assets;
  assets.skeleton = bytes;
  const auto report = check_character(assets);
  EXPECT_EQ(report.refusal, "");
  EXPECT_GT(report.bones, 0u);
  std::cout << "character skeleton: " << report.bones << " bones, " << report.errors() << " errors, "
            << report.warnings() << " warnings, " << report.mirror.pairs << " mirror pairs\n";
}
