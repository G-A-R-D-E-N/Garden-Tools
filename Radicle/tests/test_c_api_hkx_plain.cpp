#include "fo4/c_api_hkx.h"
#include "fo4/c_api_hkx_plain.h"
#include "fo4/hkx/plain.hpp"
#include "hkx_clip_fixture.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace fo4::test::hkx;

namespace {

constexpr float kU = fo4::hkx::k_plain_units_per_metre;

struct HkxDeleter {
  void operator()(radicle_hkx* hkx) const { radicle_hkx_destroy(hkx); }
};
using HkxHandle = std::unique_ptr<radicle_hkx, HkxDeleter>;

struct ClipDeleter {
  void operator()(radicle_hkx_plain_clip* clip) const { radicle_hkx_plain_clip_destroy(clip); }
};
using ClipHandle = std::unique_ptr<radicle_hkx_plain_clip, ClipDeleter>;

struct RolesDeleter {
  void operator()(radicle_hkx_clip_roles* roles) const { radicle_hkx_clip_roles_destroy(roles); }
};
using RolesHandle = std::unique_ptr<radicle_hkx_clip_roles, RolesDeleter>;

HkxHandle open(const std::vector<std::byte>& bytes) {
  radicle_hkx* raw = nullptr;
  EXPECT_EQ(radicle_hkx_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &raw), RADICLE_STATUS_OK);
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

ClipHandle sample(const HkxHandle& skeleton, const HkxHandle& clip, float rate, int32_t loop = 0) {
  radicle_hkx_plain_clip* raw = nullptr;
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), rate, loop, &raw), RADICLE_STATUS_OK);
  return ClipHandle(raw);
}

void expect_near(const std::vector<float>& actual, const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(actual[i], expected[i], 1e-5f) << i;
}

}  // namespace

TEST(CApiHkxPlain, RestPoseComesBackInEngineAxesAndMetres) {
  const auto hkx = open(clip_packfile(true));
  ASSERT_TRUE(hkx);
  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_plain_skeleton_rest(hkx.get(), &values, &count), RADICLE_STATUS_OK);
  const auto rest = take_floats(values, count);
  ASSERT_EQ(rest.size(), 30u);
  expect_near({rest.begin() + 10, rest.begin() + 20}, {2.0f / kU, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f});
}

TEST(CApiHkxPlain, ASkeletonReadOnAFileWithoutOneIsNotFound) {
  const auto hkx = open(clip_packfile(false));
  ASSERT_TRUE(hkx);
  float* values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_plain_skeleton_rest(hkx.get(), &values, &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(values, nullptr);
}

TEST(CApiHkxPlain, NativeRateSamplesEveryFrameOfEveryBoundTrack) {
  const auto skeleton = open(clip_packfile(true));
  const auto clip = open(clip_packfile(false));
  ASSERT_TRUE(skeleton);
  ASSERT_TRUE(clip);
  const auto sampled = sample(skeleton, clip, 0.0f, 1);
  ASSERT_TRUE(sampled);
  radicle_hkx_plain_clip_info info{};
  ASSERT_EQ(radicle_hkx_plain_clip_read_info(sampled.get(), &info), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(info.length, 1.0f);
  EXPECT_FLOAT_EQ(info.rate, 1.0f);
  EXPECT_EQ(info.loop, 1);
  EXPECT_EQ(info.key_count, 2u);
  EXPECT_EQ(info.track_count, 2u);
  EXPECT_EQ(info.event_count, 2u);
  EXPECT_EQ(info.root_motion_kind, 0);

  int32_t bone = -1;
  ASSERT_EQ(radicle_hkx_plain_clip_track_bone(sampled.get(), 0, &bone), RADICLE_STATUS_OK);
  EXPECT_EQ(bone, 0);
  ASSERT_EQ(radicle_hkx_plain_clip_track_bone(sampled.get(), 1, &bone), RADICLE_STATUS_OK);
  EXPECT_EQ(bone, 2);

  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_plain_clip_track_translations(sampled.get(), 1, &values, &count), RADICLE_STATUS_OK);
  expect_near(take_floats(values, count), {100.0f / kU, 0.0f, -1.0f / kU, 101.0f / kU, 0.0f, -1.0f / kU});
  ASSERT_EQ(radicle_hkx_plain_clip_track_scales(sampled.get(), 1, &values, &count), RADICLE_STATUS_OK);
  expect_near(take_floats(values, count), {2, 2, 2, 2, 2, 2});
  ASSERT_EQ(radicle_hkx_plain_clip_track_rotations(sampled.get(), 1, &values, &count), RADICLE_STATUS_OK);
  const auto q = turn();
  expect_near(take_floats(values, count), {0, 0, 0, 1, q[0], q[2], -q[1], q[3]});
}

TEST(CApiHkxPlain, AnExplicitRateResamplesTheClip) {
  const auto skeleton = open(clip_packfile(true));
  const auto clip = open(clip_packfile(false));
  const auto sampled = sample(skeleton, clip, 4.0f);
  ASSERT_TRUE(sampled);
  radicle_hkx_plain_clip_info info{};
  ASSERT_EQ(radicle_hkx_plain_clip_read_info(sampled.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.key_count, 5u);
  EXPECT_FLOAT_EQ(info.rate, 4.0f);
  EXPECT_EQ(info.loop, 0);
  float* values = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_plain_clip_track_translations(sampled.get(), 0, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 15u);
  expect_near(take_floats(values, count),
              {0, 0, 0, 0.25f / kU, 0, 0, 0.5f / kU, 0, 0, 0.75f / kU, 0, 0, 1.0f / kU, 0, 0});
}

TEST(CApiHkxPlain, EventsReachTheCallerOrderedByTime) {
  const auto skeleton = open(clip_packfile(true));
  const auto clip = open(clip_packfile(false));
  const auto sampled = sample(skeleton, clip, 0.0f);
  ASSERT_TRUE(sampled);
  float time = 0.0f;
  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_plain_clip_event_time(sampled.get(), 0, &time), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(time, 0.25f);
  ASSERT_EQ(radicle_hkx_plain_clip_event_name(sampled.get(), 0, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "start");
  ASSERT_EQ(radicle_hkx_plain_clip_event_time(sampled.get(), 1, &time), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(time, 0.75f);
  ASSERT_EQ(radicle_hkx_plain_clip_event_name(sampled.get(), 1, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, size), "end");
  EXPECT_EQ(radicle_hkx_plain_clip_event_time(sampled.get(), 2, &time), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_event_name(sampled.get(), 2, &name, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiHkxPlain, AClipWithoutExtractedMotionHasNoRootMotionTrack) {
  const auto skeleton = open(clip_packfile(true));
  const auto clip = open(clip_packfile(false));
  const auto sampled = sample(skeleton, clip, 0.0f);
  ASSERT_TRUE(sampled);
  float* values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_hkx_plain_clip_root_motion(sampled.get(), &values, &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(values, nullptr);
}

TEST(CApiHkxPlain, BadArgumentsAreInvalidArgumentAndLeaveTheOutputNull) {
  const auto skeleton = open(clip_packfile(true));
  const auto clip = open(clip_packfile(false));
  radicle_hkx_plain_clip* out = reinterpret_cast<radicle_hkx_plain_clip*>(0x1);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), -1.0f, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), 241.0f, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), std::nanf(""), 0, &out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), 0.0f, 2, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), clip.get(), 0.0f, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(nullptr, clip.get(), 0.0f, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_sample(skeleton.get(), nullptr, 0.0f, 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);

  const auto no_skeleton = open(clip_packfile(false));
  EXPECT_EQ(radicle_hkx_plain_clip_sample(no_skeleton.get(), clip.get(), 0.0f, 0, &out), RADICLE_STATUS_NOT_FOUND);

  const auto sampled = sample(skeleton, clip, 0.0f);
  ASSERT_TRUE(sampled);
  float* values = nullptr;
  size_t count = 0;
  int32_t bone = 0;
  EXPECT_EQ(radicle_hkx_plain_clip_track_translations(sampled.get(), 2, &values, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_track_bone(sampled.get(), 2, &bone), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_track_bone(nullptr, 0, &bone), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_read_info(nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_plain_clip_read_info(sampled.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_hkx_plain_clip_destroy(nullptr);
}

TEST(CApiHkxPlain, ClipRolesReadFromABehaviorFileAndResolveUnderTheRootAndDirectory) {
  const auto behavior = open(clip_generator_packfile({{"WalkGen", "Animations\\1HM\\WalkForward.hkt"},
                                                      {"RunGen", "Animations\\2HM\\RunForward.hkt"}},
                                                     {1, 0}, 1.5f));
  ASSERT_TRUE(behavior);
  radicle_hkx_clip_roles* raw = nullptr;
  ASSERT_EQ(radicle_hkx_clip_roles_read(behavior.get(), "Meshes\\Actors\\Character", nullptr, &raw), RADICLE_STATUS_OK);
  RolesHandle all(raw);
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_clip_roles_count(all.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_clip_role_name(all.get(), 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(bytes, size), "walk");
  ASSERT_EQ(radicle_hkx_clip_role_entry(all.get(), 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(bytes, size), "meshes/actors/character/animations/1hm/walkforward.hkx");
  radicle_hkx_clip_role_info info{};
  ASSERT_EQ(radicle_hkx_clip_role_read_info(all.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.loop, 1);
  EXPECT_FLOAT_EQ(info.playback_speed, 1.5f);
  ASSERT_EQ(radicle_hkx_clip_role_read_info(all.get(), 1, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.loop, 0);
  EXPECT_EQ(radicle_hkx_clip_role_entry(all.get(), 2, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_hkx_clip_roles_read(behavior.get(), nullptr, "animations/2hm", &raw), RADICLE_STATUS_OK);
  RolesHandle filtered(raw);
  ASSERT_EQ(radicle_hkx_clip_roles_count(filtered.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_EQ(radicle_hkx_clip_role_name(filtered.get(), 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(bytes, size), "run");
}

TEST(CApiHkxPlain, ClipRolesOnAFileWithoutClipGeneratorsAreNotFound) {
  const auto skeleton = open(clip_packfile(true));
  radicle_hkx_clip_roles* raw = reinterpret_cast<radicle_hkx_clip_roles*>(0x1);
  EXPECT_EQ(radicle_hkx_clip_roles_read(skeleton.get(), nullptr, nullptr, &raw), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(radicle_hkx_clip_roles_read(nullptr, nullptr, nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_clip_roles_read(skeleton.get(), nullptr, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_hkx_clip_roles_destroy(nullptr);
}
