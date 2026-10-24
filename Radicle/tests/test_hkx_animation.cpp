#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_animation_manifest.hpp"
#include "hkx_seam_ratio.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_animation;
using fo4::hkx::read_binding;
using namespace fo4::test::hkx;

namespace {

constexpr std::size_t kAnnotationTrackSize = 24;
constexpr std::size_t kAnnotationSize = 16;

void put_identity_transform(std::vector<std::byte>& data, float translation_x, float translation_y,
                            float translation_z) {
  put_float(data, translation_x);  // offset 0: translation
  put_float(data, translation_y);
  put_float(data, translation_z);
  put_float(data, 0.0f);
  put_float(data, 0.0f);  // offset 16: rotation
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);
  put_float(data, 1.0f);  // offset 32: scale
  put_float(data, 1.0f);
  put_float(data, 1.0f);
  put_float(data, 0.0f);
}

std::vector<std::byte> interleaved_packfile(std::size_t tracks, std::size_t frames, std::size_t float_tracks) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                      "hkaInterleavedUncompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t transforms_at = data.size();
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t track = 0; track < tracks; ++track) {
      put_identity_transform(data, static_cast<float>(track * 100 + frame), static_cast<float>(track), 0.0f);
    }
  }
  const std::size_t floats_at = data.size();
  for (std::size_t i = 0; i < frames * float_tracks; ++i) put_float(data, static_cast<float>(i));

  set_int_member(data, "hkaAnimation", "type", 1);
  set_float_member(data, "hkaAnimation", "duration", static_cast<float>(frames - 1));
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(tracks));
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", static_cast<std::uint32_t>(float_tracks));
  link(local, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transforms_at);
  set_count(data, member_at("hkaInterleavedUncompressedAnimation", "transforms"), tracks * frames);
  if (frames * float_tracks > 0) {
    link(local, member_at("hkaInterleavedUncompressedAnimation", "floats"), floats_at);
  }
  set_count(data, member_at("hkaInterleavedUncompressedAnimation", "floats"), frames * float_tracks);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

std::vector<std::byte> interleaved_shape_packfile(std::uint32_t declared_tracks, std::size_t transform_count) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                      "hkaInterleavedUncompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t transforms_at = data.size();
  for (std::size_t i = 0; i < transform_count; ++i) {
    put_identity_transform(data, static_cast<float>(i), 0.0f, 0.0f);
  }

  set_int_member(data, "hkaAnimation", "type", 1);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", declared_tracks);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  if (transform_count > 0) {
    link(local, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transforms_at);
  }
  set_count(data, member_at("hkaInterleavedUncompressedAnimation", "transforms"),
            static_cast<std::uint32_t>(transform_count));

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

std::size_t append_string(std::vector<std::byte>& data, const std::string& text) {
  const std::size_t at = data.size();
  for (char ch : text) data.push_back(static_cast<std::byte>(ch));
  data.push_back(std::byte{0});
  return at;
}

void add_annotation_tracks(std::vector<std::byte>& data, const std::string& track_name,
                           const std::vector<std::pair<float, std::string>>& annotations,
                           std::vector<std::uint32_t>& local, std::size_t object_offset) {
  const std::size_t tracks_at = data.size();
  data.resize(tracks_at + kAnnotationTrackSize, std::byte{0});
  const std::size_t events_at = data.size();
  data.resize(events_at + annotations.size() * kAnnotationSize, std::byte{0});

  link(local, tracks_at + member_at("hkaAnnotationTrack", "trackName"), append_string(data, track_name));
  link(local, tracks_at + member_at("hkaAnnotationTrack", "annotations"), events_at);
  patch_u32(data, tracks_at + member_at("hkaAnnotationTrack", "annotations") + 8,
            static_cast<std::uint32_t>(annotations.size()));

  for (std::size_t i = 0; i < annotations.size(); ++i) {
    const std::size_t event_at = events_at + i * kAnnotationSize;
    patch_float(data, event_at + member_at("hkaAnnotationTrackAnnotation", "time"), annotations[i].first);
    link(local, event_at + member_at("hkaAnnotationTrackAnnotation", "text"),
         append_string(data, annotations[i].second));
  }

  const std::size_t field = object_offset + member_at("hkaAnimation", "annotationTracks");
  link(local, field, tracks_at);
  set_count(data, field, 1);
}

std::vector<std::byte> animated_with_annotations_packfile() {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                      "hkaInterleavedUncompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t transforms_at = data.size();
  put_identity_transform(data, 1.0f, 2.0f, 3.0f);
  put_identity_transform(data, 4.0f, 5.0f, 6.0f);
  set_int_member(data, "hkaAnimation", "type", 1);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  link(local, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transforms_at);
  set_count(data, member_at("hkaInterleavedUncompressedAnimation", "transforms"), 2);

  add_annotation_tracks(data, "Root", {{0.0f, "start"}, {0.5f, "mid"}}, local, 0);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

}  // namespace

TEST(HkxAnimation, DecodesEveryFrameOfEveryInterleavedTrack) {
  const auto bytes = interleaved_packfile(2, 3, 0);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  EXPECT_EQ(animation.kind, HkxAnimationKind::Interleaved);
  EXPECT_EQ(animation.frame_count, 3u);
  EXPECT_EQ(animation.tracks.size(), 2u);
  EXPECT_FLOAT_EQ(animation.duration, 2.0f);
  EXPECT_FLOAT_EQ(animation.frame_duration, 1.0f);
  ASSERT_EQ(animation.tracks[1].translations.size(), 3u);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[2][0], 2.0f);
  EXPECT_FLOAT_EQ(animation.tracks[1].translations[0][0], 100.0f);
  EXPECT_FLOAT_EQ(animation.tracks[1].translations[1][1], 1.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].rotations[0][3], 1.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].scales[0][2], 1.0f);
  EXPECT_TRUE(animation.tracks[0].rotation_animated);
}

TEST(HkxAnimation, DecodesInterleavedFloatTracksInFrameOrder) {
  const auto bytes = interleaved_packfile(1, 2, 3);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.float_track_count, 3);
  ASSERT_EQ(animation.float_tracks.size(), 6u);
  EXPECT_FLOAT_EQ(animation.float_tracks[0], 0.0f);
  EXPECT_FLOAT_EQ(animation.float_tracks[5], 5.0f);
}

TEST(HkxAnimation, AnInterleavedTrackCountOfTwoBillionIsRefusedBeforeAllocating) {
  // Detects a track array resized from numberOfTransformTracks before the transforms array must cover it.
  const auto bytes = interleaved_shape_packfile(0x7FFFFFFFu, 1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, AnInterleavedTransformArrayThatIsNotWholeFramesIsBadSection) {
  // Detects a frame count from a division the declared track count must divide evenly.
  const auto bytes = interleaved_shape_packfile(2, 3);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, AnInterleavedClipWithTracksAndNoTransformsIsBadSection) {
  // Detects a frame count divided out of an empty array: a file claiming tracks must hold their samples.
  const auto bytes = interleaved_shape_packfile(2, 0);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, SplineClipEvaluatesAHandBuiltLinearSplineAtKnownFrames) {
  const auto bytes = spline_packfile(-1, -1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  EXPECT_EQ(animation.kind, HkxAnimationKind::Spline);
  EXPECT_EQ(animation.frame_count, 2u);
  EXPECT_EQ(animation.blocks.count, 1u);
  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].translations.size(), 2u);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[0][0], 0.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[1][0], 10.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[0][1], 0.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[1][2], 0.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].rotations[1][3], 1.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].scales[0][0], 1.0f);
  EXPECT_TRUE(animation.tracks[0].translation_animated[0]);
  EXPECT_FALSE(animation.tracks[0].translation_animated[1]);
  EXPECT_TRUE(animation.tracks[0].rotation_animated);
}

TEST(HkxAnimation, SplineBlockOffsetPastTheDataArrayIsTruncated) {
  const auto bytes = spline_packfile(9999, -1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::Truncated);
}

TEST(HkxAnimation, SplinePayloadThatStartsPastTheBlockIsTruncated) {
  const auto bytes = spline_packfile(-1, 4096);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::Truncated);
}

TEST(HkxAnimation, ASplineFrameCountBeyondTheDeclaredBlocksIsBadSection) {
  // Detects arrays sized from numFrames: the declared block geometry must cover every frame.
  const auto bytes = spline_shape_packfile(1, 100, 1, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
}

TEST(HkxAnimation, ASplineFrameCountOfTwoBillionIsRefusedBeforeAllocating) {
  // Detects a decoder that trusts numFrames, which must never size hundreds of gigabytes of samples.
  const auto bytes = spline_shape_packfile(1, 0x7FFFFFFFu, 1, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineTrackCountBeyondTheDataArrayIsTruncated) {
  // Detects a track array sized from numberOfTransformTracks before the mask room must be checked.
  const auto bytes = spline_shape_packfile(4096, 2, 1, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::Truncated);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineTrackCountOfTwoBillionIsRefusedBeforeAllocating) {
  // Detects a resized track array built from a declared count the data must be able to hold.
  const auto bytes = spline_shape_packfile(0x7FFFFFFFu, 2, 1, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineClipThatDeclaresNoBlocksIsRejected) {
  // Detects a geometry computation that must refuse numBlocks zero before it can divide by it.
  const auto bytes = spline_shape_packfile(1, 2, 0, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineClipThatDeclaresNoFramesPerBlockIsRejected) {
  // Detects a division by maxFramesPerBlock, which must be checked before the geometry is computed.
  const auto bytes = spline_shape_packfile(1, 2, 1, 0);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineBlockCountThatOvershootsTheFrameCountIsRejected) {
  // Detects a padded non-last block: a block count the frames cannot fill must be refused.
  const auto bytes = spline_shape_packfile(1, 3, 2, 4);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::BadSection);
}

TEST(HkxAnimation, ALaterSplineBlockStartsOneFrameBeforeThePreviousBlockEnds) {
  // Detects a block placed at block * maxFramesPerBlock: the later block must own the shared frame.
  const auto bytes = spline_two_block_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  EXPECT_EQ(animation.frame_count, 7u);
  EXPECT_EQ(animation.blocks.count, 2u);
  ASSERT_EQ(animation.tracks.size(), 1u);
  ASSERT_EQ(animation.tracks[0].translations.size(), 7u);
  const std::array<float, 7> expected{11.0f, 11.0f, 11.0f, 77.0f, 77.0f, 77.0f, 77.0f};
  for (std::size_t frame = 0; frame < expected.size(); ++frame) {
    EXPECT_FLOAT_EQ(animation.tracks[0].translations[frame][0], expected[frame]) << "frame " << frame;
  }
}

TEST(HkxAnimation, ASplinePositionChannelReadsControlPointsOnlyForItsSplineAxes) {
  // Detects a control point read for a static or identity axis, which must leave the later tracks unshifted.
  const auto bytes = spline_mixed_position_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);

  ASSERT_EQ(animation.tracks.size(), 2u);
  ASSERT_EQ(animation.tracks[0].translations.size(), 2u);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[0][0], 0.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[0][1], 4.0f);
  EXPECT_FLOAT_EQ(animation.tracks[0].translations[0][2], 0.0f);
  ASSERT_EQ(animation.tracks[1].translations.size(), 2u);
  EXPECT_FLOAT_EQ(animation.tracks[1].translations[0][0], 7.0f);
  EXPECT_FLOAT_EQ(animation.tracks[1].translations[0][1], 8.0f);
  EXPECT_FLOAT_EQ(animation.tracks[1].translations[0][2], 9.0f);
}

TEST(HkxAnimation, ASplineClipWithNoFramesAndNoDataIsTruncated) {
  // Detects a spline decode that must check the mask room even when there are no frames to read.
  const auto bytes = spline_zero_frame_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::Truncated);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ASplineClipWithNoFramesAndRoomForTheMasksDecodesNoSamples) {
  // Detects a zero-frame clip refused once the mask room is present: it must decode with empty samples.
  const auto bytes = spline_zero_frame_packfile(4);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.frame_count, 0u);
  ASSERT_EQ(animation.tracks.size(), 1u);
  EXPECT_TRUE(animation.tracks[0].translations.empty());
  EXPECT_TRUE(animation.tracks[0].rotations.empty());
  EXPECT_TRUE(animation.tracks[0].scales.empty());
}

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

TEST(HkxAnimation, TheSeamRatioAggregateCollapsesOnlyForAPackedBlockStride) {
  // Detects a packed stride at the aggregate: it must repeat frame 255 at 256, so the seam step
  // must fall to zero while the stride-255 reading keeps it level with its neighbours.
  constexpr std::size_t kTracks = 16;
  constexpr std::size_t kBlockFrames = 256;
  constexpr std::size_t kFrames = 300;
  const auto ramped_clip = [&](bool packed) {
    HkxAnimation animation;
    animation.blocks.count = 2;
    animation.blocks.max_frames_per_block = kBlockFrames;
    animation.frame_count = kFrames;
    animation.tracks.resize(kTracks);
    for (fo4::hkx::HkxAnimationTrack& track : animation.tracks) {
      for (std::size_t frame = 0; frame < kFrames; ++frame) {
        const float value =
            packed && frame >= kBlockFrames ? static_cast<float>(frame - 1) : static_cast<float>(frame);
        track.translations.push_back({value, 0.0f, 0.0f});
        track.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
        track.scales.push_back({1.0f, 1.0f, 1.0f});
      }
    }
    return animation;
  };

  const auto held = fo4::test::hkx::seam_ratio(ramped_clip(false), 1e-4f);
  const auto collapsed = fo4::test::hkx::seam_ratio(ramped_clip(true), 1e-4f);
  EXPECT_EQ(held.moving, kTracks);
  EXPECT_EQ(held.within_band, held.moving);
  EXPECT_GE(held.median, 0.5f);
  EXPECT_EQ(collapsed.moving, kTracks);
  EXPECT_EQ(collapsed.within_band, 0u);
  EXPECT_LT(collapsed.median, 0.5f);
}

TEST(HkxAnimation, TheManifestPinsTheDecodedShapeAndPosesOfTheHandBuiltSpline) {
  // Detects manifest format drift and a pose that must decode to the hand-computed 0 and 10.
  const auto bytes = spline_packfile(-1, -1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  EXPECT_EQ(fo4::test::hkx::animation_manifest(animation),
            "hkx-animation-manifest 1\n"
            "class hkaSplineCompressedAnimation\n"
            "frames 2\n"
            "tracks 1\n"
            "blocks 1\n"
            "max_frames_per_block 2\n"
            "duration_tenk 10000\n"
            "frame_duration_tenk 10000\n"
            "track 0 flags 100 1 000\n"
            "track 0 first 0 0 0 0 0 0 10000 10000 10000 10000\n"
            "track 0 middle 100000 0 0 0 0 0 10000 10000 10000 10000\n"
            "track 0 last 100000 0 0 0 0 0 10000 10000 10000 10000\n"
            "hash e91624ff10f43fd1\n");
}

TEST(HkxAnimation, DecodesAnnotationTrackNamesTimesAndText) {
  const auto bytes = animated_with_annotations_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  ASSERT_EQ(animation.annotation_tracks.size(), 1u);
  EXPECT_EQ(animation.annotation_tracks[0].name, "Root");
  ASSERT_EQ(animation.annotation_tracks[0].annotations.size(), 2u);
  EXPECT_FLOAT_EQ(animation.annotation_tracks[0].annotations[0].time, 0.0f);
  EXPECT_EQ(animation.annotation_tracks[0].annotations[0].text, "start");
  EXPECT_FLOAT_EQ(animation.annotation_tracks[0].annotations[1].time, 0.5f);
  EXPECT_EQ(animation.annotation_tracks[0].annotations[1].text, "mid");
}

TEST(HkxAnimation, AnUndecodedAnimationClassComesBackAsAViewWithItsClassName) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaLosslessCompressedAnimation"), "hkaLosslessCompressedAnimation");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(136, std::byte{0}), {}, {}, {0, 0, name_at}},
  });
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  ASSERT_EQ(read_animation(packfile, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, HkxAnimationKind::View);
  EXPECT_EQ(animation.class_name, "hkaLosslessCompressedAnimation");
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, ReadAnimationReportsMissingObjectWhenNoAnimationIsPresent) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", signature_of("hkRootLevelContainer"),
                                            filled(32, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::MissingObject);
}

TEST(HkxAnimation, ReadAnimationRefusesAnAnimationWhoseSignatureDoesNotMatchTheLayout) {
  const auto bytes = single_object_packfile("hkaInterleavedUncompressedAnimation", 0x00000001u,
                                            filled(88, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(animation.class_name.empty());
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimation, EveryShorterDataArrayOfASplineFileIsRejected) {
  // Detects a block read that must stop at the declared data array rather than the section.
  auto bytes = spline_packfile(-1, -1);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation whole;
  ASSERT_EQ(read_animation(packfile, whole), HkxStatus::Ok);
  const fo4::hkx::HkxSection& section = packfile.sections[packfile.data_section];
  ASSERT_FALSE(packfile.objects.empty());
  const std::size_t count_at = section.file_offset + packfile.objects.front().offset +
                               member_at("hkaSplineCompressedAnimation", "data") + packfile.pointer_size;
  std::uint32_t full = 0;
  std::memcpy(&full, bytes.data() + count_at, sizeof full);
  ASSERT_GT(full, 4u);

  for (std::uint32_t count = 0; count < full; ++count) {
    auto cut = bytes;
    patch_u32(cut, count_at, count);
    HkxPackfile cut_packfile;
    ASSERT_EQ(read_packfile(cut, cut_packfile), HkxStatus::Ok) << "count " << count;
    HkxAnimation animation;
    EXPECT_NE(read_animation(cut_packfile, animation), HkxStatus::Ok) << "count " << count;
  }
}
