#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_motion_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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
