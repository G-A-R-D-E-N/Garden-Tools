#include "hkx_animation_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_animation;
using fo4::hkx::read_packfile;
using namespace fo4::test::hkx;

namespace {

constexpr const char* kClass = "hkaQuantizedAnimation";
constexpr std::size_t kHeaderBytes = 40;
constexpr float kRotationStep = 4.3161006e-05f;
constexpr float kRotationBias = 0.70710677f;

using Rot = std::array<std::uint8_t, 6>;
using Bytes = std::vector<std::uint8_t>;

Rot encode_quaternion(const std::array<float, 4>& q) {
  std::size_t omitted = 0;
  for (std::size_t i = 1; i < 4; ++i) {
    if (std::fabs(q[i]) > std::fabs(q[omitted])) omitted = i;
  }
  std::array<std::uint16_t, 3> words{};
  for (std::size_t i = 0, next = 0; i < 4; ++i) {
    if (i == omitted) continue;
    const float step = std::round((q[i] + kRotationBias) / kRotationStep);
    words[next++] = static_cast<std::uint16_t>(std::clamp(step, 0.0f, 32767.0f));
  }
  words[0] = static_cast<std::uint16_t>(words[0] | ((omitted & 1) << 15));
  words[1] = static_cast<std::uint16_t>(words[1] | (((omitted >> 1) & 1) << 15));
  words[2] = static_cast<std::uint16_t>(words[2] | (q[omitted] < 0.0f ? 0x8000 : 0));
  Rot out{};
  for (std::size_t i = 0; i < 3; ++i) {
    out[2 * i] = static_cast<std::uint8_t>(words[i] & 0xff);
    out[2 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
  }
  return out;
}

struct Scalar {
  std::size_t slot;
  float value;
};

struct Dynamic {
  std::size_t slot;
  float scale;
  float offset;
  std::vector<std::uint16_t> frames;
};

struct StaticRotation {
  std::size_t slot;
  Rot bytes;
};

struct DynamicRotation {
  std::size_t slot;
  std::vector<Rot> frames;
};

struct Spec {
  std::size_t bones{1};
  std::size_t floats{0};
  std::size_t frames{1};
  float duration{1.0f};
  std::vector<Scalar> static_a;
  std::vector<Scalar> static_b;
  std::vector<StaticRotation> static_rotations;
  std::vector<Scalar> static_floats;
  std::vector<Dynamic> dynamic_a;
  std::vector<Dynamic> dynamic_b;
  std::vector<DynamicRotation> dynamic_rotations;
  std::vector<Dynamic> dynamic_floats;
  std::size_t extra_stride{0};
};

void push16(Bytes& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void push_float(Bytes& bytes, float value) {
  std::uint8_t raw[4];
  std::memcpy(raw, &value, sizeof raw);
  bytes.insert(bytes.end(), raw, raw + 4);
}

void pad16(Bytes& bytes) {
  while (bytes.size() % 16 != 0) bytes.push_back(0);
}

void set_word(Bytes& blob, std::size_t index, std::uint16_t value) {
  blob[2 * index] = static_cast<std::uint8_t>(value & 0xff);
  blob[2 * index + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::size_t word(const Bytes& blob, std::size_t index) {
  return static_cast<std::size_t>(blob[2 * index]) | (static_cast<std::size_t>(blob[2 * index + 1]) << 8);
}

Bytes build_blob(const Spec& spec) {
  Bytes blob(kHeaderBytes, 0);
  const std::size_t nr = spec.static_rotations.size();
  const std::size_t nf = spec.static_floats.size();
  const std::size_t ds = spec.dynamic_a.size() + spec.dynamic_b.size();
  const std::size_t dr = spec.dynamic_rotations.size();
  const std::size_t df = spec.dynamic_floats.size();
  const std::size_t stride = 2 * ds + 6 * dr + 2 * df + spec.extra_stride;

  pad16(blob);
  const std::size_t static_indices = blob.size();
  for (const auto* list : {&spec.static_a, &spec.static_b}) {
    for (const Scalar& scalar : *list) push16(blob, static_cast<std::uint16_t>(scalar.slot));
  }
  for (const StaticRotation& rotation : spec.static_rotations) push16(blob, static_cast<std::uint16_t>(rotation.slot / 4));
  for (const Scalar& scalar : spec.static_floats) push16(blob, static_cast<std::uint16_t>(scalar.slot));

  pad16(blob);
  const std::size_t dynamic_indices = blob.size();
  for (const auto* list : {&spec.dynamic_a, &spec.dynamic_b}) {
    for (const Dynamic& scalar : *list) push16(blob, static_cast<std::uint16_t>(scalar.slot));
  }
  for (const DynamicRotation& rotation : spec.dynamic_rotations) {
    push16(blob, static_cast<std::uint16_t>(rotation.slot / 4));
  }
  for (const Dynamic& scalar : spec.dynamic_floats) push16(blob, static_cast<std::uint16_t>(scalar.slot));

  pad16(blob);
  const std::size_t static_values = blob.size();
  for (const auto* list : {&spec.static_a, &spec.static_b}) {
    for (const Scalar& scalar : *list) push_float(blob, scalar.value);
  }
  for (const StaticRotation& rotation : spec.static_rotations) blob.insert(blob.end(), rotation.bytes.begin(), rotation.bytes.end());
  pad16(blob);
  for (const Scalar& scalar : spec.static_floats) push_float(blob, scalar.value);

  pad16(blob);
  const std::size_t offsets_at = blob.size();
  for (const auto* list : {&spec.dynamic_a, &spec.dynamic_b}) {
    for (const Dynamic& scalar : *list) push_float(blob, scalar.offset);
  }
  pad16(blob);
  for (const Dynamic& scalar : spec.dynamic_floats) push_float(blob, scalar.offset);

  pad16(blob);
  const std::size_t scales_at = blob.size();
  for (const auto* list : {&spec.dynamic_a, &spec.dynamic_b}) {
    for (const Dynamic& scalar : *list) push_float(blob, scalar.scale);
  }
  pad16(blob);
  for (const Dynamic& scalar : spec.dynamic_floats) push_float(blob, scalar.scale);

  pad16(blob);
  const std::size_t records_at = blob.size();
  for (std::size_t frame = 0; frame < spec.frames; ++frame) {
    const std::size_t record = blob.size();
    for (const auto* list : {&spec.dynamic_a, &spec.dynamic_b}) {
      for (const Dynamic& scalar : *list) push16(blob, scalar.frames[frame]);
    }
    for (const DynamicRotation& rotation : spec.dynamic_rotations) {
      blob.insert(blob.end(), rotation.frames[frame].begin(), rotation.frames[frame].end());
    }
    for (const Dynamic& scalar : spec.dynamic_floats) push16(blob, scalar.frames[frame]);
    blob.resize(record + stride, 0);
  }

  set_word(blob, 0, static_cast<std::uint16_t>(records_at));
  set_word(blob, 1, static_cast<std::uint16_t>(spec.bones));
  set_word(blob, 2, static_cast<std::uint16_t>(spec.floats));
  set_word(blob, 3, static_cast<std::uint16_t>(spec.frames));
  std::memcpy(blob.data() + 8, &spec.duration, sizeof spec.duration);
  set_word(blob, 6, static_cast<std::uint16_t>(spec.static_a.size()));
  set_word(blob, 7, static_cast<std::uint16_t>(nr));
  set_word(blob, 8, static_cast<std::uint16_t>(spec.static_b.size()));
  set_word(blob, 9, static_cast<std::uint16_t>(nf));
  set_word(blob, 10, static_cast<std::uint16_t>(spec.dynamic_a.size()));
  set_word(blob, 11, static_cast<std::uint16_t>(dr));
  set_word(blob, 12, static_cast<std::uint16_t>(spec.dynamic_b.size()));
  set_word(blob, 13, static_cast<std::uint16_t>(df));
  set_word(blob, 14, static_cast<std::uint16_t>(stride));
  set_word(blob, 15, static_cast<std::uint16_t>(static_indices));
  set_word(blob, 16, static_cast<std::uint16_t>(dynamic_indices));
  set_word(blob, 17, static_cast<std::uint16_t>(static_values));
  set_word(blob, 18, static_cast<std::uint16_t>(offsets_at));
  set_word(blob, 19, static_cast<std::uint16_t>(scales_at));
  return blob;
}

std::vector<std::byte> quantized_packfile(const Bytes& blob, std::size_t bones, std::size_t floats,
                                          std::uint32_t endian = 0) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of(kClass), kClass);
  std::vector<std::byte> data(fo4::hkx::find_layout(kClass)->size, std::byte{0});
  std::vector<std::uint32_t> local;
  while (data.size() % 16 != 0) data.push_back(std::byte{0});
  const std::size_t blob_at = data.size();
  for (std::uint8_t value : blob) data.push_back(static_cast<std::byte>(value));
  if (!blob.empty()) link(local, member_at(kClass, "data"), blob_at);
  set_count(data, member_at(kClass, "data"), blob.size());
  set_int_member(data, kClass, "endian", endian);
  set_int_member(data, "hkaAnimation", "type", 1);
  set_float_member(data, "hkaAnimation", "duration", 2.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(bones));
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", static_cast<std::uint32_t>(floats));
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

HkxStatus decode(const std::vector<std::byte>& bytes, HkxAnimation& animation) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  return read_animation(packfile, animation);
}

std::array<float, 4> z_rotation(std::size_t frame) {
  const float half = static_cast<float>(frame + 1) * 0.15f;
  return {0.0f, 0.0f, std::sin(half), std::cos(half)};
}

Spec rich_spec() {
  Spec spec;
  spec.bones = 2;
  spec.floats = 2;
  spec.frames = 3;
  spec.duration = 2.0f;
  spec.static_a = {{0, 1.0f}, {1, 2.0f}, {2, 3.0f}};
  spec.static_b = {{8, 1.5f}, {9, 1.0f}, {10, 1.0f}};
  spec.static_rotations = {{4, encode_quaternion({0.0f, 0.0f, 0.0f, 1.0f})}};
  spec.static_floats = {{0, 7.5f}};
  for (std::size_t i = 0; i < 6; ++i) {
    Dynamic scalar{i < 3 ? 12 + i : 17 + i, 0.5f, -10.0f + static_cast<float>(i), {}};
    for (std::size_t frame = 0; frame < 3; ++frame) scalar.frames.push_back(static_cast<std::uint16_t>(frame * 10 + i));
    (i < 3 ? spec.dynamic_a : spec.dynamic_b).push_back(scalar);
  }
  DynamicRotation rotation{16, {}};
  for (std::size_t frame = 0; frame < 3; ++frame) rotation.frames.push_back(encode_quaternion(z_rotation(frame)));
  spec.dynamic_rotations.push_back(rotation);
  spec.dynamic_floats = {{1, 0.25f, 1.0f, {100, 101, 102}}};
  return spec;
}

}  // namespace

TEST(HkxAnimationQuantized, DecodesStaticAndDynamicScalarsRotationsAndFloats) {
  // Constraint: detects a static/dynamic mix-up, a swapped scale and offset, or a scatter that misses a bone slot.
  const Spec spec = rich_spec();
  HkxAnimation animation;
  ASSERT_EQ(decode(quantized_packfile(build_blob(spec), 2, 2), animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, HkxAnimationKind::Quantized);
  EXPECT_EQ(animation.class_name, kClass);
  EXPECT_EQ(animation.frame_count, 3u);
  EXPECT_FLOAT_EQ(animation.duration, 2.0f);
  EXPECT_FLOAT_EQ(animation.frame_duration, 1.0f);
  EXPECT_EQ(animation.blocks.count, 1u);
  EXPECT_EQ(animation.blocks.max_frames_per_block, 3u);
  EXPECT_EQ(animation.float_track_count, 2);
  ASSERT_EQ(animation.tracks.size(), 2u);
  ASSERT_EQ(animation.float_tracks.size(), 6u);

  for (std::size_t frame = 0; frame < 3; ++frame) {
    const auto& still = animation.tracks[0];
    EXPECT_TRUE(still.rotation_animated);
    EXPECT_EQ(still.translations[frame], (std::array<float, 3>{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(still.scales[frame], (std::array<float, 3>{1.5f, 1.0f, 1.0f}));
    EXPECT_NEAR(still.rotations[frame][3], 1.0f, 1e-4f);
    EXPECT_NEAR(still.rotations[frame][0], 0.0f, 1e-4f);

    const auto& moving = animation.tracks[1];
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT_FLOAT_EQ(moving.translations[frame][i],
                      static_cast<float>(frame * 10 + i) * 0.5f + (-10.0f + static_cast<float>(i)));
      EXPECT_FLOAT_EQ(moving.scales[frame][i],
                      static_cast<float>(frame * 10 + 3 + i) * 0.5f + (-10.0f + static_cast<float>(3 + i)));
    }
    const auto expected = z_rotation(frame);
    for (std::size_t axis = 0; axis < 4; ++axis) EXPECT_NEAR(moving.rotations[frame][axis], expected[axis], 3e-4f);

    EXPECT_FLOAT_EQ(animation.float_tracks[frame * 2], 7.5f);
    EXPECT_FLOAT_EQ(animation.float_tracks[frame * 2 + 1], static_cast<float>(100 + frame) * 0.25f + 1.0f);
  }
}

TEST(HkxAnimationQuantized, RotationsKeepTheOmittedComponentAndItsSign) {
  // Constraint: detects an omitted-component index decoded from the wrong word bits or a dropped sign bit.
  Spec spec;
  spec.bones = 8;
  std::vector<std::array<float, 4>> expected;
  for (std::size_t bone = 0; bone < 8; ++bone) {
    const std::size_t axis = bone / 2;
    const float sign = (bone % 2 == 0) ? 1.0f : -1.0f;
    std::array<float, 4> q{0.3f, -0.2f, 0.1f, 0.25f};
    q[axis] = 0.9f * sign;
    float length = 0.0f;
    for (float value : q) length += value * value;
    length = std::sqrt(length);
    for (float& value : q) value /= length;
    expected.push_back(q);
    for (std::size_t c = 0; c < 3; ++c) spec.static_a.push_back({bone * 12 + c, 0.0f});
    for (std::size_t c = 0; c < 3; ++c) spec.static_b.push_back({bone * 12 + 8 + c, 1.0f});
    spec.static_rotations.push_back({bone * 12 + 4, encode_quaternion(q)});
  }
  HkxAnimation animation;
  ASSERT_EQ(decode(quantized_packfile(build_blob(spec), 8, 0), animation), HkxStatus::Ok);
  ASSERT_EQ(animation.tracks.size(), 8u);
  EXPECT_EQ(animation.frame_duration, 0.0f);
  for (std::size_t bone = 0; bone < 8; ++bone) {
    for (std::size_t axis = 0; axis < 4; ++axis) {
      EXPECT_NEAR(animation.tracks[bone].rotations[0][axis], expected[bone][axis], 3e-4f);
    }
  }
}

TEST(HkxAnimationQuantized, AnEmptyDataArrayDecodesToNoTracks) {
  HkxAnimation animation;
  ASSERT_EQ(decode(quantized_packfile({}, 0, 0), animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, HkxAnimationKind::Quantized);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimationQuantized, AnEmptyDataArrayThatDeclaresTracksIsBadSection) {
  HkxAnimation animation;
  EXPECT_EQ(decode(quantized_packfile({}, 2, 0), animation), HkxStatus::BadSection);
  EXPECT_EQ(decode(quantized_packfile({}, 0, 2), animation), HkxStatus::BadSection);
}

TEST(HkxAnimationQuantized, ANonZeroEndianMemberIsUnsupported) {
  HkxAnimation animation;
  EXPECT_EQ(decode(quantized_packfile(build_blob(rich_spec()), 2, 2, 1), animation), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(animation.tracks.empty());
}

TEST(HkxAnimationQuantized, ATrackCountThatDisagreesWithTheBlobIsBadSection) {
  HkxAnimation animation;
  EXPECT_EQ(decode(quantized_packfile(build_blob(rich_spec()), 3, 2), animation), HkxStatus::BadSection);
  EXPECT_EQ(decode(quantized_packfile(build_blob(rich_spec()), 2, 1), animation), HkxStatus::BadSection);
}

TEST(HkxAnimationQuantized, ClipsThatLeaveComponentsUnspecifiedAreUnsupported) {
  Spec missing_component = rich_spec();
  missing_component.static_a.pop_back();
  HkxAnimation animation;
  EXPECT_EQ(decode(quantized_packfile(build_blob(missing_component), 2, 2), animation), HkxStatus::UnsupportedLayout);

  Spec missing_float = rich_spec();
  missing_float.floats = 3;
  EXPECT_EQ(decode(quantized_packfile(build_blob(missing_float), 2, 3), animation), HkxStatus::UnsupportedLayout);
}

TEST(HkxAnimationQuantized, AShortHeaderIsTruncated) {
  Bytes blob = build_blob(rich_spec());
  blob.resize(kHeaderBytes - 1);
  HkxAnimation animation;
  EXPECT_EQ(decode(quantized_packfile(blob, 2, 2), animation), HkxStatus::Truncated);
}

TEST(HkxAnimationQuantized, DamagedHeadersAreBadSection) {
  struct Damage {
    const char* name;
    void (*apply)(Bytes&);
  };
  const Damage damages[] = {
      {"zero frames", [](Bytes& blob) { set_word(blob, 3, 0); }},
      {"stride below the used bytes", [](Bytes& blob) { set_word(blob, 14, 0); }},
      {"records past the blob", [](Bytes& blob) { set_word(blob, 0, 0xfff0); }},
      {"overlapping index lists", [](Bytes& blob) { set_word(blob, 16, static_cast<std::uint16_t>(word(blob, 15))); }},
      {"zero duration with several frames",
       [](Bytes& blob) {
         const float zero = 0.0f;
         std::memcpy(blob.data() + 8, &zero, sizeof zero);
       }},
      {"non-finite duration",
       [](Bytes& blob) {
         const float bad = std::numeric_limits<float>::quiet_NaN();
         std::memcpy(blob.data() + 8, &bad, sizeof bad);
       }},
      {"non-finite static scalar",
       [](Bytes& blob) {
         const float bad = std::numeric_limits<float>::infinity();
         std::memcpy(blob.data() + word(blob, 17), &bad, sizeof bad);
       }},
      {"duplicate scatter slot",
       [](Bytes& blob) {
         const std::size_t at = word(blob, 15);
         blob[at + 2] = blob[at];
         blob[at + 3] = blob[at + 1];
       }},
      {"scatter slot past the bone slots",
       [](Bytes& blob) {
         const std::size_t at = word(blob, 15);
         blob[at] = 0xff;
         blob[at + 1] = 0xff;
       }},
      {"rotation off component four",
       [](Bytes& blob) {
         const std::size_t at = word(blob, 15) + 2 * 6;
         blob[at] = 2;
         blob[at + 1] = 0;
       }},
      {"quaternion beyond unit length",
       [](Bytes& blob) {
         const std::size_t at = word(blob, 17) + 4 * 6;
         for (std::size_t i = 0; i < 3; ++i) {
           blob[at + 2 * i] = 0xff;
           blob[at + 2 * i + 1] = 0x7f;
         }
       }},
  };
  for (const Damage& damage : damages) {
    SCOPED_TRACE(damage.name);
    Bytes blob = build_blob(rich_spec());
    damage.apply(blob);
    HkxAnimation animation;
    EXPECT_EQ(decode(quantized_packfile(blob, 2, 2), animation), HkxStatus::BadSection);
    EXPECT_TRUE(animation.tracks.empty());
  }
}
