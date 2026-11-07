#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/pose.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxAppend;
using fo4::hkx::HkxInterleavedAnimation;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxQsTransform;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxStatus;
using fo4::hkx::model_space_pose;
using fo4::hkx::sample_pose;
using fo4::test::hkx::build_packfile;
using fo4::test::hkx::TestSection;

constexpr float kPi = 3.14159265f;
constexpr std::array<float, 4> kIdentity{0.0f, 0.0f, 0.0f, 1.0f};

std::array<float, 4> turn_about_z(float radians) {
  return {0.0f, 0.0f, std::sin(0.5f * radians), std::cos(0.5f * radians)};
}

HkxQsTransform make_transform(const std::array<float, 3>& translation, const std::array<float, 4>& rotation) {
  HkxQsTransform out{};
  out.translation = translation;
  out.rotation = rotation;
  out.scale = {1.0f, 1.0f, 1.0f};
  return out;
}

HkxSkeleton chain_skeleton() {
  HkxSkeleton skeleton;
  skeleton.name = "Chain";
  skeleton.bone_names = {"Root", "Mid", "Tip"};
  skeleton.parent_indices = {-1, 0, 1};
  skeleton.reference_pose = {make_transform({0.0f, 0.0f, 0.0f}, kIdentity),
                             make_transform({0.0f, 0.0f, 0.0f}, kIdentity),
                             make_transform({0.0f, 0.0f, 0.0f}, kIdentity)};
  return skeleton;
}

std::vector<std::byte> empty_source() {
  const std::vector<std::byte> names;
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
}

HkxAnimation single_track_clip(float duration, const std::vector<std::array<float, 3>>& translations) {
  HkxAnimation clip;
  clip.kind = HkxAnimationKind::Interleaved;
  clip.duration = duration;
  clip.frame_count = translations.size();
  clip.tracks.resize(1);
  clip.tracks[0].translations = translations;
  clip.tracks[0].rotations.assign(translations.size(), kIdentity);
  clip.tracks[0].scales.assign(translations.size(), std::array<float, 3>{1.0f, 1.0f, 1.0f});
  return clip;
}

template <std::size_t N>
void expect_vector_near(const std::array<float, N>& actual, const std::array<float, N>& expected, float tolerance) {
  for (std::size_t c = 0; c < N; ++c) {
    EXPECT_NEAR(actual[c], expected[c], tolerance) << "component " << c;
  }
}

}  // namespace

TEST(HkxPose, SamplesAnInterleavedClipAtFramesAndBetweenThem) {
  HkxInterleavedAnimation animation;
  animation.duration = 2.0f;
  animation.frame_count = 3;
  animation.track_count = 2;
  for (std::size_t frame = 0; frame < 3; ++frame) {
    for (std::size_t track = 0; track < 2; ++track) {
      animation.translations.push_back({static_cast<float>(frame), static_cast<float>(track), 2.0f});
      animation.rotations.push_back(frame == 1 && track == 0 ? turn_about_z(kPi / 2.0f) : turn_about_z(0.0f));
      animation.scales.push_back({1.0f, 1.0f, 1.0f});
    }
  }
  const auto source = empty_source();
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(fo4::hkx::read_packfile(appended.bytes, written), HkxStatus::Ok);
  HkxAnimation clip;
  ASSERT_EQ(fo4::hkx::read_animation(written, clip), HkxStatus::Ok);
  ASSERT_EQ(clip.kind, HkxAnimationKind::Interleaved);

  HkxSkeleton skeleton = chain_skeleton();
  skeleton.reference_pose[1] = make_transform({9.0f, 9.0f, 9.0f}, kIdentity);
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {2, 0};

  std::vector<HkxQsTransform> pose;
  ASSERT_EQ(sample_pose(skeleton, clip, binding, 1.0f, pose), HkxStatus::Ok);
  ASSERT_EQ(pose.size(), 3u);
  expect_vector_near(pose[2].translation, {1.0f, 0.0f, 2.0f}, 1e-5f);
  expect_vector_near(pose[2].rotation, turn_about_z(kPi / 2.0f), 1e-5f);
  expect_vector_near(pose[0].translation, {1.0f, 1.0f, 2.0f}, 1e-5f);
  expect_vector_near(pose[1].translation, {9.0f, 9.0f, 9.0f}, 1e-5f);

  ASSERT_EQ(sample_pose(skeleton, clip, binding, 0.5f, pose), HkxStatus::Ok);
  expect_vector_near(pose[2].translation, {0.5f, 0.0f, 2.0f}, 1e-5f);
  expect_vector_near(pose[2].rotation, turn_about_z(kPi / 4.0f), 1e-5f);

  ASSERT_EQ(sample_pose(skeleton, clip, binding, 1.5f, pose), HkxStatus::Ok);
  expect_vector_near(pose[2].translation, {1.5f, 0.0f, 2.0f}, 1e-5f);
  expect_vector_near(pose[2].rotation, turn_about_z(kPi / 4.0f), 1e-5f);

  ASSERT_EQ(sample_pose(skeleton, clip, binding, 5.0f, pose), HkxStatus::Ok);
  expect_vector_near(pose[2].translation, {2.0f, 0.0f, 2.0f}, 1e-5f);
  ASSERT_EQ(sample_pose(skeleton, clip, binding, -1.0f, pose), HkxStatus::Ok);
  expect_vector_near(pose[2].translation, {0.0f, 0.0f, 2.0f}, 1e-5f);
}

TEST(HkxPose, SplineClipSamplesMatchItsDecodedKeysAndLerpBetweenThem) {
  HkxInterleavedAnimation animation;
  animation.duration = 2.0f;
  animation.frame_count = 5;
  animation.track_count = 2;
  for (std::size_t frame = 0; frame < 5; ++frame) {
    for (std::size_t track = 0; track < 2; ++track) {
      const auto f = static_cast<float>(frame);
      animation.translations.push_back({f, 0.5f * static_cast<float>(track), 2.0f});
      animation.rotations.push_back(turn_about_z(0.1f * f));
      animation.scales.push_back({1.0f + 0.1f * f, 1.0f, 1.0f});
    }
  }
  const auto source = empty_source();
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(fo4::hkx::read_packfile(appended.bytes, written), HkxStatus::Ok);
  HkxAnimation clip;
  ASSERT_EQ(fo4::hkx::read_animation(written, clip), HkxStatus::Ok);
  ASSERT_EQ(clip.kind, HkxAnimationKind::Spline);
  ASSERT_EQ(clip.frame_count, 5u);

  HkxSkeleton skeleton = chain_skeleton();
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {1, 2};

  for (std::size_t frame = 0; frame < 5; ++frame) {
    std::vector<HkxQsTransform> pose;
    ASSERT_EQ(sample_pose(skeleton, clip, binding, 0.5f * static_cast<float>(frame), pose), HkxStatus::Ok);
    expect_vector_near(pose[1].translation, clip.tracks[0].translations[frame], 1e-5f);
    expect_vector_near(pose[1].rotation, clip.tracks[0].rotations[frame], 1e-5f);
    expect_vector_near(pose[2].scale, clip.tracks[1].scales[frame], 1e-5f);
  }

  std::vector<HkxQsTransform> mid;
  ASSERT_EQ(sample_pose(skeleton, clip, binding, 0.75f, mid), HkxStatus::Ok);
  const auto& first = clip.tracks[0].translations[1];
  const auto& second = clip.tracks[0].translations[2];
  expect_vector_near(mid[1].translation,
                     {(first[0] + second[0]) / 2.0f, (first[1] + second[1]) / 2.0f, (first[2] + second[2]) / 2.0f},
                     1e-5f);
  const auto& rotation = mid[1].rotation;
  EXPECT_NEAR(rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] +
                  rotation[3] * rotation[3],
              1.0f, 1e-5f);
}

TEST(HkxPose, ModelSpaceComposesEachBoneOntoItsParentInTheChain) {
  const HkxSkeleton skeleton = chain_skeleton();
  const auto quarter = turn_about_z(kPi / 2.0f);
  const std::vector<HkxQsTransform> local{
      make_transform({1.0f, 0.0f, 0.0f}, quarter),
      make_transform({1.0f, 0.0f, 0.0f}, kIdentity),
      make_transform({1.0f, 0.0f, 0.0f}, quarter),
  };
  std::vector<HkxQsTransform> model;
  ASSERT_EQ(model_space_pose(skeleton, local, model), HkxStatus::Ok);
  ASSERT_EQ(model.size(), 3u);
  expect_vector_near(model[0].translation, {1.0f, 0.0f, 0.0f}, 1e-5f);
  expect_vector_near(model[0].rotation, quarter, 1e-5f);
  expect_vector_near(model[1].translation, {1.0f, 1.0f, 0.0f}, 1e-5f);
  expect_vector_near(model[1].rotation, quarter, 1e-5f);
  expect_vector_near(model[2].translation, {1.0f, 2.0f, 0.0f}, 1e-5f);
  expect_vector_near(model[2].rotation, turn_about_z(kPi), 1e-5f);
}

TEST(HkxPose, RefusesAParentThatIsNotAnEarlierBone) {
  HkxSkeleton skeleton = chain_skeleton();
  skeleton.parent_indices = {-1, 2, 1};
  const std::vector<HkxQsTransform> local(3, make_transform({0.0f, 0.0f, 0.0f}, kIdentity));
  std::vector<HkxQsTransform> model;
  EXPECT_EQ(model_space_pose(skeleton, local, model), HkxStatus::BadSection);
  EXPECT_TRUE(model.empty());
}

TEST(HkxPose, RefusesABindingThatMapsATrackOutsideTheSkeleton) {
  const HkxSkeleton skeleton = chain_skeleton();
  HkxAnimation clip;
  clip.kind = HkxAnimationKind::Interleaved;
  clip.duration = 1.0f;
  clip.frame_count = 1;
  clip.tracks.resize(1);
  clip.tracks[0].translations = {{0.0f, 0.0f, 0.0f}};
  clip.tracks[0].rotations = {kIdentity};
  clip.tracks[0].scales = {{1.0f, 1.0f, 1.0f}};
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {7};
  std::vector<HkxQsTransform> pose;
  EXPECT_EQ(sample_pose(skeleton, clip, binding, 0.0f, pose), HkxStatus::BadSection);
  EXPECT_TRUE(pose.empty());
}

TEST(HkxPose, ReturnsUnsupportedLayoutForAViewClip) {
  const HkxSkeleton skeleton = chain_skeleton();
  HkxAnimation clip;
  clip.kind = HkxAnimationKind::View;
  clip.duration = 1.0f;
  clip.frame_count = 1;
  const HkxAnimationBinding binding;
  std::vector<HkxQsTransform> pose;
  EXPECT_EQ(sample_pose(skeleton, clip, binding, 0.0f, pose), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(pose.empty());
}

TEST(HkxPose, RefusesANonFiniteSampleTime) {
  const HkxSkeleton skeleton = chain_skeleton();
  const HkxAnimation clip = single_track_clip(1.0f, {{0.0f, 0.0f, 0.0f}});
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {2};
  for (float time : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    std::vector<HkxQsTransform> pose;
    EXPECT_EQ(sample_pose(skeleton, clip, binding, time, pose), HkxStatus::BadSection);
    EXPECT_TRUE(pose.empty());
  }
}

TEST(HkxPose, SamplesASingleFrameClipAtItsOnlyPoseForAnyTime) {
  const HkxSkeleton skeleton = chain_skeleton();
  const HkxAnimation clip = single_track_clip(1.0f, {{4.0f, 5.0f, 6.0f}});
  ASSERT_EQ(clip.frame_count, 1u);
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {2};
  for (float time : {0.0f, 0.75f}) {
    std::vector<HkxQsTransform> pose;
    ASSERT_EQ(sample_pose(skeleton, clip, binding, time, pose), HkxStatus::Ok);
    ASSERT_EQ(pose.size(), 3u);
    expect_vector_near(pose[2].translation, {4.0f, 5.0f, 6.0f}, 1e-5f);
    expect_vector_near(pose[0].translation, {0.0f, 0.0f, 0.0f}, 1e-5f);
  }
}

TEST(HkxPose, HoldsTheFirstSampleWhenTheClipHasZeroDuration) {
  const HkxSkeleton skeleton = chain_skeleton();
  const HkxAnimation clip = single_track_clip(0.0f, {{1.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}});
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {2};
  std::vector<HkxQsTransform> pose;
  ASSERT_EQ(sample_pose(skeleton, clip, binding, 0.5f, pose), HkxStatus::Ok);
  ASSERT_EQ(pose.size(), 3u);
  expect_vector_near(pose[2].translation, {1.0f, 0.0f, 0.0f}, 1e-5f);
}

TEST(HkxPose, ReturnsTheReferencePoseForAClipWithNoTracks) {
  HkxSkeleton skeleton = chain_skeleton();
  skeleton.reference_pose[1] = make_transform({9.0f, 9.0f, 9.0f}, kIdentity);
  HkxAnimation clip;
  clip.kind = HkxAnimationKind::Interleaved;
  clip.duration = 2.0f;
  clip.frame_count = 3;
  const HkxAnimationBinding binding;
  std::vector<HkxQsTransform> pose;
  ASSERT_EQ(sample_pose(skeleton, clip, binding, 1.0f, pose), HkxStatus::Ok);
  ASSERT_EQ(pose.size(), 3u);
  expect_vector_near(pose[1].translation, {9.0f, 9.0f, 9.0f}, 1e-5f);
  expect_vector_near(pose[2].translation, {0.0f, 0.0f, 0.0f}, 1e-5f);
}

TEST(HkxPose, ModelSpaceRefusesALocalSpanThatIsNotOneTransformPerBone) {
  const HkxSkeleton skeleton = chain_skeleton();
  std::vector<HkxQsTransform> model;
  for (std::size_t count : {std::size_t{2}, std::size_t{4}}) {
    const std::vector<HkxQsTransform> local(count, make_transform({0.0f, 0.0f, 0.0f}, kIdentity));
    EXPECT_EQ(model_space_pose(skeleton, local, model), HkxStatus::BadSection) << count << " transforms for 3 bones";
    EXPECT_TRUE(model.empty());
  }
}
