#include "hkx_animation_fixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_animation;
using fo4::hkx::read_packfile;
using namespace fo4::test::hkx;

// Constraint: the lossless cases live in their own file, so the spline and interleaved suite keeps line headroom.
TEST(HkxAnimation, ALosslessFloatOnlyClipKeepsItsDeclaredFramesAndFloatSamples) {
  // Constraint: detects a zero-track clip whose declared frames and float samples were dropped, so it read as empty.
  const auto bytes = lossless_shape_packfile(4, 0, 0, 0, 2, 8, 0, 0.0f, true, 2.5f);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, HkxAnimationKind::Lossless);
  EXPECT_EQ(animation.frame_count, 4u);
  EXPECT_FLOAT_EQ(animation.frame_duration, 2.0f / 3.0f);
  EXPECT_EQ(animation.blocks.count, 1u);
  EXPECT_EQ(animation.blocks.max_frames_per_block, 4u);
  EXPECT_EQ(animation.float_track_count, 2);
  ASSERT_EQ(animation.float_tracks.size(), 8u);
  EXPECT_FLOAT_EQ(animation.float_tracks[0], 2.5f);
  EXPECT_TRUE(animation.tracks.empty());
}
