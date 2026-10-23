#include "fo4/archive/assets.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

TEST(HkxSkeletonRealData, ReadsVanillaHumanSkeletonFromAnimationsArchive) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }

  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(packfile, skeleton), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(skeleton.bone_names.size(), 95u);
  EXPECT_EQ(skeleton.parent_indices.size(), skeleton.bone_names.size());
  EXPECT_EQ(skeleton.reference_pose.size(), skeleton.bone_names.size());
  ASSERT_FALSE(skeleton.bone_names.empty());
  EXPECT_EQ(skeleton.bone_names.front(), "Root");
  EXPECT_EQ(skeleton.parent_indices.front(), -1);
  for (std::size_t i = 0; i < skeleton.parent_indices.size(); ++i) {
    const std::int16_t parent = skeleton.parent_indices[i];
    EXPECT_TRUE(parent == -1 || (parent >= 0 && static_cast<std::size_t>(parent) < i)) << "bone " << i;
  }
}
