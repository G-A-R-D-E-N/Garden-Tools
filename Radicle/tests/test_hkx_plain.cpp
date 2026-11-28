#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/clip_roles.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/plain.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxBehaviorClip;
using fo4::hkx::HkxExtractedMotion;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxPlainClip;
using fo4::hkx::HkxQsTransform;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxStatus;
using fo4::hkx::k_plain_units_per_metre;
using fo4::test::hkx::clip_generator_packfile;

constexpr float kU = k_plain_units_per_metre;
constexpr float kHalfPi = 1.57079633f;
constexpr std::array<float, 4> kIdentity{0.0f, 0.0f, 0.0f, 1.0f};

HkxQsTransform make_transform(const std::array<float, 3>& translation, const std::array<float, 4>& rotation,
                              const std::array<float, 3>& scale = {1.0f, 1.0f, 1.0f}) {
  HkxQsTransform out{};
  out.translation = translation;
  out.rotation = rotation;
  out.scale = scale;
  return out;
}

HkxSkeleton two_bone_skeleton() {
  HkxSkeleton skeleton;
  skeleton.name = "Rig";
  skeleton.bone_names = {"Root", "Tip"};
  skeleton.parent_indices = {-1, 0};
  skeleton.reference_pose = {make_transform({0.0f, 0.0f, 0.0f}, kIdentity), make_transform({kU, 2.0f * kU, 3.0f * kU}, kIdentity)};
  return skeleton;
}

HkxAnimation tip_clip(float duration, const std::vector<std::array<float, 3>>& translations) {
  HkxAnimation clip;
  clip.kind = HkxAnimationKind::Interleaved;
  clip.duration = duration;
  clip.frame_count = translations.size();
  clip.tracks.resize(1);
  clip.tracks[0].translations = translations;
  clip.tracks[0].rotations.assign(translations.size(), kIdentity);
  clip.tracks[0].scales.assign(translations.size(), std::array<float, 3>{1.0f, 2.0f, 3.0f});
  return clip;
}

HkxAnimationBinding tip_binding() {
  HkxAnimationBinding binding;
  binding.transform_track_to_bone_indices = {1};
  return binding;
}

HkxExtractedMotion default_motion(float duration, const std::vector<std::array<float, 4>>& samples) {
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = duration;
  motion.samples = samples;
  return motion;
}

HkxBehaviorClip clip(const std::string& animation, std::int8_t mode = 1, float speed = 1.0f) {
  HkxBehaviorClip out;
  out.generator = "gen";
  out.animation = animation;
  out.mode = mode;
  out.playback_speed = speed;
  return out;
}

}  // namespace

TEST(HkxPlain, EngineAxesSwapYAndZNegateTranslationYAndScaleToMetres) {
  const auto engine = fo4::hkx::to_engine_axes(make_transform({kU, 2.0f * kU, 3.0f * kU}, {0.1f, 0.2f, 0.3f, 0.9f}, {1.0f, 2.0f, 3.0f}));
  EXPECT_NEAR(engine.translation[0], 1.0f, 1e-5f);
  EXPECT_NEAR(engine.translation[1], 3.0f, 1e-5f);
  EXPECT_NEAR(engine.translation[2], -2.0f, 1e-5f);
  EXPECT_EQ(engine.rotation, (std::array<float, 4>{0.1f, 0.3f, -0.2f, 0.9f}));
  EXPECT_EQ(engine.scale, (std::array<float, 3>{1.0f, 3.0f, 2.0f}));
}

TEST(HkxPlain, RestPoseConvertsEveryBoneInSkeletonOrder) {
  const auto rest = fo4::hkx::plain_rest_pose(two_bone_skeleton());
  ASSERT_EQ(rest.size(), 2u);
  EXPECT_NEAR(rest[1].translation[1], 3.0f, 1e-5f);
  EXPECT_NEAR(rest[1].translation[2], -2.0f, 1e-5f);
  EXPECT_EQ(rest[0].rotation, kIdentity);
}

TEST(HkxPlain, NativeRateKeysEveryFrameAndConvertsAxes) {
  const HkxAnimation animation = tip_clip(2.0f, {{0.0f, 0.0f, 0.0f}, {kU, 0.0f, 0.0f}, {2.0f * kU, kU, 0.0f}});
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), nullptr, 0.0f, true, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.key_count, 3u);
  EXPECT_FLOAT_EQ(out.rate, 1.0f);
  EXPECT_FLOAT_EQ(out.length, 2.0f);
  EXPECT_TRUE(out.loop);
  ASSERT_EQ(out.tracks.size(), 1u);
  EXPECT_EQ(out.tracks[0].bone, 1);
  ASSERT_EQ(out.tracks[0].translations.size(), 3u);
  ASSERT_EQ(out.tracks[0].rotations.size(), 3u);
  ASSERT_EQ(out.tracks[0].scales.size(), 3u);
  EXPECT_NEAR(out.tracks[0].translations[2][0], 2.0f, 1e-5f);
  EXPECT_NEAR(out.tracks[0].translations[2][2], -1.0f, 1e-5f);
  EXPECT_EQ(out.tracks[0].scales[1], (std::array<float, 3>{1.0f, 3.0f, 2.0f}));
  EXPECT_EQ(out.root_motion.kind, HkxMotionKind::None);
  EXPECT_TRUE(out.root_motion.deltas.empty());
}

TEST(HkxPlain, ExplicitRateResamplesTheClipWithInterpolation) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {kU, 0.0f, 0.0f}});
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), nullptr, 4.0f, false, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.key_count, 5u);
  EXPECT_FLOAT_EQ(out.rate, 4.0f);
  EXPECT_FALSE(out.loop);
  ASSERT_EQ(out.tracks[0].translations.size(), 5u);
  EXPECT_NEAR(out.tracks[0].translations[2][0], 0.5f, 1e-5f);
  EXPECT_NEAR(out.tracks[0].translations[4][0], 1.0f, 1e-5f);
}

TEST(HkxPlain, ARateOutsideZeroToTheCapIsRejected) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {kU, 0.0f, 0.0f}});
  HkxPlainClip out;
  for (const float rate : {-1.0f, 241.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    EXPECT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), nullptr, rate, false, out),
              HkxStatus::BadSection)
        << rate;
  }
}

TEST(HkxPlain, ASingleFrameClipYieldsOneKeyAtTheFileFrameRate) {
  HkxAnimation animation = tip_clip(0.0f, {{0.0f, 0.0f, 0.0f}});
  animation.frame_duration = 0.5f;
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), nullptr, 0.0f, false, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.key_count, 1u);
  EXPECT_FLOAT_EQ(out.rate, 2.0f);
}

TEST(HkxPlain, ATrackCountThatDoesNotMatchTheBindingIsRejected) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {kU, 0.0f, 0.0f}});
  HkxAnimationBinding binding;
  HkxPlainClip out;
  EXPECT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, binding, nullptr, 0.0f, false, out),
            HkxStatus::BadSection);
  EXPECT_EQ(out.key_count, 0u);
  EXPECT_TRUE(out.tracks.empty());
}

TEST(HkxPlain, EventsComeBackOrderedByTimeAcrossAnnotationTracks) {
  HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {kU, 0.0f, 0.0f}});
  fo4::hkx::HkxAnnotationTrack first;
  first.annotations = {{0.75f, "end"}, {0.25f, "start"}};
  fo4::hkx::HkxAnnotationTrack second;
  second.annotations = {{0.25f, "also"}};
  animation.annotation_tracks = {first, second};
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), nullptr, 0.0f, false, out),
            HkxStatus::Ok);
  ASSERT_EQ(out.events.size(), 3u);
  EXPECT_EQ(out.events[0].name, "start");
  EXPECT_EQ(out.events[1].name, "also");
  EXPECT_EQ(out.events[2].name, "end");
  EXPECT_FLOAT_EQ(out.events[2].time, 0.75f);
}

TEST(HkxPlain, RootMotionDeltasAreLocalToThePreviousHeading) {
  const HkxAnimation animation = tip_clip(2.0f, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
  const HkxExtractedMotion motion =
      default_motion(2.0f, {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, kU, 0.0f, kHalfPi}, {-kU, kU, 0.0f, kHalfPi}});
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, true, out),
            HkxStatus::Ok);
  ASSERT_EQ(out.root_motion.kind, HkxMotionKind::Default);
  ASSERT_EQ(out.root_motion.deltas.size(), 3u);
  EXPECT_EQ(out.root_motion.deltas[0], (std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));
  EXPECT_NEAR(out.root_motion.deltas[1][0], 0.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[1][1], 0.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[1][2], -1.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[1][3], kHalfPi, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[2][0], 0.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[2][2], -1.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.deltas[2][3], 0.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.net_translation[0], -1.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.net_translation[1], 0.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.net_translation[2], -1.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.net_yaw, kHalfPi, 1e-5f);
  EXPECT_NEAR(out.root_motion.mean_planar_speed, 1.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.mean_turn_rate, kHalfPi / 2.0f, 1e-5f);
}

TEST(HkxPlain, RootMotionVerticalTravelMapsToEngineY) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
  const HkxExtractedMotion motion = default_motion(1.0f, {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f * kU, 0.0f}});
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, false, out),
            HkxStatus::Ok);
  ASSERT_EQ(out.root_motion.deltas.size(), 2u);
  EXPECT_NEAR(out.root_motion.deltas[1][1], 2.0f, 1e-5f);
  EXPECT_NEAR(out.root_motion.mean_planar_speed, 0.0f, 1e-5f);
}

TEST(HkxPlain, RootMotionWithAnUpAxisThatIsNotZIsUnsupportedAndCarriesNoDeltas) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
  HkxExtractedMotion motion = default_motion(1.0f, {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, kU, 0.0f, 0.0f}});
  motion.up = {0.0f, 1.0f, 0.0f};
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, false, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.root_motion.kind, HkxMotionKind::Unsupported);
  EXPECT_TRUE(out.root_motion.deltas.empty());
}

TEST(HkxPlain, NonFiniteRootMotionIsUnsupported) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
  const HkxExtractedMotion motion =
      default_motion(1.0f, {{0.0f, 0.0f, 0.0f, 0.0f}, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f}});
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, false, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.root_motion.kind, HkxMotionKind::Unsupported);
  EXPECT_TRUE(out.root_motion.deltas.empty());
}

TEST(HkxPlain, NonFiniteRootMotionOnASingleKeyClipIsNeverReportedAsDefault) {
  constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInf = std::numeric_limits<float>::infinity();
  const std::vector<std::vector<std::array<float, 4>>> bad = {
      {{0.0f, 0.0f, 0.0f, 0.0f}, {kNaN, 0.0f, 0.0f, 0.0f}},
      {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, kInf}},
      {{kNaN, 0.0f, 0.0f, 0.0f}},
  };
  HkxAnimation animation = tip_clip(0.0f, {{0.0f, 0.0f, 0.0f}});
  animation.frame_duration = 0.5f;
  for (const auto& samples : bad) {
    const HkxExtractedMotion motion = default_motion(1.0f, samples);
    HkxPlainClip out;
    ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, false, out),
              HkxStatus::Ok);
    ASSERT_EQ(out.key_count, 1u);
    EXPECT_EQ(out.root_motion.kind, HkxMotionKind::Unsupported);
    EXPECT_TRUE(out.root_motion.deltas.empty());
  }
}

TEST(HkxPlain, AMotionOfKindNoneReportsNoRootMotion) {
  const HkxAnimation animation = tip_clip(1.0f, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
  HkxExtractedMotion motion;
  HkxPlainClip out;
  ASSERT_EQ(fo4::hkx::sample_plain_clip(two_bone_skeleton(), animation, tip_binding(), &motion, 0.0f, false, out),
            HkxStatus::Ok);
  EXPECT_EQ(out.root_motion.kind, HkxMotionKind::None);
}

TEST(HkxPlainRoles, NamesComeFromTheAnimationFileNameOnly) {
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\1HM\\WalkForward.hkt"), "walk");
  EXPECT_EQ(fo4::hkx::clip_role_of("animations/1hm/RunForward.hkx"), "run");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\Idle.hkt"), "idle");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\SneakIdle.hkt"), "sneak_idle");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\SprintForward.hkt"), "sprint");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\TurnLeft90.hkt"), "turn_left_90");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\TurnRight180.hkt"), "turn_right_180");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\TurnInPlaceLeft90.hkt"), "turn_left_90");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\RelaxedTurnInPlaceRight180_NonLooping.hkt"), "turn_right_180");
  EXPECT_EQ(fo4::hkx::clip_role_of("Animations\\Dance.hkt"), "");
  EXPECT_EQ(fo4::hkx::clip_role_of(""), "");
}

TEST(HkxPlainRoles, ResolveBuildsLowercaseArchiveEntriesAndLoopFlags) {
  const std::vector<HkxBehaviorClip> clips = {
      clip("Animations\\1HM\\WalkForward.hkt", 1, 1.0f),
      clip("Animations\\1HM\\RunForward.hkt", 0, 1.5f),
      clip("Animations\\1HM\\Unrelated.hkt"),
  };
  const auto roles = fo4::hkx::resolve_clip_roles(clips, "Meshes\\Actors\\Character\\", "");
  ASSERT_EQ(roles.size(), 2u);
  EXPECT_EQ(roles[0].role, "walk");
  EXPECT_EQ(roles[0].entry, "meshes/actors/character/animations/1hm/walkforward.hkx");
  EXPECT_TRUE(roles[0].loop);
  EXPECT_EQ(roles[1].role, "run");
  EXPECT_FALSE(roles[1].loop);
  EXPECT_FLOAT_EQ(roles[1].playback_speed, 1.5f);
}

TEST(HkxPlainRoles, ADirectoryFilterKeepsOnlyClipsBelowItAndTheFirstMatchWins) {
  const std::vector<HkxBehaviorClip> clips = {
      clip("Animations\\2HM\\WalkForward.hkt"),
      clip("Animations\\1HM\\WalkForward.hkt"),
      clip("Animations\\1HM\\WalkForward_NonLooping.hkt", 0),
      clip("Animations\\2HM\\Idle.hkt"),
  };
  const auto filtered = fo4::hkx::resolve_clip_roles(clips, "", "animations/1hm");
  ASSERT_EQ(filtered.size(), 1u);
  EXPECT_EQ(filtered[0].entry, "animations/1hm/walkforward.hkx");
  EXPECT_TRUE(filtered[0].loop);

  const auto unfiltered = fo4::hkx::resolve_clip_roles(clips, "", "");
  ASSERT_EQ(unfiltered.size(), 2u);
  EXPECT_EQ(unfiltered[0].role, "idle");
  EXPECT_EQ(unfiltered[1].entry, "animations/2hm/walkforward.hkx");
}

TEST(HkxPlainRoles, UnsafeOrForeignPathsNeverBecomeEntries) {
  const std::vector<HkxBehaviorClip> clips = {
      clip("..\\..\\Evil\\WalkForward.hkt"),
      clip("C:\\Evil\\RunForward.hkt"),
      clip("Animations\\Idle.nif"),
  };
  EXPECT_TRUE(fo4::hkx::resolve_clip_roles(clips, "meshes", "").empty());
  const std::vector<HkxBehaviorClip> safe = {clip("Animations\\WalkForward.hkt")};
  EXPECT_TRUE(fo4::hkx::resolve_clip_roles(safe, "..\\meshes", "").empty());
  EXPECT_TRUE(fo4::hkx::resolve_clip_roles(safe, "c:/meshes", "").empty());
  EXPECT_EQ(fo4::hkx::resolve_clip_roles(safe, "", "").size(), 1u);
}

TEST(HkxPlainRoles, ClipGeneratorsAreReadWithTheirNameModeAndSpeed) {
  const auto bytes = clip_generator_packfile({{"WalkGen", "Animations\\1HM\\WalkForward.hkt"}, {"RunGen", "Animations\\1HM\\RunForward.hkt"}},
                                             {1, 0}, 1.25f);
  HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok);
  std::vector<HkxBehaviorClip> clips;
  ASSERT_EQ(fo4::hkx::read_behavior_clips(packfile, clips), HkxStatus::Ok);
  ASSERT_EQ(clips.size(), 2u);
  EXPECT_EQ(clips[0].generator, "WalkGen");
  EXPECT_EQ(clips[0].animation, "Animations\\1HM\\WalkForward.hkt");
  EXPECT_EQ(clips[0].mode, 1);
  EXPECT_FLOAT_EQ(clips[0].playback_speed, 1.25f);
  EXPECT_EQ(clips[1].animation, "Animations\\1HM\\RunForward.hkt");
  EXPECT_EQ(clips[1].mode, 0);
  const auto roles = fo4::hkx::resolve_clip_roles(clips, "meshes", "");
  ASSERT_EQ(roles.size(), 2u);
  EXPECT_EQ(roles[0].entry, "meshes/animations/1hm/walkforward.hkx");
  EXPECT_TRUE(roles[0].loop);
  EXPECT_FALSE(roles[1].loop);
}

TEST(HkxPlainRoles, APackfileWithoutClipGeneratorsReportsMissingObject) {
  const auto bytes = clip_generator_packfile({}, {}, 1.0f);
  HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok);
  std::vector<HkxBehaviorClip> clips{clip("stale")};
  EXPECT_EQ(fo4::hkx::read_behavior_clips(packfile, clips), HkxStatus::MissingObject);
  EXPECT_TRUE(clips.empty());
}
