#include "fo4/c_api_hkx.h"
#include "fo4/hkx/layout.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_motion_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace fo4::test::hkx;

namespace {

struct HkxDeleter {
  void operator()(radicle_hkx* hkx) const { radicle_hkx_destroy(hkx); }
};
using HkxHandle = std::unique_ptr<radicle_hkx, HkxDeleter>;

HkxHandle open(const std::vector<std::byte>& bytes) {
  radicle_hkx* raw = nullptr;
  EXPECT_EQ(radicle_hkx_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &raw),
            RADICLE_STATUS_OK);
  return HkxHandle(raw);
}

std::string take_text(uint8_t* bytes, size_t size) {
  if (!bytes) return {};
  std::string text(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return text;
}

std::vector<float> take_floats(float* values, size_t count) {
  std::vector<float> result;
  if (values) result.assign(values, values + count);
  radicle_free_floats(values);
  return result;
}

std::vector<std::int16_t> take_indices(int16_t* values, size_t count) {
  std::vector<std::int16_t> result;
  if (values) result.assign(values, values + count);
  radicle_hkx_free_indices(values);
  return result;
}

}  // namespace

TEST(CApiHkx, SkeletonBonesParentsAndPoseDecodeThroughTheCAbi) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_skeleton_name(hkx.get(), &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "Rig");
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_skeleton_bone_count(hkx.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 3u);

  const char* const expected_names[] = {"Root", "Spine", "Head"};
  const int16_t expected_parents[] = {-1, 0, 1};
  for (size_t bone = 0; bone < count; ++bone) {
    ASSERT_EQ(radicle_hkx_skeleton_bone_name(hkx.get(), bone, &name, &size), RADICLE_STATUS_OK);
    EXPECT_EQ(take_text(name, size), expected_names[bone]);
    int16_t parent = 99;
    ASSERT_EQ(radicle_hkx_skeleton_bone_parent(hkx.get(), bone, &parent), RADICLE_STATUS_OK);
    EXPECT_EQ(parent, expected_parents[bone]);
    radicle_hkx_qs_transform pose{};
    ASSERT_EQ(radicle_hkx_skeleton_bone_pose(hkx.get(), bone, &pose), RADICLE_STATUS_OK);
    EXPECT_FLOAT_EQ(pose.translation[0], static_cast<float>(bone + 1));
    EXPECT_FLOAT_EQ(pose.translation[1], 0.0f);
    EXPECT_FLOAT_EQ(pose.rotation[0], 0.0f);
    EXPECT_FLOAT_EQ(pose.rotation[3], 1.0f);
    EXPECT_FLOAT_EQ(pose.scale[2], 1.0f);
  }

  // Regression: the ABI must expose partitions and float slots, or FO4IDE cannot name them.
  int32_t locked = -1;
  ASSERT_EQ(radicle_hkx_skeleton_bone_lock_translation(hkx.get(), 1, &locked), RADICLE_STATUS_OK);
  EXPECT_EQ(locked, 1);
  ASSERT_EQ(radicle_hkx_skeleton_bone_lock_translation(hkx.get(), 0, &locked), RADICLE_STATUS_OK);
  EXPECT_EQ(locked, 0);

  float* floats = nullptr;
  size_t floats_count = 0;
  ASSERT_EQ(radicle_hkx_skeleton_reference_floats(hkx.get(), &floats, &floats_count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(floats, floats_count), (std::vector<float>{0.5f, 1.25f}));

  size_t slots = 0;
  ASSERT_EQ(radicle_hkx_skeleton_float_slot_count(hkx.get(), &slots), RADICLE_STATUS_OK);
  ASSERT_EQ(slots, 2u);
  int32_t named = -1;
  ASSERT_EQ(radicle_hkx_skeleton_float_slot(hkx.get(), 0, &named, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(named, 1);
  EXPECT_EQ(take_text(name, size), "speed");
  ASSERT_EQ(radicle_hkx_skeleton_float_slot(hkx.get(), 1, &named, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(named, 0);
  EXPECT_EQ(name, nullptr);

  size_t frames = 0;
  ASSERT_EQ(radicle_hkx_skeleton_local_frame_count(hkx.get(), &frames), RADICLE_STATUS_OK);
  ASSERT_EQ(frames, 1u);
  int16_t frame_bone = -2;
  int32_t has_frame = -1;
  ASSERT_EQ(radicle_hkx_skeleton_local_frame(hkx.get(), 0, &frame_bone, &has_frame), RADICLE_STATUS_OK);
  EXPECT_EQ(frame_bone, 2);
  EXPECT_EQ(has_frame, 1);

  size_t partitions = 0;
  ASSERT_EQ(radicle_hkx_skeleton_partition_count(hkx.get(), &partitions), RADICLE_STATUS_OK);
  ASSERT_EQ(partitions, 1u);
  int16_t start_bone = -1;
  int16_t bone_count = -1;
  ASSERT_EQ(radicle_hkx_skeleton_partition(hkx.get(), 0, &named, &name, &size, &start_bone, &bone_count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(named, 1);
  EXPECT_EQ(take_text(name, size), "upper");
  EXPECT_EQ(start_bone, 1);
  EXPECT_EQ(bone_count, 2);
}

TEST(CApiHkx, LosslessAnimationReportsKindThreeAndItsDecodedTracks) {
  // Constraint: detects a kind that missed the Radicle enum and a track read through the ABI at the wrong stride.
  const auto hkx = open(lossless_packfile());
  ASSERT_TRUE(hkx);
  radicle_hkx_animation_header header{};
  ASSERT_EQ(radicle_hkx_read_animation_header(hkx.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.kind, 3);
  EXPECT_EQ(header.frame_count, 3u);
  EXPECT_EQ(header.track_count, 2u);
  EXPECT_FLOAT_EQ(header.duration, 2.0f);
  EXPECT_EQ(header.annotation_track_count, 1u);

  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_animation_track_translations(hkx.get(), 1, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{5, 0, 0, 15, 0, 0, 25, 0, 0}));

  radicle_hkx_track_flags flags{};
  ASSERT_EQ(radicle_hkx_animation_track_flags(hkx.get(), 1, &flags), RADICLE_STATUS_OK);
  EXPECT_EQ(flags.translation_animated[0], 1);
  EXPECT_EQ(flags.translation_animated[1], 0);
  EXPECT_EQ(flags.rotation_animated, 1);
  EXPECT_EQ(flags.scale_animated[0], 0);
}

TEST(CApiHkx, AnimationHeaderTracksAndFloatsDecodeThroughTheCAbi) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  radicle_hkx_animation_header header{};
  ASSERT_EQ(radicle_hkx_read_animation_header(hkx.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.kind, 0);
  EXPECT_EQ(header.type, 1);
  EXPECT_FLOAT_EQ(header.duration, 1.0f);
  EXPECT_EQ(header.frame_count, 2u);
  EXPECT_FLOAT_EQ(header.frame_duration, 1.0f);
  EXPECT_EQ(header.float_track_count, 1);
  EXPECT_EQ(header.block_count, 1u);
  EXPECT_EQ(header.block_max_frames_per_block, 2u);
  EXPECT_FLOAT_EQ(header.block_duration, 1.0f);
  EXPECT_EQ(header.track_count, 2u);
  EXPECT_EQ(header.annotation_track_count, 1u);

  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_animation_class_name(hkx.get(), &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "hkaInterleavedUncompressedAnimation");

  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_animation_track_translations(hkx.get(), 1, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{100, 1, 0, 101, 1, 0}));
  ASSERT_EQ(radicle_hkx_animation_track_translations(hkx.get(), 0, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{0, 0, 0, 1, 0, 0}));
  ASSERT_EQ(radicle_hkx_animation_track_rotations(hkx.get(), 1, &values, &count), RADICLE_STATUS_OK);
  const auto q = turn();
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{0, 0, 0, 1, q[0], q[1], q[2], q[3]}));
  ASSERT_EQ(radicle_hkx_animation_track_rotations(hkx.get(), 0, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1}));
  ASSERT_EQ(radicle_hkx_animation_track_scales(hkx.get(), 1, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{2, 2, 2, 2, 2, 2}));
  ASSERT_EQ(radicle_hkx_animation_track_scales(hkx.get(), 0, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{1, 1, 1, 1, 1, 1}));

  radicle_hkx_track_flags flags{};
  ASSERT_EQ(radicle_hkx_animation_track_flags(hkx.get(), 0, &flags), RADICLE_STATUS_OK);
  for (size_t axis = 0; axis < 3; ++axis) {
    EXPECT_EQ(flags.translation_animated[axis], 1);
    EXPECT_EQ(flags.scale_animated[axis], 1);
  }
  EXPECT_EQ(flags.rotation_animated, 1);

  ASSERT_EQ(radicle_hkx_animation_float_tracks(hkx.get(), &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_floats(values, count), (std::vector<float>{5.5f, 6.5f}));
}

TEST(CApiHkx, AnnotationTrackNameTimesAndTextDecodeThroughTheCAbi) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_animation_annotation_track_name(hkx.get(), 0, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "Root");
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_animation_annotation_count(hkx.get(), 0, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  float time = -1.0f;
  ASSERT_EQ(radicle_hkx_animation_annotation_time(hkx.get(), 0, 0, &time), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(time, 0.25f);
  ASSERT_EQ(radicle_hkx_animation_annotation_text(hkx.get(), 0, 0, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "start");
  ASSERT_EQ(radicle_hkx_animation_annotation_time(hkx.get(), 0, 1, &time), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(time, 0.75f);
  ASSERT_EQ(radicle_hkx_animation_annotation_text(hkx.get(), 0, 1, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "end");
}

TEST(CApiHkx, BindingNamesTheSkeletonTheAnimationAndTheThreeIndexArrays) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  radicle_hkx_binding_header header{};
  ASSERT_EQ(radicle_hkx_read_binding_header(hkx.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.has_animation_offset, 1);
  const size_t expected_offset =
      fo4::hkx::find_layout("hkaSkeleton")->size + fo4::hkx::find_layout("hkaAnimationBinding")->size;
  EXPECT_EQ(header.animation_offset, expected_offset);
  EXPECT_EQ(header.blend_hint, 1);

  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_binding_original_skeleton_name(hkx.get(), &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "Rig");

  int16_t* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_binding_transform_track_to_bone(hkx.get(), &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_indices(values, count), (std::vector<std::int16_t>{0, 2}));
  ASSERT_EQ(radicle_hkx_binding_float_track_to_float_slot(hkx.get(), &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_indices(values, count), (std::vector<std::int16_t>{0}));
  ASSERT_EQ(radicle_hkx_binding_partitions(hkx.get(), &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_indices(values, count), (std::vector<std::int16_t>{5, 6}));
}

TEST(CApiHkx, ABindingWithNoAnimationPointerReportsNoAnimationOffset) {
  const auto hkx = open(clip_packfile(true, {-1, 0, 1}, false));
  ASSERT_TRUE(hkx);
  radicle_hkx_binding_header header{};
  header.has_animation_offset = 7;
  header.animation_offset = 7;
  ASSERT_EQ(radicle_hkx_read_binding_header(hkx.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.has_animation_offset, 0);
  EXPECT_EQ(header.animation_offset, 0u);
  EXPECT_EQ(header.blend_hint, 1);
}

TEST(CApiHkx, APackfileWithoutASkeletonAnswersNotFoundForItAndStillDecodesTheClip) {
  const auto hkx = open(clip_packfile(false));
  ASSERT_TRUE(hkx);
  uint8_t* name = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_hkx_skeleton_name(hkx.get(), &name, &size), RADICLE_STATUS_NOT_FOUND);
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_skeleton_bone_count(hkx.get(), &count), RADICLE_STATUS_NOT_FOUND);
  radicle_hkx_qs_transform pose{};
  EXPECT_EQ(radicle_hkx_skeleton_bone_pose(hkx.get(), 0, &pose), RADICLE_STATUS_NOT_FOUND);
  radicle_hkx_animation_header header{};
  EXPECT_EQ(radicle_hkx_read_animation_header(hkx.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.track_count, 2u);
}

TEST(CApiHkx, APackfileWithNoSkeletonAnimationOrBindingAnswersNotFoundForEveryRead) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", signature_of("hkRootLevelContainer"),
                                            filled(32, std::byte{0}), {});
  const auto hkx = open(bytes);
  ASSERT_TRUE(hkx);
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_skeleton_bone_count(hkx.get(), &count), RADICLE_STATUS_NOT_FOUND);
  radicle_hkx_animation_header animation{};
  EXPECT_EQ(radicle_hkx_read_animation_header(hkx.get(), &animation), RADICLE_STATUS_NOT_FOUND);
  float* values = nullptr;
  EXPECT_EQ(radicle_hkx_animation_float_tracks(hkx.get(), &values, &count), RADICLE_STATUS_NOT_FOUND);
  radicle_hkx_binding_header binding{};
  EXPECT_EQ(radicle_hkx_read_binding_header(hkx.get(), &binding), RADICLE_STATUS_NOT_FOUND);
}

TEST(CApiHkx, ADamagedSkeletonIsAParseErrorWhileTheClipAndBindingStillRead) {
  const auto hkx = open(clip_packfile(true, {-1, 0, 7}));
  ASSERT_TRUE(hkx);
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_skeleton_bone_count(hkx.get(), &count), RADICLE_STATUS_PARSE_ERROR);
  radicle_hkx_animation_header header{};
  EXPECT_EQ(radicle_hkx_read_animation_header(hkx.get(), &header), RADICLE_STATUS_OK);
  radicle_hkx_binding_header binding{};
  EXPECT_EQ(radicle_hkx_read_binding_header(hkx.get(), &binding), RADICLE_STATUS_OK);
}

TEST(CApiHkx, OutOfRangeIndicesAndMissingArgumentsAreInvalidArgument) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  uint8_t* name = nullptr;
  size_t size = 0;
  int16_t parent = 0;
  radicle_hkx_qs_transform pose{};
  EXPECT_EQ(radicle_hkx_skeleton_bone_name(hkx.get(), 3, &name, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_skeleton_bone_parent(hkx.get(), 3, &parent), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_skeleton_bone_pose(hkx.get(), 3, &pose), RADICLE_STATUS_INVALID_ARGUMENT);
  float* values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_animation_track_translations(hkx.get(), 2, &values, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_hkx_track_flags flags{};
  EXPECT_EQ(radicle_hkx_animation_track_flags(hkx.get(), 2, &flags), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_animation_annotation_count(hkx.get(), 1, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  float time = 0.0f;
  EXPECT_EQ(radicle_hkx_animation_annotation_time(hkx.get(), 0, 2, &time), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_animation_annotation_text(hkx.get(), 0, 2, &name, &size), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_hkx_skeleton_bone_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_skeleton_bone_count(hkx.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_animation_track_scales(hkx.get(), 0, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiHkx, BytesThatAreNotAPackfileFailTheReadAndLeaveTheHandleNull) {
  const std::vector<std::byte> garbage(64, std::byte{0});
  radicle_hkx* hkx = reinterpret_cast<radicle_hkx*>(1);
  EXPECT_EQ(radicle_hkx_read(reinterpret_cast<const uint8_t*>(garbage.data()), garbage.size(), &hkx),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(hkx, nullptr);
  EXPECT_EQ(radicle_hkx_read(nullptr, 0, &hkx), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_read(reinterpret_cast<const uint8_t*>(garbage.data()), 0, &hkx),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiHkx, AClipWithoutExtractedMotionReportsKindNoneWithNoSamples) {
  const auto handle = open(motion_packfile(std::nullopt));
  radicle_hkx_motion_header header{};
  ASSERT_EQ(radicle_hkx_read_motion_header(handle.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.kind, 0);
  EXPECT_EQ(header.sample_count, 0u);
}

TEST(CApiHkx, ExtractedMotionHeaderAndSamplesReachTheCallerInFileOrder) {
  MotionSpec spec;
  spec.duration = 2.0f;
  spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 5.0f, 0.5f}, {3.0f, 4.0f, 5.0f, 1.0f}};
  const auto handle = open(motion_packfile(spec));
  radicle_hkx_motion_header header{};
  ASSERT_EQ(radicle_hkx_read_motion_header(handle.get(), &header), RADICLE_STATUS_OK);
  EXPECT_EQ(header.kind, 1);
  EXPECT_FLOAT_EQ(header.duration, 2.0f);
  EXPECT_FLOAT_EQ(header.up[2], 1.0f);
  EXPECT_FLOAT_EQ(header.forward[1], 1.0f);
  EXPECT_EQ(header.sample_count, 3u);
  EXPECT_FLOAT_EQ(header.net_translation[0], 3.0f);
  EXPECT_FLOAT_EQ(header.net_translation[1], 4.0f);
  EXPECT_FLOAT_EQ(header.net_translation[2], 5.0f);
  EXPECT_FLOAT_EQ(header.net_turn_radians, 1.0f);
  EXPECT_FLOAT_EQ(header.mean_planar_speed, 3.5f);
  EXPECT_FLOAT_EQ(header.mean_turn_rate, 0.5f);

  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_motion_samples(handle.get(), &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 12u);
  EXPECT_EQ(take_floats(values, count),
            (std::vector<float>{0, 0, 0, 0, 3, 0, 5, 0.5f, 3, 4, 5, 1}));
}
