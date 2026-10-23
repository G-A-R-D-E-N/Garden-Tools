#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cmath>
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

TEST(HkxAnimationRealData, ReadsAVanillaSplineClipFromTheAnimationsArchive) {
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
  const auto location = assets.locate("meshes/actors/radroach/animations/turnleft90.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_GT(animation.frame_count, 1u);
  EXPECT_GT(animation.duration, 0.0f);
  EXPECT_FALSE(animation.tracks.empty());
  for (const fo4::hkx::HkxAnimationTrack& track : animation.tracks) {
    ASSERT_EQ(track.translations.size(), animation.frame_count);
    ASSERT_EQ(track.rotations.size(), animation.frame_count);
    ASSERT_EQ(track.scales.size(), animation.frame_count);
    const auto& rotation = track.rotations.front();
    const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                   rotation[2] * rotation[2] + rotation[3] * rotation[3]);
    EXPECT_NEAR(length, 1.0f, 1e-4f);
  }
}
