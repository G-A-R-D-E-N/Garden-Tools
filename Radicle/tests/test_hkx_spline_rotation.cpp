#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_animation;
using namespace fo4::test::hkx;

TEST(HkxAnimation, DecodesAThreeComponentQuaternionAtTwentyFourBits) {
  // Detects the 24-bit format read at the 40-bit width, which must consume three bytes per control point.
  const auto bytes = spline_rotation_packfile(3, 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].rotations.size(), 2u);
  for (const std::array<float, 4>& rotation : animation.tracks[0].rotations) {
    EXPECT_NEAR(rotation[0], 0.0f, 1e-6f);
    EXPECT_NEAR(rotation[1], 1.0f, 1e-6f);
    EXPECT_NEAR(rotation[2], 0.0f, 1e-6f);
    EXPECT_NEAR(rotation[3], 0.0f, 1e-6f);
  }
  EXPECT_TRUE(animation.tracks[0].rotation_animated);
}

TEST(HkxAnimation, DecodesAFourNibbleQuaternionAtSixteenBits) {
  // Detects the 16-bit format read at the 40-bit width, which must consume two bytes per control point.
  const auto bytes = spline_rotation_packfile(4, 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].rotations.size(), 2u);
  for (const std::array<float, 4>& rotation : animation.tracks[0].rotations) {
    EXPECT_NEAR(rotation[0], 1.0f, 1e-6f);
    EXPECT_NEAR(rotation[1], 0.0f, 1e-6f);
    EXPECT_NEAR(rotation[2], 0.0f, 1e-6f);
    EXPECT_NEAR(rotation[3], 0.0f, 1e-6f);
  }
}

TEST(HkxAnimation, ASplineRotationQuantisationOutsideTheSixFormatsIsRejected) {
  // Detects an unknown format decoded at the 40-bit width instead of refused: the width must be known.
  const auto bytes = spline_rotation_packfile(6, 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
}

TEST(HkxAnimation, ASplineDegreeAboveThreeIsRejected) {
  // Detects a knot table accepted at a degree Havok never writes: the degree must stay at or below three.
  const auto bytes = spline_rotation_packfile(3, 4);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
}

TEST(HkxAnimation, ASplineRotationKnotsThatDescendAreBadSection) {
  // Requires nondecreasing knots, so a descending rotation channel must be refused.
  const auto bytes = spline_rotation_packfile(3, 1, true);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineDegreeAboveItsItemCountIsBadSection) {
  // Requires degree + 1 control points, so a degree-2 rotation over two points must be refused.
  const auto bytes = spline_rotation_packfile(3, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineQuaternionWithHugeComponentsNormalisesToAUnitQuaternion) {
  // Requires the squared length in double, so components near 2e20 must not overflow to a zero quaternion.
  const auto bytes = spline_static_rotation_packfile({2e20f, 0.0f, 0.0f, 2e20f});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].rotations.size(), 2u);
  EXPECT_NEAR(animation.tracks[0].rotations[0][0], 0.70710678f, 1e-6f);
  EXPECT_NEAR(animation.tracks[0].rotations[0][3], 0.70710678f, 1e-6f);
}

TEST(HkxAnimation, ASplineQuaternionWithTinyComponentsNormalisesToAUnitQuaternion) {
  // Requires the squared length in double, so components near 1e-30 must not underflow to the identity.
  const auto bytes = spline_static_rotation_packfile({1e-30f, 0.0f, 0.0f, 1e-30f});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].rotations.size(), 2u);
  EXPECT_NEAR(animation.tracks[0].rotations[0][0], 0.70710678f, 1e-6f);
  EXPECT_NEAR(animation.tracks[0].rotations[0][3], 0.70710678f, 1e-6f);
}

TEST(HkxAnimation, ASplineZeroQuaternionIsBadSection) {
  // Requires a zero-length quaternion to be refused rather than replaced by identity.
  const auto bytes = spline_static_rotation_packfile({0.0f, 0.0f, 0.0f, 0.0f});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}
