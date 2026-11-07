#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/pose.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool finite_rotation(const std::array<float, 4>& rotation) {
  const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] +
                                 rotation[3] * rotation[3]);
  return std::isfinite(length) && std::fabs(length - 1.0f) < 1e-3f;
}

bool finite_transform(const fo4::hkx::HkxQsTransform& transform) {
  for (float value : transform.translation) {
    if (!std::isfinite(value)) return false;
  }
  for (float value : transform.scale) {
    if (!std::isfinite(value)) return false;
  }
  return finite_rotation(transform.rotation);
}

}  // namespace

TEST(HkxPoseRealData, TimeZeroTPosePoseIsFiniteWithUnitRotations) {
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
  const auto skeleton_location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  ASSERT_TRUE(skeleton_location.has_value());
  const auto skeleton_bytes = assets.read(*skeleton_location);
  fo4::hkx::HkxPackfile skeleton_file;
  ASSERT_EQ(fo4::hkx::read_packfile(skeleton_bytes, skeleton_file), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(skeleton_file, skeleton), fo4::hkx::HkxStatus::Ok);

  const auto clip_location = assets.locate("meshes/actors/character/animations/furniture/tpose/posea_idle1.hkx");
  ASSERT_TRUE(clip_location.has_value());
  const auto clip_bytes = assets.read(*clip_location);
  fo4::hkx::HkxPackfile clip_file;
  ASSERT_EQ(fo4::hkx::read_packfile(clip_bytes, clip_file), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(clip_file, animation), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimationBinding binding;
  ASSERT_EQ(fo4::hkx::read_binding(clip_file, binding), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(animation.tracks.size(), 94u);
  EXPECT_EQ(binding.transform_track_to_bone_indices.size(), animation.tracks.size());

  std::vector<fo4::hkx::HkxQsTransform> pose;
  ASSERT_EQ(fo4::hkx::sample_pose(skeleton, animation, binding, 0.0f, pose), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(pose.size(), skeleton.bone_names.size());
  for (std::size_t bone = 0; bone < pose.size(); ++bone) {
    EXPECT_TRUE(finite_transform(pose[bone])) << "bone " << bone;
  }

  std::vector<fo4::hkx::HkxQsTransform> model;
  ASSERT_EQ(fo4::hkx::model_space_pose(skeleton, pose, model), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(model.size(), skeleton.bone_names.size());
  for (std::size_t bone = 0; bone < model.size(); ++bone) {
    EXPECT_TRUE(finite_transform(model[bone])) << "bone " << bone;
  }
}
