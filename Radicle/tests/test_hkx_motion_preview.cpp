#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/motion_preview.hpp"

#include <gtest/gtest.h>

#include <cstddef>

using fo4::hkx::HkxExtractedMotion;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::motion_path_points;
using fo4::hkx::motion_second_marks;

namespace {

HkxExtractedMotion walk(std::size_t samples, float duration) {
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = duration;
  for (std::size_t i = 0; i < samples; ++i) motion.samples.push_back({0.0f, static_cast<float>(i), 0.0f, 0.0f});
  return motion;
}

}  // namespace

TEST(HkxMotionMarks, OnePosePerWholeSecondFollowsTheMotion) {
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = 2.5f;
  motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 5.0f, 0.0f, 0.0f}};
  const auto marks = motion_second_marks(motion);
  ASSERT_EQ(marks.size(), 3u);
  EXPECT_NEAR(marks[0].position[1], 0.0f, 1e-4f);
  EXPECT_NEAR(marks[1].position[1], 2.0f, 1e-4f);
  EXPECT_NEAR(marks[2].position[1], 4.0f, 1e-4f);
}

TEST(HkxMotionMarks, AMotionThatIsNotPreviewableHasNoMarks) {
  HkxExtractedMotion none;
  none.duration = 2.0f;
  EXPECT_TRUE(motion_second_marks(none).empty());
  HkxExtractedMotion no_samples;
  no_samples.kind = HkxMotionKind::Default;
  no_samples.duration = 2.0f;
  EXPECT_TRUE(motion_second_marks(no_samples).empty());
}

TEST(HkxMotionMarks, AVeryLongMotionIsCappedAtTheMarkLimit) {
  HkxExtractedMotion motion;
  motion.kind = HkxMotionKind::Default;
  motion.duration = 1.0e9f;
  motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
  EXPECT_EQ(motion_second_marks(motion).size(), 3601u);
}

TEST(HkxMotionPath, ASampleListWithinTheLimitIsReturnedWhole) {
  const auto points = motion_path_points(walk(4, 1.0f), 10);
  ASSERT_EQ(points.size(), 4u);
  EXPECT_EQ(points[3].position[1], 3.0f);
}

TEST(HkxMotionPath, ALongSampleListIsSpreadEvenlyKeepingBothEnds) {
  const auto points = motion_path_points(walk(101, 1.0f), 11);
  ASSERT_EQ(points.size(), 11u);
  EXPECT_EQ(points.front().position[1], 0.0f);
  EXPECT_EQ(points[5].position[1], 50.0f);
  EXPECT_EQ(points.back().position[1], 100.0f);
}

TEST(HkxMotionPath, AZeroOrOnePointLimitAndAnUnpreviewableMotionStayWithinTheirBounds) {
  EXPECT_TRUE(motion_path_points(walk(4, 1.0f), 0).empty());
  EXPECT_EQ(motion_path_points(walk(4, 1.0f), 1).size(), 1u);
  HkxExtractedMotion none;
  EXPECT_TRUE(motion_path_points(none, 10).empty());
}
