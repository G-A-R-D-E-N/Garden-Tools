#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_motion_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

using fo4::hkx::HkxExtractedMotion;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_extracted_motion;
using fo4::hkx::read_packfile;
using namespace fo4::test::hkx;

namespace {

HkxStatus read_motion(const std::vector<std::byte>& bytes, HkxExtractedMotion& motion) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  return read_extracted_motion(packfile, motion);
}

}  // namespace

TEST(HkxMotion, AClipWithNoExtractedMotionReadsAsKindNone) {
  HkxExtractedMotion motion;
  EXPECT_EQ(read_motion(motion_packfile(std::nullopt), motion), HkxStatus::Ok);
  EXPECT_EQ(motion.kind, HkxMotionKind::None);
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, ADefaultReferenceFrameDecodesItsAxesDurationAndSamples) {
  MotionSpec spec;
  spec.up = {0.0f, 0.0f, 1.0f};
  spec.forward = {0.6f, 0.8f, 0.0f};
  spec.duration = 2.0f;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f, 0.5f}, {4.0f, 5.0f, 6.0f, 1.0f}};
  HkxExtractedMotion motion;
  ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok);

  EXPECT_EQ(motion.kind, HkxMotionKind::Default);
  EXPECT_EQ(motion.class_name, "hkaDefaultAnimatedReferenceFrame");
  EXPECT_EQ(motion.up, spec.up);
  EXPECT_EQ(motion.forward, spec.forward);
  EXPECT_FLOAT_EQ(motion.duration, 2.0f);
  ASSERT_EQ(motion.samples.size(), 3u);
  EXPECT_EQ(motion.samples[1], (std::array<float, 4>{1.0f, 2.0f, 3.0f, 0.5f}));
  EXPECT_EQ(motion.samples[2], (std::array<float, 4>{4.0f, 5.0f, 6.0f, 1.0f}));
}

TEST(HkxMotion, AnotherReferenceFrameClassIsUnsupportedWithNoSamples) {
  MotionSpec spec;
  spec.frame_class = "hkaAnimatedReferenceFrame";
  spec.samples = {{1.0f, 2.0f, 3.0f, 4.0f}};
  HkxExtractedMotion motion;
  ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok);

  EXPECT_EQ(motion.kind, HkxMotionKind::Unsupported);
  EXPECT_EQ(motion.class_name, "hkaAnimatedReferenceFrame");
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, AParametricFrameTypeIsUnsupportedWithNoSamples) {
  MotionSpec spec;
  spec.frame_type = 2;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.25f}};
  HkxExtractedMotion motion;
  ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok);

  EXPECT_EQ(motion.kind, HkxMotionKind::Unsupported);
  EXPECT_EQ(motion.class_name, "hkaDefaultAnimatedReferenceFrame");
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, AFrameTypeOneIsTheSameDefaultPayloadAsZero) {
  MotionSpec spec;
  spec.frame_type = 1;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.25f}};
  HkxExtractedMotion motion;
  ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok);

  EXPECT_EQ(motion.kind, HkxMotionKind::Default);
  EXPECT_EQ(motion.samples.size(), 2u);
}

TEST(HkxMotion, AZeroUpOrForwardIsBadSectionNotAnAxisGuess) {
  MotionSpec zero_up;
  zero_up.up = {0.0f, 0.0f, 0.0f};
  zero_up.samples = {{0.0f, 0.0f, 0.0f, 0.0f}};
  HkxExtractedMotion motion;
  EXPECT_EQ(read_motion(motion_packfile(zero_up), motion), HkxStatus::BadSection);
  EXPECT_TRUE(motion.samples.empty());

  MotionSpec zero_forward;
  zero_forward.forward = {0.0f, 0.0f, 0.0f};
  zero_forward.samples = {{0.0f, 0.0f, 0.0f, 0.0f}};
  EXPECT_EQ(read_motion(motion_packfile(zero_forward), motion), HkxStatus::BadSection);
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, ANonEmptySampleArrayWithoutItsPointerOrPastTheDataIsRefusedNotReadAsEmpty) {
  // Constraint: detects a counted but unreachable sample array read as a clip with no travel.
  for (const auto link : {SampleLink::NoPointer, SampleLink::Truncated}) {
    MotionSpec spec;
    spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}};
    spec.sample_link = link;
    HkxExtractedMotion motion;
    EXPECT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::FixupOutOfRange);
    EXPECT_TRUE(motion.samples.empty());
  }
}

TEST(HkxMotion, TwoSupportedClipsInOnePackfileAreRefusedForMotionToo) {
  // Constraint: detects motion read from the first of two clips instead of a refusal.
  MotionSpec spec;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}};
  spec.second_clip = true;
  HkxExtractedMotion motion;
  EXPECT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::UnsupportedLayout);
  EXPECT_EQ(motion.kind, HkxMotionKind::None);
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, SummarizeMotionProjectsStepsOntoThePlaneOrthogonalToUp) {
  fo4::hkx::HkxExtractedMotion motion;
  motion.kind = fo4::hkx::HkxMotionKind::Default;
  motion.duration = 2.0f;
  motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 5.0f, 0.5f}, {3.0f, 4.0f, 5.0f, 1.0f}};
  const auto summary = fo4::hkx::summarize_motion(motion);
  EXPECT_FLOAT_EQ(summary.net_translation[0], 3.0f);
  EXPECT_FLOAT_EQ(summary.net_translation[1], 4.0f);
  EXPECT_FLOAT_EQ(summary.net_translation[2], 5.0f);
  EXPECT_FLOAT_EQ(summary.net_turn_radians, 1.0f);
  EXPECT_FLOAT_EQ(summary.mean_planar_speed, 3.5f);
  EXPECT_FLOAT_EQ(summary.mean_turn_rate, 0.5f);
}

TEST(HkxMotion, FrameTypesOutsideZeroAndOneAreUnsupportedWithNoSamples) {
  // Constraint: detects a signed frame type byte read as a Default frame, which would decode samples from a frame the format does not define.
  for (const std::uint8_t type : {std::uint8_t{3}, std::uint8_t{128}, std::uint8_t{255}}) {
    MotionSpec spec;
    spec.frame_type = type;
    spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.25f}};
    HkxExtractedMotion motion;
    ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok) << static_cast<int>(type);
    EXPECT_EQ(motion.kind, HkxMotionKind::Unsupported) << static_cast<int>(type);
    EXPECT_TRUE(motion.samples.empty()) << static_cast<int>(type);
  }
}

TEST(HkxMotion, AFrameTheAnimationDoesNotPointAtIsNotBorrowedAsItsMotion) {
  // Constraint: detects an unreferenced reference frame in the file read as the clip's travel.
  MotionSpec spec;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.25f}};
  spec.link_frame = false;
  HkxExtractedMotion motion;
  ASSERT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::Ok);
  EXPECT_EQ(motion.kind, HkxMotionKind::None);
  EXPECT_TRUE(motion.samples.empty());
}

TEST(HkxMotion, ANonFiniteYawSampleIsBadSectionEvenWhenTheTravelIsFinite) {
  // Constraint: detects a sample with finite travel and a NaN or infinite yaw kept, which would turn the root by an undefined angle.
  for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    MotionSpec spec;
    spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 30.0f, 0.0f, 0.5f}, {-40.0f, 30.0f, 0.0f, bad}};
    HkxExtractedMotion motion;
    EXPECT_EQ(read_motion(motion_packfile(spec), motion), HkxStatus::BadSection);
    EXPECT_EQ(motion.kind, HkxMotionKind::None);
    EXPECT_TRUE(motion.samples.empty());
  }
}

TEST(HkxMotion, SummarizeMotionAccumulatesCurvedTravelOnTheUpPlaneAndKeepsTurnsUnwrapped) {
  // Constraint: detects a curved path measured as its net length, an up axis ignored, or a multi-turn angle wrapped into one turn.
  constexpr float kPi = std::numbers::pi_v<float>;
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = 2.0f;
  motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 30.0f, 0.0f, kPi / 4.0f}, {-40.0f, 30.0f, 0.0f, kPi / 2.0f}};

  auto summary = fo4::hkx::summarize_motion(motion);
  EXPECT_NEAR(summary.net_translation[0], -40.0f, 1e-4f);
  EXPECT_NEAR(summary.net_translation[1], 30.0f, 1e-4f);
  EXPECT_NEAR(summary.net_translation[2], 0.0f, 1e-4f);
  EXPECT_NEAR(summary.mean_planar_speed, 35.0f, 1e-4f);
  EXPECT_NEAR(summary.net_turn_radians, kPi / 2.0f, 1e-5f);
  EXPECT_NEAR(summary.mean_turn_rate, kPi / 4.0f, 1e-5f);

  motion.up = {0.0f, 1.0f, 0.0f};
  EXPECT_NEAR(fo4::hkx::summarize_motion(motion).mean_planar_speed, 20.0f, 1e-4f);

  motion.up = {0.0f, 0.0f, 1.0f};
  motion.samples.back() = {-40.0f, 30.0f, 0.0f, -3.0f * kPi};
  summary = fo4::hkx::summarize_motion(motion);
  EXPECT_NEAR(summary.net_turn_radians, -3.0f * kPi, 1e-4f);
  EXPECT_NEAR(summary.mean_turn_rate, -1.5f * kPi, 1e-4f);
}

namespace {

HkxExtractedMotion path_motion() {
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = 2.0f;
  motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f, 1.0f}, {2.0f, 4.0f, 6.0f, 3.0f}};
  return motion;
}

}  // namespace

// Constraint: detects a query that extrapolates past either end instead of clamping to the first or last sample.
TEST(HkxMotion, QueriesInterpolateBetweenSamplesAndClampPastEitherEnd) {
  const HkxExtractedMotion motion = path_motion();
  const auto middle = fo4::hkx::motion_at_fraction(motion, 0.25f);
  EXPECT_NEAR(middle.position[0], 1.0f, 1e-6f);
  EXPECT_NEAR(middle.turn_radians, 0.5f, 1e-6f);
  const auto second = fo4::hkx::motion_at_fraction(motion, 0.75f);
  EXPECT_NEAR(second.position[1], 2.0f, 1e-6f);
  EXPECT_NEAR(second.position[2], 3.0f, 1e-6f);
  EXPECT_NEAR(second.turn_radians, 2.0f, 1e-6f);

  EXPECT_EQ(fo4::hkx::motion_at_fraction(motion, -3.0f).position, (std::array<float, 3>{0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(fo4::hkx::motion_at_fraction(motion, 9.0f).position, (std::array<float, 3>{2.0f, 4.0f, 6.0f}));
  EXPECT_NEAR(fo4::hkx::motion_at_time(motion, 99.0f).turn_radians, 3.0f, 1e-6f);
  EXPECT_NEAR(fo4::hkx::motion_at_time(motion, -5.0f).turn_radians, 0.0f, 1e-6f);
  EXPECT_NEAR(fo4::hkx::motion_at_time(motion, 1.0f).position[0], 2.0f, 1e-6f);
  EXPECT_EQ(fo4::hkx::motion_at_time(motion, std::numeric_limits<float>::quiet_NaN()).position,
            (std::array<float, 3>{0.0f, 0.0f, 0.0f}));
  EXPECT_NEAR(fo4::hkx::motion_at_fraction(motion, std::numeric_limits<float>::quiet_NaN()).turn_radians, 0.0f, 1e-6f);
}

// Constraint: detects a looped query that accumulates past one cycle or that wraps a negative time to a negative fraction.
TEST(HkxMotion, ALoopedQueryWrapsIntoOneCycleWithoutAccumulatingPastIt) {
  const HkxExtractedMotion motion = path_motion();
  const auto once = fo4::hkx::motion_at_time(motion, 0.5f, true);
  const auto twice = fo4::hkx::motion_at_time(motion, 4.5f, true);
  EXPECT_EQ(once.position, twice.position);
  EXPECT_NEAR(once.turn_radians, twice.turn_radians, 1e-6f);
  const auto before_start = fo4::hkx::motion_at_time(motion, -0.5f, true);
  const auto last_quarter = fo4::hkx::motion_at_time(motion, 1.5f, true);
  EXPECT_EQ(before_start.position, last_quarter.position);
  EXPECT_NEAR(fo4::hkx::motion_at_time(motion, 2.0f, true).turn_radians, 0.0f, 1e-6f);
  EXPECT_NEAR(fo4::hkx::motion_at_time(motion, 2.0f, false).turn_radians, 3.0f, 1e-6f);

  HkxExtractedMotion instant = motion;
  instant.duration = 0.0f;
  EXPECT_EQ(fo4::hkx::motion_at_time(instant, 5.0f, true).position, (std::array<float, 3>{0.0f, 0.0f, 0.0f}));
}

// Constraint: detects travel that is kept but placed, which would send the preview to a non-finite or absurdly far point.
TEST(HkxMotion, InvalidTravelIsNeverPlacedAndAQueryAnswersZero) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const auto zero = std::array<float, 3>{0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(fo4::hkx::motion_previewable(path_motion()));

  HkxExtractedMotion bad = path_motion();
  bad.samples[1][0] = nan;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  EXPECT_EQ(fo4::hkx::motion_at_fraction(bad, 0.5f).position, zero);
  bad = path_motion();
  bad.samples[2][3] = inf;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  bad = path_motion();
  bad.samples[2][1] = 5.0e6f;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  EXPECT_EQ(fo4::hkx::motion_at_time(bad, 2.0f).position, zero);
  EXPECT_EQ(fo4::hkx::motion_at_time(bad, 2.0f).turn_radians, 0.0f);
  bad = path_motion();
  bad.duration = nan;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  bad = path_motion();
  bad.up = {0.0f, nan, 1.0f};
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  bad = path_motion();
  bad.kind = HkxMotionKind::Unsupported;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  bad = path_motion();
  bad.kind = HkxMotionKind::None;
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  bad = path_motion();
  bad.samples.clear();
  EXPECT_FALSE(fo4::hkx::motion_previewable(bad));
  EXPECT_EQ(fo4::hkx::motion_at_fraction(bad, 0.5f).position, zero);
}
