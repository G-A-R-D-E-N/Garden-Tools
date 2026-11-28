#include "hkx_animation_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
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

constexpr const char* kClass = "hkaPredictiveCompressedAnimation";
constexpr std::size_t kLaneComponents = 10;
constexpr std::array<std::uint16_t, kLaneComponents> kLaneSlots{0, 1, 2, 4, 5, 6, 7, 8, 9, 10};

enum class Fault {
  None,
  Width,
  Float,
  Boundary,
  Static,
  Renorm,
  Mixed,
  Reference,
  Truncated,
  Padding,
};

using Sampler = std::function<int(std::size_t lane, std::size_t frame)>;

struct Spec {
  std::size_t frames{1};
  std::size_t bones{1};
  bool fixed_range{false};
  Fault fault{Fault::None};
  Sampler sampler{};
  std::int64_t declared_tracks{-1};
};

Spec make_spec(std::size_t frames, std::size_t bones, bool fixed_range, Fault fault = Fault::None) {
  Spec spec;
  spec.frames = frames;
  spec.bones = bones;
  spec.fixed_range = fixed_range;
  spec.fault = fault;
  return spec;
}

using Block = std::array<std::array<int, 16>, 16>;

struct Bits {
  std::array<std::uint8_t, 17> bytes{};

  void put(std::size_t at, std::uint32_t value, std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
      if (((value >> k) & 1u) != 0) bytes[(at + k) / 8] = static_cast<std::uint8_t>(bytes[(at + k) / 8] | (1u << ((at + k) % 8)));
    }
  }
};

bool fits(int value, std::size_t width) {
  if (width == 0) return value == 0;
  const int limit = 1 << (width - 1);
  return value >= -limit && value < limit;
}

std::vector<std::uint8_t> encode_fields(const Block& fields) {
  std::vector<std::uint8_t> bytes(16, 0);
  for (std::size_t track = 0; track < 16; ++track) {
    std::uint8_t descriptor = 0;
    for (std::size_t half = 0; half < 2; ++half) {
      Bits bits;
      std::size_t position = 0;
      for (std::size_t pair = 0; pair < 4; ++pair) {
        const int a = fields[track][half * 8 + pair * 2];
        const int b = fields[track][half * 8 + pair * 2 + 1];
        std::size_t width = 0;
        while (width < 14 && !(fits(a, width) && fits(b, width))) ++width;
        const std::uint32_t mask = (1u << width) - 1;
        bits.put(position, static_cast<std::uint32_t>(width), 4);
        bits.put(position + 4, static_cast<std::uint32_t>(a) & mask, width);
        bits.put(position + 4 + width, static_cast<std::uint32_t>(b) & mask, width);
        position += 4 + 2 * width;
      }
      const std::size_t count = (position + 7) / 8;
      descriptor = static_cast<std::uint8_t>(descriptor | ((count - 1) << (half == 0 ? 4 : 0)));
      for (std::size_t i = count; i-- > 0;) bytes.push_back(bits.bytes[i]);
    }
    bytes[track] = descriptor;
  }
  return bytes;
}

std::vector<std::uint8_t> encode_samples(const Block& samples) {
  Block fields{};
  for (std::size_t track = 0; track < 16; ++track) {
    std::array<std::int16_t, 16> differences{};
    for (std::size_t s = 0; s < 16; ++s) differences[s] = static_cast<std::int16_t>(samples[track][s] * 4);
    for (int pass = 0; pass < 3; ++pass) {
      for (std::size_t s = 15; s > 0; --s) {
        differences[s] = static_cast<std::int16_t>(differences[s] - differences[s - 1]);
      }
    }
    for (std::size_t s = 0; s < 16; ++s) fields[track][s] = differences[s] >> 2;
  }
  return encode_fields(fields);
}

int default_sample(std::size_t lane, std::size_t frame, bool fixed_range) {
  const std::size_t component = lane % kLaneComponents;
  if (component == 0) return static_cast<int>(frame + lane / kLaneComponents * 100);
  if (component >= 6) return fixed_range ? 8191 : 4;
  return 0;
}

float fixed_scale() {
  const std::uint32_t bits = 0x39000400u;
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& source) {
  std::vector<std::byte> out(source.size());
  std::memcpy(out.data(), source.data(), source.size());
  return out;
}

void align_data(std::vector<std::byte>& data) {
  while (data.size() % 16 != 0) data.push_back(std::byte{0});
}

std::vector<std::byte> predictive_packfile(const Spec& spec) {
  const std::size_t frames = spec.frames;
  const std::size_t bones = spec.bones;
  const std::size_t blocks = (frames - 1) / 15 + 1;
  const std::size_t count = kLaneComponents * bones;
  const std::size_t groups = (count + 15) / 16;
  Sampler sampler = spec.sampler;
  if (!sampler) {
    sampler = [&](std::size_t lane, std::size_t frame) { return default_sample(lane, frame, spec.fixed_range); };
  }

  std::vector<std::uint8_t> compressed;
  std::vector<std::uint16_t> ints;
  for (std::size_t block = 0; block < blocks; ++block) {
    for (std::size_t group = 0; group < groups; ++group) {
      Block samples{};
      for (std::size_t lane = 0; lane < 16; ++lane) {
        for (std::size_t sample = 0; sample < 16; ++sample) {
          const std::size_t frame = std::min(frames - 1, block * 15 + sample);
          samples[lane][sample] = sampler(group * 16 + lane, frame);
        }
      }
      std::vector<std::uint8_t> bytes = encode_samples(samples);
      if (spec.fault == Fault::Width && block == 0 && group == 0) {
        const std::size_t first_end = 16 + (bytes[0] >> 4) + 1;
        bytes[first_end - 1] = static_cast<std::uint8_t>(bytes[first_end - 1] | 15);
      }
      if (spec.fault == Fault::Padding && block == 0 && group == 0) {
        std::size_t start = 16;
        for (std::size_t lane = 0; lane < 10; ++lane) start += (bytes[lane] >> 4) + 1 + (bytes[lane] & 15) + 1;
        const std::size_t first_end = start + (bytes[10] >> 4) + 1;
        bytes[first_end - 1] = static_cast<std::uint8_t>(bytes[first_end - 1] | 15);
      }
      compressed.insert(compressed.end(), bytes.begin(), bytes.end());
    }
    ints.push_back(static_cast<std::uint16_t>(compressed.size() & 0xffff));
    ints.push_back(static_cast<std::uint16_t>(compressed.size() >> 16));
  }

  std::array<std::uint32_t, 9> offsets{};
  offsets[1] = offsets[2] = static_cast<std::uint32_t>(ints.size());
  for (std::size_t i = 0; i < groups; ++i) ints.push_back(0xffff);
  offsets[3] = static_cast<std::uint32_t>(ints.size());
  for (std::size_t i = 0; i < groups; ++i) {
    ints.push_back(static_cast<std::uint16_t>(spec.fixed_range ? 0xffff : static_cast<std::uint16_t>(spec.fault == Fault::Mixed ? 1 : 0)));
  }
  offsets[4] = static_cast<std::uint32_t>(ints.size());
  for (std::size_t bone = 0; bone < bones; ++bone) {
    for (std::uint16_t slot : kLaneSlots) ints.push_back(static_cast<std::uint16_t>(bone * 12 + slot));
  }
  offsets[5] = static_cast<std::uint32_t>(ints.size());
  if (spec.fault == Fault::Boundary) ints.push_back(0);
  offsets[6] = static_cast<std::uint32_t>(ints.size());
  if (spec.fault == Fault::Static) ints.push_back(0);
  offsets[7] = offsets[8] = static_cast<std::uint32_t>(ints.size());
  if (spec.fault == Fault::Renorm) ints.push_back(0);
  if (spec.fault == Fault::Reference) ints[offsets[4] + 9] = 11;
  if (spec.fault == Fault::Truncated) compressed.pop_back();

  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of(kClass), kClass);
  std::vector<std::byte> data(fo4::hkx::find_layout(kClass)->size, std::byte{0});
  std::vector<std::uint32_t> local;

  align_data(data);
  const std::size_t compressed_at = data.size();
  const std::vector<std::byte> compressed_bytes = to_bytes(compressed);
  data.insert(data.end(), compressed_bytes.begin(), compressed_bytes.end());
  align_data(data);
  const std::size_t ints_at = data.size();
  for (std::uint16_t value : ints) {
    data.push_back(static_cast<std::byte>(value & 0xff));
    data.push_back(static_cast<std::byte>(value >> 8));
  }
  align_data(data);
  const std::size_t floats_at = data.size();
  const std::size_t float_count = spec.fixed_range ? 0 : count * 2;
  for (std::size_t i = 0; i < float_count; ++i) put_float(data, i < count ? 0.25f : 0.0f);

  link(local, member_at(kClass, "compressedData"), compressed_at);
  set_count(data, member_at(kClass, "compressedData"), compressed.size());
  link(local, member_at(kClass, "intData"), ints_at);
  set_count(data, member_at(kClass, "intData"), ints.size());
  if (float_count > 0) link(local, member_at(kClass, "floatData"), floats_at);
  set_count(data, member_at(kClass, "floatData"), float_count);

  for (std::size_t i = 0; i < offsets.size(); ++i) {
    patch_u32(data, member_at(kClass, "intArrayOffsets") + 4 * i, offsets[i]);
  }
  patch_u32(data, member_at(kClass, "floatArrayOffsets") + 8, spec.fixed_range ? 0 : static_cast<std::uint32_t>(count));
  const std::uint32_t float_slots = spec.fault == Fault::Float ? 1 : 0;
  set_int_member(data, kClass, "numBones", static_cast<std::uint32_t>(bones));
  set_int_member(data, kClass, "numFloatSlots", float_slots);
  set_int_member(data, kClass, "numFrames", static_cast<std::uint32_t>(frames));
  set_float_member(data, "hkaAnimation", "duration", static_cast<float>(std::max<std::size_t>(1, frames - 1)));
  const auto declared_tracks = spec.declared_tracks < 0 ? bones : static_cast<std::size_t>(spec.declared_tracks);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(declared_tracks));
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", float_slots);

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

float expected_lane(int quantised, bool fixed_range) {
  const float scale = fixed_range ? fixed_scale() : 0.25f;
  return static_cast<float>(quantised) * scale;
}

}  // namespace

TEST(HkxAnimationPredictive, DecodesRampsAcrossBlockBoundaries) {
  // Constraint: detects a block stride of 16 instead of 15, which would shift every frame after the first block.
  struct Shape {
    std::size_t frames;
    std::size_t bones;
    bool fixed;
  };
  for (const Shape shape : {Shape{1, 1, false}, Shape{15, 1, false}, Shape{16, 1, false}, Shape{17, 2, false},
                            Shape{31, 2, false}, Shape{32, 1, true}}) {
    SCOPED_TRACE(shape.frames);
    HkxAnimation animation;
    ASSERT_EQ(decode(predictive_packfile(make_spec(shape.frames, shape.bones, shape.fixed)), animation), HkxStatus::Ok);
    EXPECT_EQ(animation.kind, HkxAnimationKind::Predictive);
    EXPECT_EQ(animation.class_name, kClass);
    EXPECT_EQ(animation.frame_count, shape.frames);
    EXPECT_EQ(animation.blocks.count, (shape.frames - 1) / 15 + 1);
    EXPECT_EQ(animation.float_track_count, 0);
    ASSERT_EQ(animation.tracks.size(), shape.bones);
    for (std::size_t bone = 0; bone < shape.bones; ++bone) {
      const auto& track = animation.tracks[bone];
      ASSERT_EQ(track.translations.size(), shape.frames);
      EXPECT_TRUE(track.rotation_animated);
      for (std::size_t frame = 0; frame < shape.frames; ++frame) {
        EXPECT_FLOAT_EQ(track.translations[frame][0],
                        expected_lane(static_cast<int>(frame + bone * 100), shape.fixed));
        EXPECT_FLOAT_EQ(track.rotations[frame][3], expected_lane(shape.fixed ? 8191 : 4, shape.fixed));
        EXPECT_FLOAT_EQ(track.scales[frame][0], expected_lane(shape.fixed ? 8191 : 4, shape.fixed));
      }
    }
  }
}

TEST(HkxAnimationPredictive, TimingFollowsTheDeclaredDuration) {
  HkxAnimation animation;
  ASSERT_EQ(decode(predictive_packfile(make_spec(31, 1, false)), animation), HkxStatus::Ok);
  EXPECT_FLOAT_EQ(animation.duration, 30.0f);
  EXPECT_FLOAT_EQ(animation.frame_duration, 1.0f);
  EXPECT_FLOAT_EQ(animation.blocks.block_duration, 15.0f);
  EXPECT_EQ(animation.blocks.max_frames_per_block, 16u);
}

TEST(HkxAnimationPredictive, ReconstructsWrappingThirdDifferencesOnEveryLane) {
  // Constraint: detects an inverse difference that does not wrap at int16 or that scatters lanes to the wrong slots.
  std::uint32_t state = 683;
  const auto next = [&state]() {
    state = state * 1664525u + 1013904223u;
    return static_cast<int>((state >> 8) % 16384u) - 8192;
  };
  constexpr std::size_t kFrames = 15;
  std::vector<std::array<int, kFrames>> values(kLaneComponents);
  for (auto& lane : values) {
    for (int& value : lane) value = next();
  }
  Spec spec = make_spec(kFrames, 1, false);
  spec.sampler = [&](std::size_t lane, std::size_t frame) { return lane < kLaneComponents ? values[lane][frame] : 0; };
  HkxAnimation animation;
  ASSERT_EQ(decode(predictive_packfile(spec), animation), HkxStatus::Ok);
  ASSERT_EQ(animation.tracks.size(), 1u);
  const auto& track = animation.tracks[0];
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      EXPECT_FLOAT_EQ(track.translations[frame][axis], expected_lane(values[axis][frame], false));
      EXPECT_FLOAT_EQ(track.scales[frame][axis], expected_lane(values[7 + axis][frame], false));
    }
    for (std::size_t axis = 0; axis < 4; ++axis) {
      EXPECT_FLOAT_EQ(track.rotations[frame][axis], expected_lane(values[3 + axis][frame], false));
    }
  }
}

TEST(HkxAnimationPredictive, AResidualWidthOfFifteenIsUnsupported) {
  HkxAnimation animation;
  EXPECT_EQ(decode(predictive_packfile(make_spec(31, 1, false, Fault::Width)), animation), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(animation.tracks.empty());
  // Constraint: detects a refusal that reaches the caller without the case that was declined.
  EXPECT_EQ(animation.refusal.class_name, kClass);
  EXPECT_EQ(animation.refusal.feature, "a residual width above the supported 0..14 arithmetic");
}

TEST(HkxAnimationPredictive, UnresolvedLayoutsAreUnsupportedAndNameTheRefusedCase) {
  // Constraint: detects a refused clip listed as "unsupported" only, so nobody can tell which unresolved layout to close first.
  const std::array<std::pair<Fault, const char*>, 6> cases{{
      {Fault::Float, "float-track and float/bone boundary sampling"},
      {Fault::Boundary, "float-track and float/bone boundary sampling"},
      {Fault::Static, "static track index walks"},
      {Fault::Renorm, "quaternion renormalization index consumption"},
      {Fault::Mixed, "mixed fixed/ranged scale-array cursor association"},
      {Fault::Reference, "reference/default pose reconstruction"},
  }};
  for (const auto& [fault, feature] : cases) {
    SCOPED_TRACE(static_cast<int>(fault));
    HkxAnimation animation;
    EXPECT_EQ(decode(predictive_packfile(make_spec(31, 1, false, fault)), animation), HkxStatus::UnsupportedLayout);
    EXPECT_TRUE(animation.tracks.empty());
    EXPECT_EQ(animation.refusal.class_name, kClass);
    EXPECT_EQ(animation.refusal.feature, feature);
  }
}

TEST(HkxAnimationPredictive, TruncatedCompressedDataIsMalformedNotUnsupported) {
  HkxAnimation animation;
  EXPECT_EQ(decode(predictive_packfile(make_spec(31, 1, false, Fault::Truncated)), animation), HkxStatus::BadSection);
  // Constraint: detects a BadSection clip listed as an unresolved layout, which would send a corrupt file to the decode backlog.
  EXPECT_TRUE(animation.refusal.class_name.empty());
  EXPECT_TRUE(animation.refusal.feature.empty());
}

TEST(HkxAnimationPredictive, ASignatureWithoutAVerifiedLayoutIsRefusedAndNamed) {
  // Constraint: detects a known animation class with a foreign signature read as the verified layout, or refused with no class named.
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of(kClass) ^ 1u, kClass);
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(fo4::hkx::find_layout(kClass)->size, std::byte{0}), {}, {}, {0, 0, name_at}},
  });
  HkxAnimation animation;
  EXPECT_EQ(decode(bytes, animation), HkxStatus::UnsupportedLayout);
  EXPECT_EQ(animation.refusal.class_name, kClass);
  EXPECT_EQ(animation.refusal.feature, "no verified layout for this class signature");
}

TEST(HkxAnimationPredictive, JunkInAPaddingLaneDoesNotRefuseTheFile) {
  // Constraint: detects a decoder that reads the lanes past the dynamic count instead of skipping them.
  HkxAnimation animation;
  ASSERT_EQ(decode(predictive_packfile(make_spec(31, 1, false, Fault::Padding)), animation), HkxStatus::Ok);
  EXPECT_EQ(animation.frame_count, 31u);
}

TEST(HkxAnimationPredictive, AnEmptyClipDecodesToNoTracks) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of(kClass), kClass);
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(fo4::hkx::find_layout(kClass)->size, std::byte{0}), {}, {}, {0, 0, name_at}},
  });
  HkxAnimation animation;
  ASSERT_EQ(decode(bytes, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, HkxAnimationKind::Predictive);
  EXPECT_TRUE(animation.tracks.empty());
  EXPECT_EQ(animation.frame_count, 0u);
}

TEST(HkxAnimationPredictive, ABaseTrackCountThatDisagreesWithTheCodecCountIsBadSection) {
  HkxAnimation animation;
  Spec spec = make_spec(16, 1, false);
  spec.declared_tracks = 2;
  EXPECT_EQ(decode(predictive_packfile(spec), animation), HkxStatus::BadSection);
}

TEST(HkxAnimationPredictive, ASecondSupportedClipInTheFileIsStillRefused) {
  std::vector<std::byte> names;
  const auto first = add_class_name(names, signature_of(kClass), kClass);
  const auto second = add_class_name(names, signature_of("hkaQuantizedAnimation"), "hkaQuantizedAnimation");
  std::vector<std::byte> data = filled(fo4::hkx::find_layout(kClass)->size, std::byte{0});
  const std::size_t quantized_at = data.size();
  const auto quantized = filled(fo4::hkx::find_layout("hkaQuantizedAnimation")->size, std::byte{0});
  data.insert(data.end(), quantized.begin(), quantized.end());
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), {}, {}, {0, 0, first, static_cast<std::uint32_t>(quantized_at), 0, second}},
  });
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimation animation;
  EXPECT_EQ(read_animation(packfile, animation), HkxStatus::UnsupportedLayout);
}
