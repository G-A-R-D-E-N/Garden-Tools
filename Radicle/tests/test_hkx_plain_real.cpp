#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/clip_roles.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/plain.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr const char* kRoot = "meshes/actors/character";

bool finite(const std::vector<std::array<float, 3>>& values) {
  for (const auto& value : values) {
    for (const float component : value) {
      if (!std::isfinite(component)) return false;
    }
  }
  return true;
}

}  // namespace

TEST(HkxPlainRealData, TheCharacterBehaviorNamesWalkAndRunAndThePlainClipSamples) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);

  const auto behavior_location = assets.locate("meshes/actors/character/behaviors/locomotion_8wayblend.hkx");
  ASSERT_TRUE(behavior_location.has_value());
  const auto behavior_bytes = assets.read(*behavior_location);
  fo4::hkx::HkxPackfile behavior_file;
  ASSERT_EQ(fo4::hkx::read_packfile(behavior_bytes, behavior_file), fo4::hkx::HkxStatus::Ok);
  std::vector<fo4::hkx::HkxBehaviorClip> clips;
  ASSERT_EQ(fo4::hkx::read_behavior_clips(behavior_file, clips), fo4::hkx::HkxStatus::Ok);
  ASSERT_FALSE(clips.empty());

  const auto roles = fo4::hkx::resolve_clip_roles(clips, kRoot, "animations/1hm");
  const fo4::hkx::HkxClipRole* walk = nullptr;
  const fo4::hkx::HkxClipRole* run = nullptr;
  for (const auto& role : roles) {
    EXPECT_TRUE(assets.locate(role.entry).has_value()) << role.entry;
    if (role.role == "walk") walk = &role;
    if (role.role == "run") run = &role;
  }
  ASSERT_NE(walk, nullptr);
  ASSERT_NE(run, nullptr);
  EXPECT_EQ(walk->entry, "meshes/actors/character/animations/1hm/walkforward.hkx");
  EXPECT_EQ(run->entry, "meshes/actors/character/animations/1hm/runforward.hkx");
  EXPECT_TRUE(walk->loop);
  EXPECT_TRUE(run->loop);

  const auto skeleton_location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  ASSERT_TRUE(skeleton_location.has_value());
  const auto skeleton_bytes = assets.read(*skeleton_location);
  fo4::hkx::HkxPackfile skeleton_file;
  ASSERT_EQ(fo4::hkx::read_packfile(skeleton_bytes, skeleton_file), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(skeleton_file, skeleton), fo4::hkx::HkxStatus::Ok);

  const auto walk_location = assets.locate(walk->entry);
  ASSERT_TRUE(walk_location.has_value());
  const auto walk_bytes = assets.read(*walk_location);
  fo4::hkx::HkxPackfile walk_file;
  ASSERT_EQ(fo4::hkx::read_packfile(walk_bytes, walk_file), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(walk_file, animation), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimationBinding binding;
  ASSERT_EQ(fo4::hkx::read_binding(walk_file, binding), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxExtractedMotion motion;
  ASSERT_EQ(fo4::hkx::read_extracted_motion(walk_file, motion), fo4::hkx::HkxStatus::Ok);

  fo4::hkx::HkxPlainClip native;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(skeleton, animation, binding, &motion, 0.0f, walk->loop, native),
            fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(native.key_count, animation.frame_count);
  EXPECT_EQ(native.tracks.size(), binding.transform_track_to_bone_indices.size());
  EXPECT_TRUE(native.loop);
  for (const auto& track : native.tracks) {
    EXPECT_EQ(track.translations.size(), native.key_count);
    EXPECT_TRUE(finite(track.translations));
  }
  ASSERT_EQ(native.root_motion.kind, fo4::hkx::HkxMotionKind::Default);
  EXPECT_EQ(native.root_motion.deltas.size(), native.key_count);
  EXPECT_GT(native.root_motion.mean_planar_speed, 0.0f);

  fo4::hkx::HkxPlainClip resampled;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(skeleton, animation, binding, &motion, 30.0f, walk->loop, resampled),
            fo4::hkx::HkxStatus::Ok);
  EXPECT_FLOAT_EQ(resampled.rate, 30.0f);
  EXPECT_EQ(resampled.key_count, static_cast<std::size_t>(std::llround(animation.duration * 30.0f)) + 1);
  EXPECT_EQ(resampled.root_motion.deltas.size(), resampled.key_count);
}
