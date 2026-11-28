#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/animation_edit.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_edit_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::hkx::append_object;
using fo4::hkx::find_layout;
using fo4::hkx::HkxAppend;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_packfile;
using fo4::hkx::verify_append;
using fo4::test::hkx::add_class_name;
using fo4::test::hkx::build_packfile;
using fo4::test::hkx::filled;
using fo4::test::hkx::patch_u32;
using fo4::test::hkx::TestSection;

struct KnownClass {
  std::string name;
  std::uint32_t signature{0};
  std::size_t size{0};
};

constexpr const char* kFixtureClass = "hkaInterleavedUncompressedAnimation";

// Constraint: the fixture's class must be one the generated layout table knows, or the append cannot size it.
KnownClass known_class(const char* name) {
  const auto* layout = find_layout(name);
  if (layout == nullptr) return {name, 0, 0};
  return {name, layout->signature, layout->size};
}

std::vector<std::byte> source_packfile_with_signature(const KnownClass& known, std::uint32_t signature) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature, known.name);
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(known.size, std::byte{7}), {}, {}, {0, 0, name_at}},
  });
}

std::vector<std::byte> source_packfile(const KnownClass& known) {
  return source_packfile_with_signature(known, known.signature);
}

std::vector<std::byte> source_packfile_two(const KnownClass& known) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, known.signature, known.name);
  std::vector<std::byte> data = filled(known.size, std::byte{7});
  while (data.size() % 16 != 0) data.push_back(std::byte{0});
  const auto second_at = static_cast<std::uint32_t>(data.size());
  const auto second = filled(known.size, std::byte{7});
  data.insert(data.end(), second.begin(), second.end());
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), {}, {}, {0, 0, name_at, second_at, 0, name_at}},
  });
}

std::uint16_t u16_at(const std::vector<std::byte>& bytes, std::size_t at) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint32_t>(bytes[at]) |
                                    (std::to_integer<std::uint32_t>(bytes[at + 1]) << 8U));
}

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  return std::to_integer<std::uint32_t>(bytes[at]) | (std::to_integer<std::uint32_t>(bytes[at + 1]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[at + 2]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[at + 3]) << 24U);
}

std::size_t count_name(const std::vector<std::byte>& names, const std::string& name) {
  std::size_t hits = 0;
  for (std::size_t at = 0; at + name.size() <= names.size(); ++at) {
    bool same = true;
    for (std::size_t i = 0; i < name.size() && same; ++i) {
      same = names[at + i] == static_cast<std::byte>(name[i]);
    }
    if (same) ++hits;
  }
  return hits;
}

}  // namespace

// Constraint: the appended object must join the data section without moving or altering any existing object.
TEST(HkxWrite, AppendsAnObjectAndReadsBackWithTheOldOnesUnchanged) {
  const auto known = known_class(kFixtureClass);
  ASSERT_NE(known.signature, 0U) << "the fixture class must exist in the generated layout table";
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);
  const auto instance = filled(known.size, std::byte{0});

  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);

  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  ASSERT_EQ(written.objects.size(), 2U);
  EXPECT_EQ(written.objects[0].class_name, known.name);
  EXPECT_EQ(written.objects[1].class_name, known.name);
  EXPECT_EQ(appended.object_index, 1U);
  EXPECT_EQ(verify_append(source, appended.bytes, instance), HkxStatus::Ok);
}

// Constraint: an unknown class has no size, so the append must refuse instead of guessing one.
TEST(HkxWrite, RefusesAClassTheLayoutTableDoesNotKnow) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);
  HkxAppend appended;
  EXPECT_EQ(append_object(source, "NoSuchHavokClass", 0x1234U, filled(known.size, std::byte{0}), appended),
            HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: the instance must be exactly the layout's size, so a short buffer is refused rather than padded.
TEST(HkxWrite, RefusesAnInstanceOfTheWrongSize) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 1U);
  const auto source = source_packfile(known);
  HkxAppend appended;
  EXPECT_EQ(append_object(source, known.name, known.signature, filled(known.size - 1, std::byte{0}), appended),
            HkxStatus::BadSection);
}

// Constraint: a file with no appended object must not verify as an append, so the check cannot pass by accident.
TEST(HkxWrite, VerificationRejectsAFileThatLostItsObjectCount) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);
  EXPECT_EQ(verify_append(source, source, {}), HkxStatus::MissingObject);
}

// Constraint: bytes that are not a packfile are refused before any layout work starts.
TEST(HkxWrite, RefusesASourceThatIsNotAPackfile) {
  const std::vector<std::byte> garbage(64, std::byte{0xAB});
  HkxAppend appended;
  EXPECT_EQ(append_object(garbage, kFixtureClass, 0, {}, appended), HkxStatus::BadMagic);
}

// Constraint: a class already in the names table is reused, and a different signature cannot hijack its entry.
TEST(HkxWrite, ReusesAnExistingClassNameAndRefusesAMismatchedSignature) {
  const auto known = known_class("hkaAnimation");
  ASSERT_NE(known.signature, 0U) << "hkaAnimation must exist in the generated layout table";
  ASSERT_GT(known.size, 0U);
  ASSERT_EQ(known.name.size() % 2, 0U) << "the reuse case needs an even-length name";
  const auto source = source_packfile(known);

  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, filled(known.size, std::byte{0}), appended),
            HkxStatus::Ok);
  HkxPackfile after;
  ASSERT_EQ(read_packfile(appended.bytes, after), HkxStatus::Ok);
  EXPECT_EQ(count_name(after.sections[after.class_names_section].data, known.name), 1U);

  // Constraint: the source names the class under another signature, so the append must not reuse that entry.
  const auto wrong = source_packfile_with_signature(known, known.signature ^ 0x1U);
  HkxAppend mismatched;
  EXPECT_EQ(append_object(wrong, known.name, known.signature, filled(known.size, std::byte{0}), mismatched),
            HkxStatus::BadClassName);
}

// Constraint: a names section ending in 0xFF filler must still accept a new entry and verify as an append.
TEST(HkxWrite, AppendsANewNameAfterExistingFiller) {
  const auto existing = known_class(kFixtureClass);
  const auto added_class = known_class("hkaSkeleton");
  ASSERT_NE(added_class.signature, 0U) << "hkaSkeleton must exist in the generated layout table";
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, existing.signature, existing.name);
  names.insert(names.end(), 8, std::byte{0xFF});
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(existing.size, std::byte{7}), {}, {}, {0, 0, name_at}},
  });
  const auto instance = filled(added_class.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, added_class.name, added_class.signature, instance, appended), HkxStatus::Ok);
  HkxPackfile after;
  ASSERT_EQ(read_packfile(appended.bytes, after), HkxStatus::Ok);
  EXPECT_EQ(count_name(after.sections[after.class_names_section].data, added_class.name), 1U);
  EXPECT_EQ(verify_append(source, appended.bytes, instance), HkxStatus::Ok);
}

namespace {

std::vector<std::byte> source_with_exports_tail(const KnownClass& known) {
  auto source = source_packfile(known);
  HkxPackfile parsed;
  EXPECT_EQ(read_packfile(source, parsed), HkxStatus::Ok);
  const auto table_at = 0x40U + u16_at(source, 0x3E);
  const auto header = table_at + parsed.data_section * 0x40U;
  const auto mark3 = u32_at(source, header + 0x18U + 12U);
  source.insert(source.end(), 8, std::byte{0});
  const auto set32 = [&source](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) source[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  };
  set32(header + 0x18U + 12U, mark3);
  set32(header + 0x18U + 16U, mark3);
  set32(header + 0x18U + 20U, mark3 + 8U);
  EXPECT_EQ(read_packfile(source, parsed), HkxStatus::Ok);
  return source;
}

}  // namespace

// Constraint: an exports or imports tail cannot be relocated by this writer, so such a source is refused.
TEST(HkxWrite, RefusesASourceWithAnExportsOrImportsTail) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_with_exports_tail(known);
  HkxAppend appended;
  EXPECT_EQ(append_object(source, known.name, known.signature, filled(known.size, std::byte{0}), appended),
            HkxStatus::UnsupportedLayout);
}

// Constraint: corruption in an earlier object must fail too, or only the last object could ever be checked.
TEST(HkxWrite, VerificationRejectsACorruptedEarlierObject) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile_two(known);
  const auto instance = filled(known.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);
  auto corrupted = appended.bytes;
  HkxPackfile parsed;
  ASSERT_EQ(read_packfile(corrupted, parsed), HkxStatus::Ok);
  ASSERT_GE(parsed.objects.size(), 3U);
  const auto table_at = 0x40U + u16_at(corrupted, 0x3E);
  const auto start = u32_at(corrupted, table_at + parsed.data_section * 0x40U + 0x14U);
  const auto at = static_cast<std::size_t>(start) + parsed.objects.front().offset;
  corrupted[at] = static_cast<std::byte>(std::to_integer<unsigned char>(corrupted[at]) ^ 0x01U);
  EXPECT_EQ(verify_append(source, corrupted, instance), HkxStatus::BadSection);
}

// Constraint: a single corrupted byte in a pre-existing object must fail the verification, or it proves nothing.
TEST(HkxWrite, VerificationRejectsACorruptedPreExistingObject) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);
  const auto instance = filled(known.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);

  auto corrupted = appended.bytes;
  HkxPackfile parsed;
  ASSERT_EQ(read_packfile(corrupted, parsed), HkxStatus::Ok);
  const auto table_at = 0x40U + u16_at(corrupted, 0x3E);
  const auto start = u32_at(corrupted, table_at + parsed.data_section * 0x40U + 0x14U);
  const auto at = static_cast<std::size_t>(start) + parsed.objects.front().offset;
  corrupted[at] = std::byte{static_cast<unsigned char>(std::to_integer<unsigned char>(corrupted[at]) ^ 0x01U)};
  EXPECT_NE(verify_append(source, corrupted, instance), HkxStatus::Ok);
}

// Constraint: an appended clip must read back through the decoder the editor uses, sample for sample.
TEST(HkxWrite, AppendsAnInterleavedAnimationAndReadsItBack) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.5f;
  animation.frame_count = 3;
  animation.track_count = 2;
  for (std::size_t sample = 0; sample < 6; ++sample) {
    animation.translations.push_back({static_cast<float>(sample), 0.0f, 1.0f});
    animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.push_back({1.0f, 1.0f, 1.0f});
  }
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::Ok);
  fo4::hkx::HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  EXPECT_EQ(decoded.kind, fo4::hkx::HkxAnimationKind::Interleaved);
  EXPECT_EQ(decoded.frame_count, 3U);
  EXPECT_EQ(decoded.tracks.size(), 2U);
  EXPECT_FLOAT_EQ(decoded.duration, 1.5f);
  EXPECT_FLOAT_EQ(decoded.tracks[1].translations[2][0], 5.0f);
  EXPECT_FLOAT_EQ(decoded.tracks[0].rotations[1][3], 1.0f);
  EXPECT_FLOAT_EQ(decoded.tracks[1].scales[2][2], 1.0f);
  EXPECT_EQ(verify_append(source, appended.bytes), HkxStatus::Ok);
}

// Constraint: a sample count that does not match frame_count * track_count is refused instead of truncated.
TEST(HkxWrite, RefusesAnAnimationWithTheWrongSampleCount) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 2;
  animation.translations.assign(3, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(3, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(3, {1.0f, 1.0f, 1.0f});
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: a non-finite sample cannot be stored, because the reader would hand it to the engine unchanged.
TEST(HkxWrite, RefusesANonFiniteAnimationSample) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 1;
  animation.track_count = 1;
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  animation.translations.push_back({nan, 0.0f, 0.0f});
  animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.push_back({1.0f, 1.0f, 1.0f});
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

TEST(HkxWrite, RefusesAMultiFrameInterleavedAnimationWithoutAPositiveDuration) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  for (const float duration : {0.0f, -1.0f}) {
    fo4::hkx::HkxInterleavedAnimation animation;
    animation.duration = duration;
    animation.frame_count = 2;
    animation.track_count = 1;
    animation.translations.assign(2, {0.0f, 0.0f, 0.0f});
    animation.rotations.assign(2, {0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
    HkxAppend appended;
    EXPECT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::BadSection)
        << "duration " << duration;
    EXPECT_TRUE(appended.bytes.empty());
  }
}

// Constraint: an appended spline clip must read back through the decoder the editor uses, sample for sample.
TEST(HkxWrite, AppendsASplineAnimationAndReadsItBack) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 2.0f;
  animation.frame_count = 5;
  animation.track_count = 2;
  for (std::size_t sample = 0; sample < 10; ++sample) {
    const auto frame = static_cast<float>(sample / 2);
    const auto track = static_cast<float>(sample % 2);
    animation.translations.push_back({frame, track * 0.5f, 2.0f});
    const auto angle = 0.1f * frame;
    animation.rotations.push_back({0.0f, 0.0f, std::sin(angle), std::cos(angle)});
    animation.scales.push_back({1.0f + 0.1f * frame, 1.0f, 1.0f});
  }
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  EXPECT_EQ(decoded.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(decoded.class_name, "hkaSplineCompressedAnimation");
  EXPECT_EQ(decoded.frame_count, 5U);
  EXPECT_EQ(decoded.tracks.size(), 2U);
  EXPECT_FLOAT_EQ(decoded.duration, 2.0f);
  EXPECT_NEAR(decoded.tracks[0].translations[4][0], 4.0f, 0.02f);
  EXPECT_NEAR(decoded.tracks[1].translations[3][1], 0.5f, 0.02f);
  EXPECT_NEAR(decoded.tracks[0].rotations[3][2], std::sin(0.3f), 0.01f);
  EXPECT_NEAR(decoded.tracks[0].rotations[3][3], std::cos(0.3f), 0.01f);
  EXPECT_NEAR(decoded.tracks[1].scales[4][0], 1.4f, 0.02f);
  EXPECT_EQ(verify_append(source, appended.bytes), HkxStatus::Ok);
}

// Constraint: blocks overlap by one frame, so a clip past one block must keep its seam sample exact.
TEST(HkxWrite, AppendsASplineAnimationAcrossBlocksWithoutShiftingTheSeam) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.frame_count = 300;
  animation.track_count = 2;
  animation.duration = 10.0f;
  for (std::size_t frame = 0; frame < animation.frame_count; ++frame) {
    for (std::size_t track = 0; track < animation.track_count; ++track) {
      animation.translations.push_back({static_cast<float>(frame), static_cast<float>(track), 0.0f});
      animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
      animation.scales.push_back({1.0f, 1.0f, 1.0f});
    }
  }
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.frame_count, 300U);
  ASSERT_EQ(decoded.tracks.size(), 2U);
  for (std::size_t frame = 254; frame < 300; ++frame) {
    EXPECT_NEAR(decoded.tracks[1].translations[frame][0], static_cast<float>(frame), 0.02f) << "frame " << frame;
  }
  EXPECT_EQ(verify_append(source, appended.bytes), HkxStatus::Ok);
}

// Constraint: a constant track must encode as a static channel, not as a spline of repeated points.
TEST(HkxWrite, EncodesAStaticTrackAndAnIdentityTrack) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.frame_count = 4;
  animation.track_count = 2;
  animation.duration = 3.0f;
  for (std::size_t frame = 0; frame < animation.frame_count; ++frame) {
    animation.translations.push_back({5.0f, 0.0f, 0.0f});
    animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.push_back({1.0f, 1.0f, 1.0f});
    animation.translations.push_back({0.0f, 0.0f, 0.0f});
    animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.push_back({1.0f, 1.0f, 1.0f});
  }
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.tracks.size(), 2U);
  for (std::size_t frame = 0; frame < 4; ++frame) {
    EXPECT_FLOAT_EQ(decoded.tracks[0].translations[frame][0], 5.0f);
    EXPECT_FLOAT_EQ(decoded.tracks[0].translations[frame][1], 0.0f);
    EXPECT_FLOAT_EQ(decoded.tracks[0].rotations[frame][3], 1.0f);
    EXPECT_FLOAT_EQ(decoded.tracks[1].translations[frame][0], 0.0f);
    EXPECT_FLOAT_EQ(decoded.tracks[1].scales[frame][0], 1.0f);
  }
}

// Constraint: a non-finite sample, a zero frame count and a zero track count are refused before any write.
TEST(HkxWrite, RefusesUnusableSplineInputs) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxSplineOptions options;
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.frame_count = 0;
  animation.track_count = 1;
  // Constraint: the duration is valid here, so the zero frame count is what the encoder refuses.
  animation.duration = 1.0f;
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());

  animation.frame_count = 1;
  animation.track_count = 0;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());

  animation.frame_count = 1;
  animation.track_count = 1;
  animation.duration = 1.0f;
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  animation.translations.assign(1, {nan, 0.0f, 0.0f});
  animation.rotations.assign(1, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(1, {1.0f, 1.0f, 1.0f});
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());

  animation.translations.assign(1, {0.0f, 0.0f, 0.0f});
  options.position_tolerance = -1.0f;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: a spline clip with a sample count that does not match its frames is refused, not truncated.
TEST(HkxWrite, RefusesASplineAnimationWithTheWrongSampleCount) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 2;
  animation.translations.assign(3, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(3, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(3, {1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: Havok cannot store one frame, so a one-frame clip is written as two identical frames.
TEST(HkxWrite, AppendsAOneFrameSplineAnimationAsTwoFrames) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.frame_count = 1;
  animation.track_count = 1;
  animation.duration = 1.0f;
  animation.translations.push_back({2.0f, 3.0f, 4.0f});
  animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.push_back({1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.frame_count, 2U);
  ASSERT_EQ(decoded.tracks.size(), 1U);
  EXPECT_NEAR(decoded.tracks[0].translations[0][0], 2.0f, 0.02f);
  EXPECT_NEAR(decoded.tracks[0].translations[1][0], 2.0f, 0.02f);
  EXPECT_NEAR(decoded.tracks[0].translations[1][2], 4.0f, 0.02f);
  EXPECT_EQ(verify_append(source, appended.bytes), HkxStatus::Ok);
}

// Constraint: a channel whose float span overflows must be refused before any bytes are written.
TEST(HkxWrite, RefusesASplineChannelWhoseRangeOverflowsFloat) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  const auto big = std::numeric_limits<float>::max();
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 1;
  animation.translations = {{-big, 0.0f, 0.0f}, {big, 0.0f, 0.0f}};
  animation.rotations.assign(2, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: a spline channel near the float limit must decode finite, and a constant at FLT_MAX must stay FLT_MAX.
TEST(HkxWrite, AppendsAFiniteSplineChannelNearTheFloatLimit) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  const auto big = std::numeric_limits<float>::max();
  const auto half = big * 0.5f;
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 2;
  animation.translations = {{-half, 0.0f, 0.0f}, {big, 0.0f, 0.0f}, {half, 0.0f, 0.0f}, {big, 0.0f, 0.0f}};
  animation.rotations.assign(4, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(4, {1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.tracks.size(), 2U);
  const float tolerance = big * 1e-6f;
  EXPECT_NEAR(decoded.tracks[0].translations[0][0], -half, tolerance);
  EXPECT_NEAR(decoded.tracks[0].translations[1][0], half, tolerance);
  EXPECT_FLOAT_EQ(decoded.tracks[1].translations[0][0], big);
  EXPECT_FLOAT_EQ(decoded.tracks[1].translations[1][0], big);
}

// Constraint: the sample count is checked before the one-frame duplication, so a short clip is refused.
TEST(HkxWrite, RefusesAOneFrameSplineAnimationWithTooManySamples) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.frame_count = 1;
  animation.track_count = 1;
  animation.duration = 1.0f;
  animation.translations.assign(3, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(3, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(3, {1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection);
  EXPECT_TRUE(appended.bytes.empty());
}

// Constraint: a changed global fixup in a non-data section must fail verification, not pass as an unchanged append.
TEST(HkxWrite, VerificationRejectsAChangedGlobalFixupOutsideTheDataSection) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 16U);
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, known.signature, known.name);
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", filled(16, std::byte{0}), {}, {0, 2, 0, 0xFFFFFFFFU}, {}},
      TestSection{"__data__", filled(known.size, std::byte{7}), {}, {}, {0, 0, name_at}},
  });
  const auto instance = filled(known.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);
  ASSERT_EQ(verify_append(source, appended.bytes, instance), HkxStatus::Ok);

  auto corrupted = appended.bytes;
  const auto table_at = 0x40U + u16_at(corrupted, 0x3E);
  const auto types_header = table_at + 1U * 0x40U;
  const auto start = u32_at(corrupted, types_header + 0x14U);
  const auto global_at = start + u32_at(corrupted, types_header + 0x18U + 4U);
  patch_u32(corrupted, global_at + 8U, 4U);
  EXPECT_EQ(verify_append(source, corrupted, instance), HkxStatus::BadSection);
}

// Constraint: the class-name section fixup tables are copied unchanged, so a changed one must fail verification.
TEST(HkxWrite, VerificationRejectsAChangedClassNameLocalFixup) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, known.signature, known.name);
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {0, 4}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(known.size, std::byte{7}), {}, {}, {0, 0, name_at}},
  });
  const auto instance = filled(known.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);
  ASSERT_EQ(verify_append(source, appended.bytes, instance), HkxStatus::Ok);

  auto corrupted = appended.bytes;
  const auto table_at = 0x40U + u16_at(corrupted, 0x3E);
  const auto start = u32_at(corrupted, table_at + 0x14U);
  const auto local_at = start + u32_at(corrupted, table_at + 0x18U);
  patch_u32(corrupted, local_at + 4U, 5U);
  EXPECT_EQ(verify_append(source, corrupted, instance), HkxStatus::BadSection);
}

// Constraint: the writer copies the header, so a changed user tag must fail even when every section matches.
TEST(HkxWrite, VerificationRejectsAChangedUserTag) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);
  const auto instance = filled(known.size, std::byte{0});
  HkxAppend appended;
  ASSERT_EQ(append_object(source, known.name, known.signature, instance, appended), HkxStatus::Ok);
  ASSERT_EQ(verify_append(source, appended.bytes, instance), HkxStatus::Ok);

  auto corrupted = appended.bytes;
  patch_u32(corrupted, 0x08U, u32_at(corrupted, 0x08U) ^ 1U);
  EXPECT_EQ(verify_append(source, corrupted, instance), HkxStatus::BadSection);
}

// Constraint: a rotation too small to normalise in float must still be written as a finite unit rotation.
TEST(HkxWrite, WritesATinyFiniteInterleavedRotationAsAFiniteUnitRotation) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 1;
  animation.translations.assign(2, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(2, {std::numeric_limits<float>::denorm_min(), 0.0f, 0.0f, 0.0f});
  animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.tracks.size(), 1U);
  for (const auto& rotation : decoded.tracks[0].rotations) {
    for (const float value : rotation) EXPECT_TRUE(std::isfinite(value));
    EXPECT_FLOAT_EQ(rotation[0], 1.0f);
    EXPECT_FLOAT_EQ(rotation[3], 0.0f);
  }
}

// Constraint: a rotation too small to normalise in float must still be written as a finite unit rotation.
TEST(HkxWrite, WritesATinyFiniteSplineRotationAsAFiniteUnitRotation) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 1;
  animation.translations.assign(2, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(2, {std::numeric_limits<float>::denorm_min(), 0.0f, 0.0f, 0.0f});
  animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
  fo4::hkx::HkxSplineOptions options;
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::Ok);
  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), HkxStatus::Ok);
  ASSERT_EQ(decoded.tracks.size(), 1U);
  for (const auto& rotation : decoded.tracks[0].rotations) {
    for (const float value : rotation) EXPECT_TRUE(std::isfinite(value));
    EXPECT_NEAR(rotation[0], 1.0f, 0.01f);
    EXPECT_NEAR(rotation[3], 0.0f, 0.01f);
  }
}

// Constraint: a spline duration whose derived block timing leaves float must be refused, not stored.
TEST(HkxWrite, RefusesASplineDurationWhoseDerivedTimingLeavesFloat) {
  const std::vector<std::byte> names;
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", {}, {}, {}, {}},
  });
  fo4::hkx::HkxSplineOptions options;
  for (const float duration : {1e-42f, 2e38f}) {
    fo4::hkx::HkxInterleavedAnimation animation;
    animation.duration = duration;
    animation.frame_count = 2;
    animation.track_count = 1;
    animation.translations.assign(2, {0.0f, 0.0f, 0.0f});
    animation.rotations.assign(2, {0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
    HkxAppend appended;
    EXPECT_EQ(fo4::hkx::append_spline_animation(source, animation, options, appended), HkxStatus::BadSection)
        << "duration " << duration;
    EXPECT_TRUE(appended.bytes.empty());
  }
}

// Constraint: an appended clip is transform-only and unbound, so an existing binding keeps no animation pointer.
TEST(HkxWrite, AppendedClipLeavesTheExistingBindingUnlinkedAndReportsItsTransformOnlyResult) {
  const auto binding = known_class("hkaAnimationBinding");
  ASSERT_NE(binding.signature, 0U) << "hkaAnimationBinding must exist in the generated layout table";
  const auto clip_class = known_class(kFixtureClass);
  ASSERT_NE(clip_class.signature, 0U);
  std::vector<std::byte> names;
  const auto binding_name = add_class_name(names, binding.signature, binding.name);
  const auto source = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(binding.size, std::byte{0}), {}, {}, {0, 0, binding_name}},
  });
  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 1.0f;
  animation.frame_count = 2;
  animation.track_count = 1;
  animation.translations.assign(2, {0.0f, 0.0f, 0.0f});
  animation.rotations.assign(2, {0.0f, 0.0f, 0.0f, 1.0f});
  animation.scales.assign(2, {1.0f, 1.0f, 1.0f});
  HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_interleaved_animation(source, animation, appended), HkxStatus::Ok);

  HkxPackfile written;
  ASSERT_EQ(read_packfile(appended.bytes, written), HkxStatus::Ok);
  ASSERT_EQ(written.objects.size(), 2U);
  const auto& added = written.objects[appended.object_index];
  EXPECT_EQ(added.class_name, clip_class.name);
  for (const auto& fixup : written.sections[written.data_section].local_fixups) {
    EXPECT_NE(fixup.destination, added.offset);
  }
  fo4::hkx::HkxAnimationBinding linked;
  ASSERT_EQ(fo4::hkx::read_binding(written, linked), HkxStatus::Ok);
  EXPECT_FALSE(linked.animation_offset.has_value());
}

namespace {

using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxClipEdit;
using fo4::hkx::HkxEditReport;
using fo4::hkx::HkxEditStatus;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::HkxSaveOptions;
using fo4::test::hkx::editable_packfile;
using fo4::test::hkx::EditSpec;
using fo4::test::hkx::kEditFrames;

HkxClipEdit open_clip(const std::vector<std::byte>& bytes) {
  HkxClipEdit clip;
  EXPECT_EQ(fo4::hkx::open_clip_edit(bytes, clip), HkxEditStatus::Ok);
  return clip;
}

std::vector<std::byte> save_clip(const std::vector<std::byte>& source, const HkxClipEdit& clip,
                                 const HkxSaveOptions& options = {}, HkxEditReport* report = nullptr) {
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, clip, options, out, report), HkxEditStatus::Ok);
  return out;
}

void expect_near_array(const std::array<float, 3>& a, const std::array<float, 3>& b, float tolerance = 1e-5f) {
  for (std::size_t i = 0; i < 3; ++i) EXPECT_NEAR(a[i], b[i], tolerance);
}

void expect_near_array(const std::array<float, 4>& a, const std::array<float, 4>& b, float tolerance = 1e-5f) {
  for (std::size_t i = 0; i < 4; ++i) EXPECT_NEAR(a[i], b[i], tolerance);
}

void expect_same_clip(const HkxClipEdit& a, const HkxClipEdit& b, float tolerance = 1e-5f) {
  ASSERT_EQ(a.animation.frame_count, b.animation.frame_count);
  ASSERT_EQ(a.animation.tracks.size(), b.animation.tracks.size());
  EXPECT_NEAR(a.animation.duration, b.animation.duration, std::max(tolerance, 1e-5f));
  for (std::size_t t = 0; t < a.animation.tracks.size(); ++t) {
    for (std::size_t f = 0; f < a.animation.frame_count; ++f) {
      expect_near_array(a.animation.tracks[t].translations[f], b.animation.tracks[t].translations[f], tolerance);
      expect_near_array(a.animation.tracks[t].rotations[f], b.animation.tracks[t].rotations[f], tolerance);
      expect_near_array(a.animation.tracks[t].scales[f], b.animation.tracks[t].scales[f], tolerance);
    }
  }
  ASSERT_EQ(a.animation.annotation_tracks.size(), b.animation.annotation_tracks.size());
  for (std::size_t i = 0; i < a.animation.annotation_tracks.size(); ++i) {
    EXPECT_EQ(a.animation.annotation_tracks[i].name, b.animation.annotation_tracks[i].name);
    ASSERT_EQ(a.animation.annotation_tracks[i].annotations.size(), b.animation.annotation_tracks[i].annotations.size());
    for (std::size_t j = 0; j < a.animation.annotation_tracks[i].annotations.size(); ++j) {
      EXPECT_NEAR(a.animation.annotation_tracks[i].annotations[j].time,
                  b.animation.annotation_tracks[i].annotations[j].time, 1e-5f);
      EXPECT_EQ(a.animation.annotation_tracks[i].annotations[j].text, b.animation.annotation_tracks[i].annotations[j].text);
    }
  }
  EXPECT_EQ(a.motion.kind, b.motion.kind);
  EXPECT_EQ(a.motion.up, b.motion.up);
  EXPECT_EQ(a.motion.forward, b.motion.forward);
  EXPECT_EQ(a.motion.duration, b.motion.duration);
  EXPECT_EQ(a.motion.samples, b.motion.samples);
}

// Constraint: track t, frame f is translated (f + 10t, 2f, -f) and turned 0.2f about Z, so every frame differs from the first.
HkxClipEdit ramp_clip(std::size_t frames, std::size_t tracks = 1) {
  HkxClipEdit clip;
  clip.animation.kind = fo4::hkx::HkxAnimationKind::Interleaved;
  clip.animation.frame_count = frames;
  clip.animation.frame_duration = 1.0f / 30.0f;
  clip.animation.duration = static_cast<float>(frames - 1) / 30.0f;
  clip.animation.tracks.resize(tracks);
  for (std::size_t t = 0; t < tracks; ++t) {
    for (std::size_t f = 0; f < frames; ++f) {
      const auto at = static_cast<float>(f);
      clip.animation.tracks[t].translations.push_back({at + 10.0f * static_cast<float>(t), 2.0f * at, -at});
      clip.animation.tracks[t].rotations.push_back(fo4::test::hkx::z_turn(0.2f * at));
      clip.animation.tracks[t].scales.push_back({1.0f, 1.0f, 1.0f});
    }
  }
  return clip;
}

}  // namespace

// Regression: a save must not leave a pointer at the dropped clip or lose annotations, motion or binding maps.
TEST(HkxClipEdit, SaveWithoutEditsReopensWithTheSameTransformsBindingAnnotationsAndMotion) {
  const auto source = editable_packfile();
  const HkxClipEdit before = open_clip(source);
  ASSERT_EQ(before.animation.frame_count, kEditFrames);
  ASSERT_EQ(before.motion.kind, HkxMotionKind::Default);
  const auto written = save_clip(source, before);
  const HkxClipEdit after = open_clip(written);
  expect_same_clip(before, after);

  HkxPackfile original;
  ASSERT_EQ(read_packfile(source, original), HkxStatus::Ok);
  HkxPackfile saved;
  ASSERT_EQ(read_packfile(written, saved), HkxStatus::Ok);
  EXPECT_EQ(saved.objects.size(), original.objects.size());
  std::uint32_t clip_offset = 0;
  for (const auto& object : saved.objects) {
    if (object.class_name == kFixtureClass) clip_offset = object.offset;
  }
  std::size_t pointers_at_clip = 0;
  for (const auto& fixup : saved.sections[saved.data_section].global_fixups) {
    if (fixup.destination == clip_offset) ++pointers_at_clip;
  }
  EXPECT_EQ(pointers_at_clip, 2U);

  HkxAnimationBinding was;
  HkxAnimationBinding now;
  ASSERT_EQ(fo4::hkx::read_binding(original, was), HkxStatus::Ok);
  ASSERT_EQ(fo4::hkx::read_binding(saved, now), HkxStatus::Ok);
  EXPECT_EQ(now.original_skeleton_name, was.original_skeleton_name);
  EXPECT_EQ(now.transform_track_to_bone_indices, was.transform_track_to_bone_indices);
  EXPECT_EQ(now.float_track_to_float_slot_indices, was.float_track_to_float_slot_indices);
  EXPECT_EQ(now.partition_indices, was.partition_indices);
  EXPECT_EQ(now.blend_hint, was.blend_hint);
  EXPECT_EQ(now.animation_offset, std::optional<std::size_t>(clip_offset));
}

// Regression: a trim must rebase root motion and drop or re-time annotations outside the window.
TEST(HkxClipEdit, TrimKeepsTheWindowRebasesMotionAndDropsAnnotationsOutsideIt) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  const HkxClipEdit original = clip;
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 1, 3, &report), HkxEditStatus::Ok);
  EXPECT_EQ(report.annotations_dropped, 2U);
  const HkxClipEdit reopened = open_clip(save_clip(source, clip));
  expect_same_clip(clip, reopened);

  ASSERT_EQ(reopened.animation.frame_count, 3U);
  EXPECT_NEAR(reopened.animation.duration, 2.0f, 1e-6f);
  for (std::size_t t = 0; t < 2; ++t) {
    for (std::size_t f = 0; f < 3; ++f) {
      expect_near_array(reopened.animation.tracks[t].translations[f], original.animation.tracks[t].translations[f + 1]);
    }
  }
  ASSERT_EQ(reopened.animation.annotation_tracks.size(), 1U);
  ASSERT_EQ(reopened.animation.annotation_tracks[0].annotations.size(), 1U);
  EXPECT_EQ(reopened.animation.annotation_tracks[0].annotations[0].text, "b");
  EXPECT_NEAR(reopened.animation.annotation_tracks[0].annotations[0].time, 1.0f, 1e-6f);

  ASSERT_EQ(reopened.motion.samples.size(), 3U);
  EXPECT_NEAR(reopened.motion.duration, 2.0f, 1e-6f);
  EXPECT_NEAR(reopened.motion.samples[0][0], 0.0f, 1e-6f);
  EXPECT_NEAR(reopened.motion.samples[0][3], 0.0f, 1e-6f);
  const float c = std::cos(0.1f);
  const float s = std::sin(0.1f);
  EXPECT_NEAR(reopened.motion.samples[2][0], 2.0f * c + 4.0f * s, 1e-5f);
  EXPECT_NEAR(reopened.motion.samples[2][1], -2.0f * s + 4.0f * c, 1e-5f);
  EXPECT_NEAR(reopened.motion.samples[2][3], 0.2f, 1e-6f);
}

// Regression: a trim outside the clip, reversed or one frame wide must refuse and leave the clip alone.
TEST(HkxClipEdit, TrimRefusalsLeaveTheClipUnchanged) {
  HkxClipEdit clip = open_clip(editable_packfile());
  const HkxClipEdit before = clip;
  EXPECT_EQ(fo4::hkx::trim_clip(clip, 3, 1), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::trim_clip(clip, 2, 2), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::trim_clip(clip, 0, kEditFrames), HkxEditStatus::BadRange);
  expect_same_clip(clip, before);
}

// Regression: a retime must scale annotations and motion by the same factor and resample on the curve.
TEST(HkxClipEdit, RetimeResamplesFramesAnnotationsAndMotionAndSurvivesASave) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::retime_clip(clip, 2.0f, true, {}, &report), HkxEditStatus::Ok);
  EXPECT_TRUE(report.resampled);
  const HkxClipEdit reopened = open_clip(save_clip(source, clip));
  expect_same_clip(clip, reopened);

  ASSERT_EQ(reopened.animation.frame_count, 9U);
  EXPECT_NEAR(reopened.animation.duration, 8.0f, 1e-5f);
  expect_near_array(reopened.animation.tracks[0].translations[1], std::array<float, 3>{5.0f, 0.0f, -0.5f});
  expect_near_array(reopened.animation.tracks[0].translations[8], std::array<float, 3>{40.0f, 0.0f, -4.0f});
  const auto& notes = reopened.animation.annotation_tracks[0].annotations;
  ASSERT_EQ(notes.size(), 3U);
  EXPECT_NEAR(notes[0].time, 0.4f, 1e-5f);
  EXPECT_NEAR(notes[1].time, 4.0f, 1e-5f);
  EXPECT_NEAR(notes[2].time, 7.6f, 1e-5f);
  ASSERT_EQ(reopened.motion.samples.size(), 9U);
  EXPECT_NEAR(reopened.motion.duration, 8.0f, 1e-5f);
  expect_near_array(reopened.motion.samples[1], {0.5f, 1.0f, 0.0f, 0.05f});
}

// Regression: a retime over budget or out of range must refuse and leave the clip alone.
TEST(HkxClipEdit, RetimeOverTheBudgetOrOutOfRangeRefusesAndLeavesTheClipAlone) {
  HkxClipEdit clip = open_clip(editable_packfile());
  ASSERT_EQ(fo4::hkx::set_key(clip, 0, 1, {std::array<float, 3>{100.0f, 0.0f, 0.0f}, std::nullopt, std::nullopt}),
            HkxEditStatus::Ok);
  const HkxClipEdit before = clip;
  fo4::hkx::HkxRetimeBudget budget;
  budget.position = 1.0f;
  EXPECT_EQ(fo4::hkx::retime_clip(clip, 0.5f, true, budget), HkxEditStatus::BudgetExceeded);
  EXPECT_EQ(fo4::hkx::retime_clip(clip, 0.0f, true), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::retime_clip(clip, 101.0f, true), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::retime_clip(clip, std::numeric_limits<float>::quiet_NaN(), false), HkxEditStatus::BadValue);
  expect_same_clip(clip, before);
}

// Regression: a loop must ease the last frames into frame 0 and not blend from the wrong end.
TEST(HkxClipEdit, LoopEasesTheLastFramesIntoTheFirstAndSurvivesASave) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  const HkxClipEdit original = clip;
  ASSERT_EQ(fo4::hkx::loop_clip(clip, 2), HkxEditStatus::Ok);
  const HkxClipEdit reopened = open_clip(save_clip(source, clip));
  expect_same_clip(clip, reopened);
  const auto& track = reopened.animation.tracks[0];
  expect_near_array(track.translations[4], original.animation.tracks[0].translations[0]);
  expect_near_array(track.translations[3], std::array<float, 3>{15.0f, 0.0f, -1.5f});
  expect_near_array(track.translations[2], original.animation.tracks[0].translations[2]);
  expect_near_array(track.rotations[4], original.animation.tracks[0].rotations[0], 1e-4f);

  HkxClipEdit refused = original;
  EXPECT_EQ(fo4::hkx::loop_clip(refused, 0), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::loop_clip(refused, kEditFrames), HkxEditStatus::BadRange);
}

// Regression: a key edit must hit the right frame and track, store a unit rotation and refuse a zero one.
TEST(HkxClipEdit, SetKeyAndCopyPoseSurviveASaveAndRefuseBadKeys) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  const HkxClipEdit original = clip;
  ASSERT_EQ(fo4::hkx::set_key(clip, 1, 2, {std::array<float, 3>{7.0f, 8.0f, 9.0f}, std::array<float, 4>{0.0f, 0.0f, 0.0f, 2.0f}, std::nullopt}),
            HkxEditStatus::Ok);
  ASSERT_EQ(fo4::hkx::copy_pose(clip, 0, 4), HkxEditStatus::Ok);
  const HkxClipEdit reopened = open_clip(save_clip(source, clip));
  expect_near_array(reopened.animation.tracks[1].translations[2], std::array<float, 3>{7.0f, 8.0f, 9.0f});
  expect_near_array(reopened.animation.tracks[1].rotations[2], {0.0f, 0.0f, 0.0f, 1.0f});
  expect_near_array(reopened.animation.tracks[1].scales[2], original.animation.tracks[1].scales[2]);
  expect_near_array(reopened.animation.tracks[0].translations[2], original.animation.tracks[0].translations[2]);
  for (std::size_t t = 0; t < 2; ++t) {
    expect_near_array(reopened.animation.tracks[t].translations[4], original.animation.tracks[t].translations[0]);
    expect_near_array(reopened.animation.tracks[t].rotations[4], original.animation.tracks[t].rotations[0]);
  }

  HkxClipEdit refused = original;
  EXPECT_EQ(fo4::hkx::set_key(refused, 0, 0, {std::nullopt, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}, std::nullopt}),
            HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::set_key(refused, 0, 0, {}), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::set_key(refused, 2, 0, {std::array<float, 3>{1.0f, 1.0f, 1.0f}, std::nullopt, std::nullopt}),
            HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::set_key(refused, 0, kEditFrames, {std::array<float, 3>{1.0f, 1.0f, 1.0f}, std::nullopt, std::nullopt}),
            HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::set_key(refused, 0, 0, {std::array<float, 3>{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, std::nullopt, std::nullopt}),
            HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::copy_pose(refused, 1, 1), HkxEditStatus::BadRange);
  expect_same_clip(refused, original);
}

// Regression: timeline edits must keep frames, annotations and duration in step.
TEST(HkxClipEdit, TimelineInsertDeleteAndMoveKeepFramesAndAnnotationsInStep) {
  EditSpec spec;
  spec.motion = false;
  const auto source = editable_packfile(spec);
  HkxClipEdit clip = open_clip(source);
  const HkxClipEdit original = clip;

  ASSERT_EQ(fo4::hkx::insert_frames(clip, 2, 2), HkxEditStatus::Ok);
  const HkxClipEdit grown = open_clip(save_clip(source, clip));
  ASSERT_EQ(grown.animation.frame_count, 7U);
  EXPECT_NEAR(grown.animation.duration, 6.0f, 1e-5f);
  const auto& old_track = original.animation.tracks[0];
  const auto& grown_track = grown.animation.tracks[0];
  expect_near_array(grown_track.translations[1], old_track.translations[1]);
  expect_near_array(grown_track.translations[2], old_track.translations[1]);
  expect_near_array(grown_track.translations[3], old_track.translations[1]);
  expect_near_array(grown_track.translations[4], old_track.translations[2]);
  expect_near_array(grown_track.translations[6], old_track.translations[4]);
  const auto& grown_notes = grown.animation.annotation_tracks[0].annotations;
  ASSERT_EQ(grown_notes.size(), 3U);
  EXPECT_NEAR(grown_notes[0].time, 0.2f, 1e-5f);
  EXPECT_NEAR(grown_notes[1].time, 4.0f, 1e-5f);
  EXPECT_NEAR(grown_notes[2].time, 5.8f, 1e-5f);

  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::delete_frames(clip, 2, 2, &report), HkxEditStatus::Ok);
  EXPECT_EQ(report.annotations_dropped, 0U);
  expect_same_clip(clip, original, 1e-5f);

  ASSERT_EQ(fo4::hkx::move_frames(clip, 0, 1, 4), HkxEditStatus::Ok);
  const HkxClipEdit moved = open_clip(save_clip(source, clip));
  expect_near_array(moved.animation.tracks[0].translations[4], original.animation.tracks[0].translations[0]);
  expect_near_array(moved.animation.tracks[0].translations[0], original.animation.tracks[0].translations[1]);
  expect_near_array(moved.animation.tracks[1].translations[3], original.animation.tracks[1].translations[4]);

  HkxClipEdit refused = original;
  EXPECT_EQ(fo4::hkx::delete_frames(refused, 0, 4), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::delete_frames(refused, 4, 2), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::insert_frames(refused, kEditFrames + 1, 1), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::insert_frames(refused, 0, 0), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::move_frames(refused, 3, 3, 0), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::move_frames(refused, 1, 1, 1), HkxEditStatus::BadRange);
  expect_same_clip(refused, original);
}

// Regression: a frame-structure edit must refuse while root motion has samples, not keep it stale.
TEST(HkxClipEdit, FrameStructureEditsRefuseWhileDefaultRootMotionHasSamples) {
  HkxClipEdit clip = open_clip(editable_packfile());
  const HkxClipEdit before = clip;
  EXPECT_EQ(fo4::hkx::insert_frames(clip, 1, 1), HkxEditStatus::NotRebuildable);
  EXPECT_EQ(fo4::hkx::delete_frames(clip, 1, 1), HkxEditStatus::NotRebuildable);
  EXPECT_EQ(fo4::hkx::move_frames(clip, 0, 1, 2), HkxEditStatus::NotRebuildable);
  expect_same_clip(clip, before);
}

// Regression: root motion that is not one sample per frame must be cut by fraction on a trim and keep its samples on a retime, never indexed by frame.
TEST(HkxClipEdit, MotionThatIsNotOneSamplePerFrameIsCutByFractionAndKeptByARetime) {
  const auto source = editable_packfile();
  HkxClipEdit two = open_clip(source);
  two.motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 0.0f, 1.0f}};
  HkxClipEdit three = two;
  three.motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 0.0f, 0.0f}, {8.0f, 4.0f, 0.0f, 0.0f}};
  HkxClipEdit kept_rate = two;
  HkxClipEdit same_frames = open_clip(source);

  ASSERT_EQ(fo4::hkx::trim_clip(two, 1, 3), HkxEditStatus::Ok);
  const HkxClipEdit two_saved = open_clip(save_clip(source, two));
  expect_same_clip(two, two_saved);
  ASSERT_EQ(two_saved.motion.samples.size(), 2U);
  EXPECT_NEAR(two_saved.motion.duration, 2.0f, 1e-6f);
  expect_near_array(two_saved.motion.samples[0], {0.0f, 0.0f, 0.0f, 0.0f});
  expect_near_array(two_saved.motion.samples[1], {4.0f * std::cos(0.25f), -4.0f * std::sin(0.25f), 0.0f, 0.5f});

  ASSERT_EQ(fo4::hkx::trim_clip(three, 1, 3), HkxEditStatus::Ok);
  const HkxClipEdit three_saved = open_clip(save_clip(source, three));
  expect_same_clip(three, three_saved);
  ASSERT_EQ(three_saved.motion.samples.size(), 3U);
  expect_near_array(three_saved.motion.samples[0], {0.0f, 0.0f, 0.0f, 0.0f});
  expect_near_array(three_saved.motion.samples[1], {2.0f, 1.0f, 0.0f, 0.0f});
  expect_near_array(three_saved.motion.samples[2], {4.0f, 2.0f, 0.0f, 0.0f});

  const auto before_samples = kept_rate.motion.samples;
  ASSERT_EQ(fo4::hkx::retime_clip(kept_rate, 2.0f, true), HkxEditStatus::Ok);
  EXPECT_EQ(kept_rate.animation.frame_count, 9U);
  EXPECT_EQ(kept_rate.motion.samples, before_samples);
  EXPECT_NEAR(kept_rate.motion.duration, 8.0f, 1e-5f);

  const auto per_frame = same_frames.motion.samples;
  ASSERT_EQ(fo4::hkx::retime_clip(same_frames, 2.0f, false), HkxEditStatus::Ok);
  EXPECT_EQ(same_frames.animation.frame_count, kEditFrames);
  EXPECT_EQ(same_frames.motion.samples, per_frame);
  EXPECT_NEAR(same_frames.animation.duration, 8.0f, 1e-5f);
  EXPECT_NEAR(same_frames.motion.duration, 8.0f, 1e-5f);
}

// Regression: a trim or retime must refuse root motion with a sample that is not finite or is too far, not rebase it into garbage, and a loop must not touch root motion.
TEST(HkxClipEdit, MotionThatIsNotFiniteOrTooFarRefusesTrimAndRetimeAndALoopLeavesMotionAlone) {
  HkxClipEdit not_finite = open_clip(editable_packfile());
  not_finite.motion.samples[2][0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::hkx::trim_clip(not_finite, 1, 3), HkxEditStatus::UnsupportedMotion);
  EXPECT_EQ(fo4::hkx::retime_clip(not_finite, 2.0f, true), HkxEditStatus::UnsupportedMotion);
  EXPECT_EQ(not_finite.animation.frame_count, kEditFrames);

  HkxClipEdit too_far = open_clip(editable_packfile());
  too_far.motion.samples[4][0] = 1000001.0f;
  const HkxClipEdit far_before = too_far;
  EXPECT_EQ(fo4::hkx::trim_clip(too_far, 1, 3), HkxEditStatus::UnsupportedMotion);
  EXPECT_EQ(fo4::hkx::retime_clip(too_far, 2.0f, true), HkxEditStatus::UnsupportedMotion);
  expect_same_clip(too_far, far_before);

  HkxClipEdit looped = open_clip(editable_packfile());
  const HkxClipEdit loop_before = looped;
  ASSERT_EQ(fo4::hkx::loop_clip(looped, 2), HkxEditStatus::Ok);
  EXPECT_EQ(looped.motion.samples, loop_before.motion.samples);
  EXPECT_EQ(looped.motion.duration, loop_before.motion.duration);
  EXPECT_EQ(looped.motion.up, loop_before.motion.up);
  EXPECT_EQ(looped.motion.forward, loop_before.motion.forward);
}

// Regression: annotation edits must keep new tracks and order and refuse times off the clip.
TEST(HkxClipEdit, AnnotationEditsSurviveASaveAndRefuseBadTimesAndText) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  ASSERT_EQ(fo4::hkx::add_annotation(clip, "Spine", 1.5f, "hit"), HkxEditStatus::Ok);
  ASSERT_EQ(fo4::hkx::add_annotation(clip, "Root", 1.0f, "mid"), HkxEditStatus::Ok);
  ASSERT_EQ(fo4::hkx::edit_annotation(clip, 0, 0, 0.5f, "first"), HkxEditStatus::Ok);
  ASSERT_EQ(fo4::hkx::remove_annotation(clip, 0, 3), HkxEditStatus::Ok);
  const HkxClipEdit reopened = open_clip(save_clip(source, clip));
  ASSERT_EQ(reopened.animation.annotation_tracks.size(), 2U);
  EXPECT_EQ(reopened.animation.annotation_tracks[0].name, "Root");
  const auto& root = reopened.animation.annotation_tracks[0].annotations;
  ASSERT_EQ(root.size(), 3U);
  EXPECT_EQ(root[0].text, "first");
  EXPECT_EQ(root[1].text, "mid");
  EXPECT_EQ(root[2].text, "b");
  EXPECT_EQ(reopened.animation.annotation_tracks[1].name, "Spine");
  ASSERT_EQ(reopened.animation.annotation_tracks[1].annotations.size(), 1U);
  EXPECT_EQ(reopened.animation.annotation_tracks[1].annotations[0].text, "hit");
  EXPECT_FLOAT_EQ(reopened.animation.annotation_tracks[1].annotations[0].time, 1.5f);

  EXPECT_EQ(fo4::hkx::add_annotation(clip, "Root", 4.5f, "late"), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::add_annotation(clip, "Root", -0.5f, "early"), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::add_annotation(clip, "Root", std::numeric_limits<float>::quiet_NaN(), "nan"), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::add_annotation(clip, "Root", 1.0f, std::string("a\0b", 3)), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::edit_annotation(clip, 5, 0, 1.0f, "x"), HkxEditStatus::BadRange);
  EXPECT_EQ(fo4::hkx::remove_annotation(clip, 0, 9), HkxEditStatus::BadRange);
  expect_same_clip(clip, reopened);
}

// Regression: a clip the editor cannot rewrite faithfully must be refused, not opened or saved with data dropped.
TEST(HkxClipEdit, UnsupportedSourcesAreRefusedWithNoOutput) {
  HkxClipEdit clip;
  EditSpec float_spec;
  float_spec.float_track = true;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(float_spec), clip), HkxEditStatus::UnsupportedClip);

  EditSpec unlinked;
  unlinked.link_binding = false;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(unlinked), clip), HkxEditStatus::BindingMismatch);

  EditSpec parametric;
  parametric.frame_type = 2;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(parametric), clip), HkxEditStatus::UnsupportedMotion);

  EditSpec other_frame;
  other_frame.motion_class = "hkaAnimatedReferenceFrame";
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(other_frame), clip), HkxEditStatus::UnsupportedMotion);

  EditSpec two_clips;
  two_clips.second_clip = true;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(two_clips), clip), HkxEditStatus::UnsupportedClip);

  EditSpec short_map;
  short_map.short_track_map = true;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(short_map), clip), HkxEditStatus::BindingMismatch);

  EditSpec pointer_in_motion;
  pointer_in_motion.motion_extra_fixup = true;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(pointer_in_motion), clip), HkxEditStatus::UnsupportedMotion);

  EditSpec shared_motion;
  shared_motion.motion_shared = true;
  EXPECT_EQ(fo4::hkx::open_clip_edit(editable_packfile(shared_motion), clip), HkxEditStatus::UnsupportedMotion);

  EXPECT_EQ(fo4::hkx::open_clip_edit(source_packfile(known_class("hkaAnimationBinding")), clip),
            HkxEditStatus::UnsupportedClip);
  EXPECT_EQ(fo4::hkx::open_clip_edit(std::vector<std::byte>(8, std::byte{0}), clip), HkxEditStatus::BadPackfile);

  const auto source = editable_packfile();
  const HkxClipEdit good = open_clip(source);
  std::vector<std::byte> out(3, std::byte{1});
  HkxClipEdit fewer_tracks = good;
  fewer_tracks.animation.tracks.pop_back();
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, fewer_tracks, {}, out), HkxEditStatus::BadValue);
  EXPECT_TRUE(out.empty());
  HkxClipEdit lost_motion = good;
  lost_motion.motion = {};
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, lost_motion, {}, out), HkxEditStatus::BadValue);
  EXPECT_TRUE(out.empty());
  HkxClipEdit bad_sample = good;
  bad_sample.animation.tracks[0].translations[1][0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, bad_sample, {}, out), HkxEditStatus::BadValue);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(fo4::hkx::save_clip_edit(editable_packfile(float_spec), good, {}, out), HkxEditStatus::UnsupportedClip);
  EXPECT_TRUE(out.empty());
}

// Regression: a spline save must keep binding, annotations and motion and report its encode error.
TEST(HkxClipEdit, SplineSaveKeepsBindingAnnotationsAndMotionWithinTheErrorBudget) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  HkxSaveOptions options;
  options.spline = true;
  options.max_position_error = 0.05f;
  options.max_rotation_error = 0.05f;
  options.max_scale_error = 0.05f;
  HkxEditReport report;
  const auto written = save_clip(source, clip, options, &report);
  EXPECT_LE(report.position_error, 0.05f);

  HkxPackfile saved;
  ASSERT_EQ(read_packfile(written, saved), HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(saved, animation), HkxStatus::Ok);
  EXPECT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  const HkxClipEdit reopened = open_clip(written);
  expect_same_clip(clip, reopened, 0.05f);

  HkxSaveOptions strict = options;
  strict.max_position_error = -1.0f;
  std::vector<std::byte> out(3, std::byte{1});
  HkxEditReport measured;
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, clip, strict, out, &measured), HkxEditStatus::BudgetExceeded);
  EXPECT_TRUE(out.empty());
  EXPECT_GE(measured.position_error, 0.0f);
}

// Regression: an annotation edit past the writer limits must be refused as BadValue, never saved or counted as a write failure.
TEST(HkxClipEdit, AnnotationEditsPastTheWriterLimitsAreRefusedAsBadValue) {
  const auto source = editable_packfile();
  const HkxClipEdit good = open_clip(source);
  std::vector<std::byte> out(3, std::byte{1});
  HkxClipEdit long_text = good;
  long_text.animation.annotation_tracks[0].annotations[0].text = std::string(256, 'x');
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, long_text, {}, out), HkxEditStatus::BadValue);
  EXPECT_TRUE(out.empty());
  HkxClipEdit long_name = good;
  long_name.animation.annotation_tracks[0].name = std::string(256, 'n');
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, long_name, {}, out), HkxEditStatus::BadValue);
  HkxClipEdit many_tracks = good;
  many_tracks.animation.annotation_tracks.resize(1025);
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, many_tracks, {}, out), HkxEditStatus::BadValue);
  HkxClipEdit many_events = good;
  many_events.animation.annotation_tracks[0].annotations.resize(65537, good.animation.annotation_tracks[0].annotations[0]);
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, many_events, {}, out), HkxEditStatus::BadValue);
  EXPECT_TRUE(out.empty());
  HkxClipEdit at_limit = good;
  at_limit.animation.annotation_tracks[0].annotations[0].text = std::string(255, 'x');
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, at_limit, {}, out), HkxEditStatus::Ok);
}

// Constraint: detects a loop that leaves a gap at the seam, misreports the worst gap, or edits notes or sampled travel it must leave alone.
TEST(HkxClipEdit, LoopClosesTheSeamReportsTheWorstGapAndLeavesNotesAndSampledMotionAlone) {
  HkxClipEdit clip = ramp_clip(6);
  clip.animation.annotation_tracks.push_back({"Root", {{0.05f, "a"}, {0.1f, "b"}}});
  clip.motion.kind = HkxMotionKind::Default;
  clip.motion.duration = clip.animation.duration;
  clip.motion.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {9.0f, 0.0f, 0.0f, 0.5f}};
  const HkxClipEdit before = clip;
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::loop_clip(clip, 1, &report), HkxEditStatus::Ok);

  const auto& track = clip.animation.tracks[0];
  expect_near_array(track.translations[5], before.animation.tracks[0].translations[0]);
  expect_near_array(track.rotations[5], before.animation.tracks[0].rotations[0], 1e-4f);
  expect_near_array(track.translations[4], before.animation.tracks[0].translations[4]);
  EXPECT_NEAR(report.position_error, std::sqrt(150.0f), 1e-3f);
  EXPECT_NEAR(report.rotation_error, 1.0f, 1e-3f);
  ASSERT_EQ(clip.animation.annotation_tracks.size(), 1U);
  const auto& notes = clip.animation.annotation_tracks[0].annotations;
  ASSERT_EQ(notes.size(), 2U);
  EXPECT_EQ(notes[0].text, "a");
  EXPECT_FLOAT_EQ(notes[0].time, 0.05f);
  EXPECT_EQ(notes[1].text, "b");
  EXPECT_FLOAT_EQ(notes[1].time, 0.1f);
  EXPECT_EQ(clip.motion.samples, before.motion.samples);
  EXPECT_EQ(clip.motion.duration, before.motion.duration);

  HkxClipEdit eased = ramp_clip(8, 2);
  HkxEditReport eased_report;
  ASSERT_EQ(fo4::hkx::loop_clip(eased, 3, &eased_report), HkxEditStatus::Ok);
  const auto& first = eased.animation.tracks[0].translations;
  expect_near_array(first[4], std::array<float, 3>{4.0f, 8.0f, -4.0f});
  expect_near_array(first[5], std::array<float, 3>{5.0f - 5.0f / 3.0f, 20.0f / 3.0f, -5.0f + 5.0f / 3.0f});
  expect_near_array(first[6], std::array<float, 3>{2.0f, 4.0f, -2.0f});
  expect_near_array(first[7], std::array<float, 3>{0.0f, 0.0f, 0.0f});
  expect_near_array(eased.animation.tracks[1].translations[7], std::array<float, 3>{10.0f, 0.0f, 0.0f});
  EXPECT_NEAR(eased_report.position_error, std::sqrt(294.0f), 1e-3f);
}

// Constraint: detects a loop that edits a clip it cannot ease, which would index past a track's last sample.
TEST(HkxClipEdit, LoopRefusesAOneFrameClipAndATrackShorterThanTheClip) {
  HkxClipEdit one = ramp_clip(1);
  EXPECT_EQ(fo4::hkx::loop_clip(one, 1), HkxEditStatus::UnsupportedClip);
  EXPECT_EQ(one.animation.frame_count, 1U);

  HkxClipEdit shorter = ramp_clip(5, 2);
  shorter.animation.tracks[1].translations.pop_back();
  const auto kept = shorter.animation.tracks[0].translations;
  EXPECT_EQ(fo4::hkx::loop_clip(shorter, 2), HkxEditStatus::UnsupportedClip);
  EXPECT_EQ(shorter.animation.tracks[0].translations, kept);
  EXPECT_EQ(shorter.animation.tracks[1].translations.size(), 4U);
}

// Constraint: detects a trim or retime that keeps root motion it cannot decode, or refuses a clip that has none.
TEST(HkxClipEdit, TrimAndRetimeRefuseMotionOfAnUnsupportedKindAndAcceptNoMotion) {
  HkxClipEdit clip = ramp_clip(9);
  clip.motion.kind = HkxMotionKind::Unsupported;
  EXPECT_EQ(fo4::hkx::trim_clip(clip, 2, 6), HkxEditStatus::UnsupportedMotion);
  EXPECT_EQ(fo4::hkx::retime_clip(clip, 2.0f, true), HkxEditStatus::UnsupportedMotion);
  EXPECT_EQ(clip.animation.frame_count, 9U);
  EXPECT_EQ(clip.motion.kind, HkxMotionKind::Unsupported);

  clip.motion.kind = HkxMotionKind::None;
  HkxClipEdit retimed = clip;
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 2, 6), HkxEditStatus::Ok);
  EXPECT_EQ(clip.animation.frame_count, 5U);
  EXPECT_NEAR(clip.animation.duration, 4.0f / 30.0f, 1e-5f);
  EXPECT_EQ(clip.motion.kind, HkxMotionKind::None);
  EXPECT_TRUE(clip.motion.samples.empty());
  ASSERT_EQ(fo4::hkx::retime_clip(retimed, 2.0f, true), HkxEditStatus::Ok);
  EXPECT_EQ(retimed.animation.frame_count, 17U);
  EXPECT_NEAR(retimed.animation.duration, 16.0f / 30.0f, 1e-5f);
  EXPECT_EQ(retimed.motion.kind, HkxMotionKind::None);
}

// Constraint: detects a resample that hides a dropped scale peak from the report or from the budget, because position and rotation stay exact.
TEST(HkxClipEdit, RetimeReportsTheScalePeakItDropsAndRefusesItOverBudget) {
  HkxClipEdit clip = ramp_clip(3);
  for (auto& translation : clip.animation.tracks[0].translations) translation = {0.0f, 0.0f, 0.0f};
  for (auto& rotation : clip.animation.tracks[0].rotations) rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  clip.animation.tracks[0].scales[1] = {10.0f, 1.0f, 1.0f};
  const HkxClipEdit before = clip;

  fo4::hkx::HkxRetimeBudget budget;
  budget.scale = 1.0f;
  EXPECT_EQ(fo4::hkx::retime_clip(clip, 0.5f, true, budget), HkxEditStatus::BudgetExceeded);
  EXPECT_EQ(clip.animation.frame_count, 3U);
  EXPECT_EQ(clip.animation.tracks[0].scales, before.animation.tracks[0].scales);

  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::retime_clip(clip, 0.5f, true, {}, &report), HkxEditStatus::Ok);
  EXPECT_EQ(clip.animation.frame_count, 2U);
  EXPECT_TRUE(report.resampled);
  EXPECT_NEAR(report.position_error, 0.0f, 1e-5f);
  EXPECT_NEAR(report.rotation_error, 0.0f, 1e-3f);
  EXPECT_NEAR(report.scale_error, 9.0f, 1e-4f);
}

// Constraint: detects a trim after a retime that drops the wrong notes, mis-rebases the resampled travel, or saves a clip that reads back different.
TEST(HkxClipEdit, TrimAfterARetimeKeepsTheSameNotesInMemoryAndAfterASaveAndReopen) {
  const auto source = editable_packfile();
  HkxClipEdit clip = open_clip(source);
  ASSERT_EQ(fo4::hkx::retime_clip(clip, 2.0f, true), HkxEditStatus::Ok);
  ASSERT_EQ(clip.animation.frame_count, 9U);
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 2, 6, &report), HkxEditStatus::Ok);
  EXPECT_EQ(report.annotations_dropped, 2U);
  ASSERT_EQ(clip.animation.annotation_tracks.size(), 1U);
  ASSERT_EQ(clip.animation.annotation_tracks[0].annotations.size(), 1U);
  EXPECT_EQ(clip.animation.annotation_tracks[0].annotations[0].text, "b");
  EXPECT_NEAR(clip.animation.annotation_tracks[0].annotations[0].time, 2.0f, 1e-5f);

  const auto written = save_clip(source, clip);
  const HkxClipEdit reopened = open_clip(written);
  expect_same_clip(clip, reopened);
  EXPECT_EQ(reopened.animation.frame_count, 5U);
  EXPECT_NEAR(reopened.animation.duration, 4.0f, 1e-4f);
  ASSERT_EQ(reopened.animation.annotation_tracks.size(), 1U);
  ASSERT_EQ(reopened.animation.annotation_tracks[0].annotations.size(), 1U);
  EXPECT_EQ(reopened.animation.annotation_tracks[0].annotations[0].text, "b");
  EXPECT_NEAR(reopened.animation.annotation_tracks[0].annotations[0].time, 2.0f, 1e-4f);
  expect_near_array(reopened.animation.tracks[0].translations[0], std::array<float, 3>{10.0f, 0.0f, -1.0f});
  expect_near_array(reopened.animation.tracks[0].translations[4], std::array<float, 3>{30.0f, 0.0f, -3.0f});
  expect_near_array(reopened.animation.tracks[1].translations[0], std::array<float, 3>{11.0f, 1.0f, -1.0f});

  const float c = std::cos(0.1f);
  const float s = std::sin(0.1f);
  EXPECT_NEAR(reopened.motion.duration, 4.0f, 1e-4f);
  ASSERT_EQ(reopened.motion.samples.size(), 5U);
  expect_near_array(reopened.motion.samples[0], std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
  expect_near_array(reopened.motion.samples[4], std::array<float, 4>{2.0f * c + 4.0f * s, 4.0f * c - 2.0f * s, 0.0f, 0.2f}, 1e-4f);

  HkxPackfile original;
  ASSERT_EQ(read_packfile(source, original), HkxStatus::Ok);
  HkxPackfile saved;
  ASSERT_EQ(read_packfile(written, saved), HkxStatus::Ok);
  const auto reference_frames = [](const HkxPackfile& packfile) {
    return std::count_if(packfile.objects.begin(), packfile.objects.end(),
                         [](const auto& object) { return object.class_name == "hkaDefaultAnimatedReferenceFrame"; });
  };
  EXPECT_EQ(saved.objects.size(), original.objects.size());
  EXPECT_EQ(reference_frames(saved), 1);
  EXPECT_EQ(reference_frames(original), 1);
}

// Constraint: detects a trim that rotates travel about Z whatever the motion's up axis is, which sends a Y-up clip sideways.
TEST(HkxClipEdit, TrimRebasesTravelAndTurnAboutAnUpAxisThatIsNotZ) {
  HkxClipEdit clip = open_clip(editable_packfile());
  clip.motion.up = {0.0f, 1.0f, 0.0f};
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 1, 3), HkxEditStatus::Ok);
  ASSERT_EQ(clip.motion.samples.size(), 3U);
  const float c = std::cos(0.1f);
  const float s = std::sin(0.1f);
  expect_near_array(clip.motion.samples[0], std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
  expect_near_array(clip.motion.samples[1], std::array<float, 4>{c, 2.0f, s, 0.1f}, 1e-5f);
  expect_near_array(clip.motion.samples[2], std::array<float, 4>{2.0f * c, 4.0f, 2.0f * s, 0.2f}, 1e-5f);
}

// Constraint: detects a trim with no half-step slack or the wrong side of it, which drops a note 0.4 step before the window or keeps one 0.6 step after it.
TEST(HkxClipEdit, TrimKeepsNotesWithinHalfAStepOfTheWindowAndClampsThemToIt) {
  HkxClipEdit clip = ramp_clip(9);
  const float step = clip.animation.duration / 8.0f;
  clip.animation.annotation_tracks.push_back(
      {"Root", {{1.4f * step, "early"}, {1.6f * step, "lead"}, {4.0f * step, "mid"}, {6.4f * step, "tail"}, {6.6f * step, "late"}}});
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 2, 6, &report), HkxEditStatus::Ok);
  EXPECT_EQ(report.annotations_dropped, 2U);
  EXPECT_NEAR(clip.animation.duration, 4.0f * step, 1e-6f);
  const auto& notes = clip.animation.annotation_tracks[0].annotations;
  ASSERT_EQ(notes.size(), 3U);
  EXPECT_EQ(notes[0].text, "lead");
  EXPECT_FLOAT_EQ(notes[0].time, 0.0f);
  EXPECT_EQ(notes[1].text, "mid");
  EXPECT_NEAR(notes[1].time, 2.0f * step, 1e-6f);
  EXPECT_EQ(notes[2].text, "tail");
  EXPECT_NEAR(notes[2].time, 4.0f * step, 1e-6f);
}

// Constraint: detects a save that writes a non-finite scale, rotation or root motion value, which the reader refuses or the game reads as garbage.
TEST(HkxClipEdit, SaveRefusesNonFiniteScaleRotationMotionLaneAndMotionDurationWithNoOutput) {
  const auto source = editable_packfile();
  const HkxClipEdit good = open_clip(source);
  const float not_a_number = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const auto refused = [&](const char* label, const auto& change) {
    HkxClipEdit bad = good;
    change(bad);
    std::vector<std::byte> out(3, std::byte{1});
    EXPECT_EQ(fo4::hkx::save_clip_edit(source, bad, {}, out), HkxEditStatus::BadValue) << label;
    EXPECT_TRUE(out.empty()) << label;
  };
  refused("scale", [&](HkxClipEdit& clip) { clip.animation.tracks[0].scales[1][2] = not_a_number; });
  refused("rotation", [&](HkxClipEdit& clip) { clip.animation.tracks[1].rotations[3][0] = inf; });
  refused("motion lane", [&](HkxClipEdit& clip) { clip.motion.samples[2][3] = not_a_number; });
  refused("motion duration", [&](HkxClipEdit& clip) { clip.motion.duration = not_a_number; });
  EXPECT_FALSE(save_clip(source, good).empty());
}

// Constraint: detects an all-zero rotation key that is saved as identity instead of refused by name with no output.
TEST(HkxClipEdit, AnAllZeroRotationKeyIsRefusedAsZeroRotationWithNoOutput) {
  const auto source = editable_packfile();
  const HkxClipEdit good = open_clip(source);
  EXPECT_FALSE(save_clip(source, good).empty());

  const std::pair<std::size_t, std::size_t> cases[] = {{0, 1}, {1, 3}, {1, 0}};
  for (const auto& [track, frame] : cases) {
    HkxClipEdit bad = good;
    bad.animation.tracks[track].rotations[frame] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<std::byte> out(3, std::byte{1});
    EXPECT_EQ(fo4::hkx::save_clip_edit(source, bad, {}, out), HkxEditStatus::ZeroRotation);
    EXPECT_TRUE(out.empty());
  }
  EXPECT_STREQ(fo4::hkx::hkx_edit_status_name(HkxEditStatus::ZeroRotation), "ZeroRotation");

  HkxClipEdit spline = good;
  spline.animation.tracks[0].rotations[2] = {0.0f, 0.0f, 0.0f, 0.0f};
  HkxSaveOptions options;
  options.spline = true;
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, spline, options, out), HkxEditStatus::ZeroRotation);
  EXPECT_TRUE(out.empty());

  HkxClipEdit tiny = good;
  tiny.animation.tracks[0].rotations[1] = {0.0f, 0.0f, 0.0f, 1e-7f};
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, tiny, {}, out), HkxEditStatus::ZeroRotation);
  tiny.animation.tracks[0].rotations[1] = {0.0f, 0.0f, 0.0f, 1e-3f};
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, tiny, {}, out), HkxEditStatus::Ok);

  HkxClipEdit nan_key = good;
  nan_key.animation.tracks[0].rotations[1] = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
  EXPECT_EQ(fo4::hkx::save_clip_edit(source, nan_key, {}, out), HkxEditStatus::BadValue);
}

// Constraint: detects a verify that compares the edit with itself and so passes bytes that differ from the edit.
TEST(HkxClipEdit, VerifyClipSaveAcceptsTheWrittenBytesAndRejectsAnEditTheyDoNotMatch) {
  const auto source = editable_packfile();
  const HkxClipEdit clip = open_clip(source);
  const auto written = save_clip(source, clip);
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, clip, {}, written), HkxEditStatus::Ok);

  HkxClipEdit rotation = clip;
  rotation.animation.tracks[1].rotations[2] = fo4::test::hkx::z_turn(1.5f);
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, rotation, {}, written), HkxEditStatus::BudgetExceeded);

  HkxClipEdit scale = clip;
  scale.animation.tracks[0].scales[3] = {2.0f, 1.0f, 1.0f};
  HkxEditReport measured;
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, scale, {}, written, &measured), HkxEditStatus::BudgetExceeded);
  EXPECT_GT(measured.scale_error, 0.0f);

  HkxClipEdit translation = clip;
  translation.animation.tracks[0].translations[1][0] += 5.0f;
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, translation, {}, written), HkxEditStatus::BudgetExceeded);

  HkxClipEdit short_track = clip;
  short_track.animation.tracks[1].rotations.pop_back();
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, short_track, {}, written), HkxEditStatus::BadValue);
  short_track = clip;
  short_track.animation.tracks[0].scales.clear();
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, short_track, {}, written), HkxEditStatus::BadValue);
}

// Constraint: detects a verify that skips the written extracted motion, so altered root motion or a changed duration is published.
TEST(HkxClipEdit, VerifyClipSaveRejectsWrittenRootMotionThatDiffersFromTheEdit) {
  const auto source = editable_packfile();
  const HkxClipEdit clip = open_clip(source);
  const auto written = save_clip(source, clip);

  HkxClipEdit sample = clip;
  sample.motion.samples[2][1] += 1.0f;
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, sample, {}, written), HkxEditStatus::WriteFailed);

  HkxClipEdit turn = clip;
  turn.motion.samples[3][3] += 0.5f;
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, turn, {}, written), HkxEditStatus::WriteFailed);

  HkxClipEdit duration = clip;
  duration.motion.duration += 1.0f;
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, duration, {}, written), HkxEditStatus::WriteFailed);
}

// Constraint: detects a verify that accepts a rebuilt clip with fewer frames, fewer tracks or no clip than the edit declares.
TEST(HkxClipEdit, VerifyClipSaveRejectsAShortRebuiltClipAndBytesThatAreNotThePackfile) {
  const auto source = editable_packfile();
  const HkxClipEdit clip = open_clip(source);
  HkxClipEdit trimmed = clip;
  ASSERT_EQ(fo4::hkx::trim_clip(trimmed, 1, 3), HkxEditStatus::Ok);
  const auto short_written = save_clip(source, trimmed);
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, trimmed, {}, short_written), HkxEditStatus::Ok);
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, clip, {}, short_written), HkxEditStatus::WriteFailed);

  HkxClipEdit fewer_tracks = clip;
  fewer_tracks.animation.tracks.pop_back();
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, fewer_tracks, {}, save_clip(source, clip)), HkxEditStatus::WriteFailed);

  const std::vector<std::byte> junk(16, std::byte{7});
  EXPECT_EQ(fo4::hkx::verify_clip_save(source, clip, {}, junk), HkxEditStatus::WriteFailed);
  EXPECT_EQ(fo4::hkx::verify_clip_save(junk, clip, {}, short_written), HkxEditStatus::BadPackfile);
}

// Constraint: detects a duration retime that scales by the wrong ratio, drops the frame rate rule, or accepts a bad length.
TEST(HkxClipEdit, RetimeToDurationSetsTheLengthAndRefusesBadLengths) {
  const auto source = editable_packfile();
  HkxClipEdit played = open_clip(source);
  ASSERT_EQ(fo4::hkx::retime_clip_to_duration(played, 2.0f, false), HkxEditStatus::Ok);
  EXPECT_EQ(played.animation.frame_count, kEditFrames);
  EXPECT_NEAR(played.animation.duration, 2.0f, 1e-5f);
  expect_same_clip(played, open_clip(save_clip(source, played)));

  HkxClipEdit resampled = open_clip(source);
  HkxEditReport report;
  ASSERT_EQ(fo4::hkx::retime_clip_to_duration(resampled, 8.0f, true, {}, &report), HkxEditStatus::Ok);
  EXPECT_TRUE(report.resampled);
  EXPECT_EQ(resampled.animation.frame_count, 9U);
  EXPECT_NEAR(resampled.animation.duration, 8.0f, 1e-5f);

  HkxClipEdit snapped = open_clip(source);
  ASSERT_EQ(fo4::hkx::retime_clip_to_duration(snapped, 2.2f, true), HkxEditStatus::Ok);
  EXPECT_EQ(snapped.animation.frame_count, 3U);
  EXPECT_NEAR(snapped.animation.duration, 2.0f, 1e-5f);

  HkxClipEdit refused = open_clip(source);
  const HkxClipEdit before = refused;
  for (const float seconds : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    EXPECT_EQ(fo4::hkx::retime_clip_to_duration(refused, seconds, false), HkxEditStatus::BadValue);
  }
  EXPECT_EQ(fo4::hkx::retime_clip_to_duration(refused, 400.1f, false), HkxEditStatus::BadValue);
  EXPECT_EQ(fo4::hkx::retime_clip_to_duration(refused, 0.01f, false), HkxEditStatus::BadValue);
  expect_same_clip(refused, before);
}

// Constraint: detects a save that changes the packfile model or drifts on each write, since the rewrite must be a fixed point.
TEST(HkxRepack, SyntheticSourcesRewriteToTheSameModelAndReachAFixedPoint) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  for (const auto& source : {source_packfile(known), source_packfile_two(known)}) {
    const auto report = fo4::hkx::repack_check(source);
    EXPECT_EQ(report.verdict, fo4::hkx::HkxRepackVerdict::Equal) << report.difference;
    EXPECT_EQ(report.status, HkxStatus::Ok);
    EXPECT_EQ(report.source_size, source.size());

    HkxPackfile first;
    ASSERT_EQ(read_packfile(source, first), HkxStatus::Ok);
    std::vector<std::byte> once;
    ASSERT_EQ(fo4::hkx::rewrite_packfile(source, first, once), HkxStatus::Ok);
    HkxPackfile second;
    ASSERT_EQ(read_packfile(once, second), HkxStatus::Ok);
    std::vector<std::byte> twice;
    ASSERT_EQ(fo4::hkx::rewrite_packfile(once, second, twice), HkxStatus::Ok);
    EXPECT_TRUE(twice == once);
  }
}

// Constraint: detects a repack comparison that passes a rewrite which dropped an object, a fixup, a section, or changed section bytes other than the 0xFF filler tail.
TEST(HkxRepack, NamesTheFirstDifferenceOfAMangledRewrite) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile_two(known);
  const auto fresh = [&source] {
    HkxPackfile pack;
    EXPECT_EQ(read_packfile(source, pack), HkxStatus::Ok);
    return pack;
  };
  const HkxPackfile original = fresh();
  ASSERT_EQ(original.objects.size(), 2U);
  ASSERT_FALSE(original.sections[original.data_section].virtual_fixups.empty());
  const auto data = std::to_string(original.data_section);
  const auto difference = [&original](const HkxPackfile& other) {
    return fo4::hkx::first_packfile_difference(original, other);
  };

  EXPECT_EQ(difference(fresh()), "");

  auto dropped_object = fresh();
  dropped_object.objects.pop_back();
  EXPECT_EQ(difference(dropped_object), "object count differs");

  auto moved_object = fresh();
  moved_object.objects[1].offset += 16;
  EXPECT_EQ(difference(moved_object), "object 1 differs");

  auto dropped_fixup = fresh();
  dropped_fixup.sections[dropped_fixup.data_section].virtual_fixups.pop_back();
  EXPECT_EQ(difference(dropped_fixup), "section " + data + " virtual fixups differ");

  auto dropped_section = fresh();
  dropped_section.sections.pop_back();
  EXPECT_EQ(difference(dropped_section), "section count differs");

  auto padded = fresh();
  padded.sections[padded.data_section].data.push_back(std::byte{0xFF});
  EXPECT_EQ(difference(padded), "");

  auto bad_tail = fresh();
  bad_tail.sections[bad_tail.data_section].data.push_back(std::byte{0x00});
  EXPECT_EQ(difference(bad_tail), "section " + data + " data differs");

  auto shortened = fresh();
  shortened.sections[shortened.data_section].data.pop_back();
  EXPECT_EQ(difference(shortened), "section " + data + " data differs");

  auto edited = fresh();
  edited.sections[edited.data_section].data[0] = std::byte{0x55};
  EXPECT_EQ(difference(edited), "section " + data + " data differs");
}

// Constraint: detects a check that reports an unreadable or writer-refused file as a clean pass or as corruption.
TEST(HkxRepack, SeparatesUnreadableFromRefusedSources) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  const auto source = source_packfile(known);

  const auto truncated = fo4::hkx::repack_check(std::span<const std::byte>(source).first(0x20));
  EXPECT_EQ(truncated.verdict, fo4::hkx::HkxRepackVerdict::ReadFailed);
  EXPECT_EQ(truncated.status, HkxStatus::Truncated);

  auto bad_magic = source;
  bad_magic[0] = std::byte{0};
  const auto magic = fo4::hkx::repack_check(bad_magic);
  EXPECT_EQ(magic.verdict, fo4::hkx::HkxRepackVerdict::ReadFailed);
  EXPECT_EQ(magic.status, HkxStatus::BadMagic);

  const auto refused = fo4::hkx::repack_check(source_with_exports_tail(known));
  EXPECT_EQ(refused.verdict, fo4::hkx::HkxRepackVerdict::Refused);
  EXPECT_EQ(refused.status, HkxStatus::UnsupportedLayout);
}

namespace {

using fo4::hkx::HkxRemoval;
using fo4::hkx::remove_objects;
using fo4::test::hkx::BehaviorFixture;

constexpr const char* kRemovalClass = "hkbStateMachineStateInfo";

// Invariant: objects 0 to 4 are parent, a, b, c, d; parent -> a -> b, b lists d in an array, c shares a's name string.
struct RemovalGraph {
  std::vector<std::byte> bytes;
  std::size_t parent{0};
  std::size_t a{0};
  std::size_t b{0};
  std::size_t c{0};
  std::size_t d{0};
  std::size_t name{0};
  std::size_t generator{0};
};

RemovalGraph removal_graph(std::uint32_t signature = 0) {
  BehaviorFixture f;
  if (signature != 0) f.override_signature(kRemovalClass, signature);
  RemovalGraph g;
  g.parent = f.add(kRemovalClass);
  g.a = f.add(kRemovalClass);
  g.b = f.add(kRemovalClass);
  g.c = f.add(kRemovalClass);
  g.d = f.add(kRemovalClass);
  g.name = f.member(kRemovalClass, "name");
  g.generator = f.member(kRemovalClass, "generator");
  const auto state_id = f.member(kRemovalClass, "stateId");
  std::int32_t marker = 1;
  for (const auto object : {g.parent, g.a, g.b, g.c, g.d}) f.set<std::int32_t>(object + state_id, marker++);
  f.point(g.parent + g.generator, g.a);
  f.point(g.a + g.generator, g.b);
  const auto pooled = f.text(g.a + g.name, "A");
  f.share(g.c + g.name, pooled);
  f.text(g.b + g.name, "B");
  f.text(g.d + g.name, "D");
  const auto listeners = f.array(g.b + f.member(kRemovalClass, "listeners"), 1, 8);
  f.point(listeners, g.d);
  g.bytes = f.bytes();
  return g;
}

void unlink_pointer(HkxPackfile& pf, std::size_t source) {
  std::erase_if(pf.sections[pf.data_section].global_fixups,
                [&](const fo4::hkx::HkxRemoteFixup& fixup) { return fixup.source == source; });
}

std::string string_at(const HkxPackfile& pf, std::size_t field) {
  const auto& data = pf.sections[pf.data_section];
  for (const auto& fixup : data.local_fixups) {
    if (fixup.source == field) {
      const auto text = fo4::hkx::cstring_at(data.data, fixup.destination);
      return text ? std::string(*text) : std::string("<unterminated>");
    }
  }
  return "<no pointer>";
}

bool zeroed_object(const HkxPackfile& pf, std::size_t offset) {
  const auto& data = pf.sections[pf.data_section].data;
  const auto size = find_layout(kRemovalClass)->size;
  return std::all_of(data.begin() + static_cast<std::ptrdiff_t>(offset),
                     data.begin() + static_cast<std::ptrdiff_t>(offset + size),
                     [](std::byte value) { return value == std::byte{0}; });
}

}  // namespace

// Constraint: detects a removal that deletes an object another live object still points at, which would leave a dangling pointer in the file.
TEST(HkxRemove, RefusesAnObjectSomethingLiveStillPointsAtAndChangesNothing) {
  const auto g = removal_graph();
  HkxPackfile pf;
  ASSERT_EQ(read_packfile(g.bytes, pf), HkxStatus::Ok);
  const auto before = pf.sections[pf.data_section].data;
  const auto globals = pf.sections[pf.data_section].global_fixups.size();
  const auto locals = pf.sections[pf.data_section].local_fixups.size();
  HkxRemoval removal;
  const std::size_t request[] = {1};
  EXPECT_EQ(remove_objects(pf, request, true, removal), HkxStatus::BadSection);
  EXPECT_NE(removal.refusal.find("still pointed at"), std::string::npos) << removal.refusal;
  EXPECT_TRUE(removal.removed.empty());
  EXPECT_TRUE(pf.sections[pf.data_section].data == before);
  EXPECT_EQ(pf.sections[pf.data_section].global_fixups.size(), globals);
  EXPECT_EQ(pf.sections[pf.data_section].local_fixups.size(), locals);
  EXPECT_EQ(pf.sections[pf.data_section].virtual_fixups.size(), 5U);
}

// Constraint: detects a removal that zeroes a string or array another object still points at, or that drops an object's referrers' pointers instead of its own.
TEST(HkxRemove, RemovesOnlyTheNamedObjectAndKeepsAStringAnotherObjectShares) {
  const auto g = removal_graph();
  HkxPackfile pf;
  ASSERT_EQ(read_packfile(g.bytes, pf), HkxStatus::Ok);
  unlink_pointer(pf, g.parent + g.generator);
  HkxRemoval removal;
  const std::size_t request[] = {1};
  ASSERT_EQ(remove_objects(pf, request, false, removal), HkxStatus::Ok) << removal.refusal;
  EXPECT_EQ(removal.removed, std::vector<std::size_t>{1});
  EXPECT_TRUE(zeroed_object(pf, g.a));
  EXPECT_FALSE(zeroed_object(pf, g.b));

  std::vector<std::byte> out;
  ASSERT_EQ(fo4::hkx::rewrite_packfile(g.bytes, pf, out), HkxStatus::Ok);
  HkxPackfile again;
  ASSERT_EQ(read_packfile(out, again), HkxStatus::Ok);
  EXPECT_EQ(again.objects.size(), 4U);
  EXPECT_EQ(again.sections[again.data_section].global_fixups.size(), 1U);
  EXPECT_EQ(again.sections[again.data_section].local_fixups.size(), 4U);
  EXPECT_EQ(string_at(again, g.c + g.name), "A");
  EXPECT_EQ(string_at(again, g.b + g.name), "B");
  EXPECT_EQ(string_at(again, g.a + g.name), "<no pointer>");
  const auto report = fo4::hkx::repack_check(out);
  EXPECT_EQ(report.verdict, fo4::hkx::HkxRepackVerdict::Equal) << report.difference;
}

// Constraint: detects a cascade that leaves a subtree's arrays, strings and children behind, or one that takes an object another referrer keeps.
TEST(HkxRemove, CascadeTakesWhatOnlyTheRemovedObjectsReachedAndNothingElse) {
  const auto g = removal_graph();
  HkxPackfile pf;
  ASSERT_EQ(read_packfile(g.bytes, pf), HkxStatus::Ok);
  unlink_pointer(pf, g.parent + g.generator);
  HkxRemoval removal;
  const std::size_t request[] = {1};
  ASSERT_EQ(remove_objects(pf, request, true, removal), HkxStatus::Ok) << removal.refusal;
  EXPECT_EQ(removal.removed, (std::vector<std::size_t>{1, 2, 4}));
  EXPECT_TRUE(zeroed_object(pf, g.a));
  EXPECT_TRUE(zeroed_object(pf, g.b));
  EXPECT_TRUE(zeroed_object(pf, g.d));
  EXPECT_FALSE(zeroed_object(pf, g.c));
  EXPECT_FALSE(zeroed_object(pf, g.parent));

  std::vector<std::byte> out;
  ASSERT_EQ(fo4::hkx::rewrite_packfile(g.bytes, pf, out), HkxStatus::Ok);
  HkxPackfile again;
  ASSERT_EQ(read_packfile(out, again), HkxStatus::Ok);
  EXPECT_EQ(again.objects.size(), 2U);
  EXPECT_EQ(again.sections[again.data_section].global_fixups.size(), 0U);
  EXPECT_EQ(again.sections[again.data_section].local_fixups.size(), 1U);
  EXPECT_EQ(string_at(again, g.c + g.name), "A");
  const auto report = fo4::hkx::repack_check(out);
  EXPECT_EQ(report.verdict, fo4::hkx::HkxRepackVerdict::Equal) << report.difference;
}

// Constraint: detects a dead pointer into the middle of a live array being zeroed, and a dead own array being refused.
TEST(HkxRemove, RefusesADeadPointerIntoTheMiddleOfALiveArrayAndTakesADeadOwnArray) {
  const auto make = [](bool inside_live_array) {
    BehaviorFixture f;
    const auto parent = f.add(kRemovalClass);
    const auto dead = f.add(kRemovalClass);
    const auto kept = f.add(kRemovalClass);
    const auto listeners = f.member(kRemovalClass, "listeners");
    const auto slots = f.array(parent + listeners, 3, 8);
    for (std::size_t i = 0; i < 3; ++i) f.point(slots + i * 8, kept);
    if (inside_live_array) {
      f.share_array(dead + listeners, slots + 8, 2);
    } else {
      const auto own = f.array(dead + listeners, 1, 8);
      f.point(own, kept);
    }
    return f.bytes();
  };
  const std::size_t request[] = {1};

  HkxPackfile split;
  ASSERT_EQ(read_packfile(make(true), split), HkxStatus::Ok);
  const auto before = split.sections[split.data_section].data;
  HkxRemoval refused;
  EXPECT_EQ(remove_objects(split, request, true, refused), HkxStatus::BadSection);
  EXPECT_NE(refused.refusal.find("inside an array or string"), std::string::npos) << refused.refusal;
  EXPECT_TRUE(refused.removed.empty());
  EXPECT_TRUE(split.sections[split.data_section].data == before);

  HkxPackfile own;
  ASSERT_EQ(read_packfile(make(false), own), HkxStatus::Ok);
  HkxRemoval taken;
  ASSERT_EQ(remove_objects(own, request, true, taken), HkxStatus::Ok) << taken.refusal;
  EXPECT_EQ(taken.removed, std::vector<std::size_t>{1});
}

// Constraint: detects a dead-pointer index that misses a second pointer into a shared live string or array, or that matches one outside the live region.
TEST(HkxRemove, IndexesEveryDeadPointerAgainstASharedLiveStringAndArray) {
  const auto name = BehaviorFixture{}.member(kRemovalClass, "name");
  const auto listeners = BehaviorFixture{}.member(kRemovalClass, "listeners");
  const std::size_t request[] = {1, 2};

  struct Shared {
    std::vector<std::byte> bytes;
    std::size_t region;
    std::size_t kept;
  };
  const auto strings = [&](bool inside) {
    BehaviorFixture f;
    const std::size_t kept = f.add(kRemovalClass);
    const auto first = f.add(kRemovalClass);
    const auto second = f.add(kRemovalClass);
    const std::size_t pooled = f.text(kept + name, "ABCDEFGHIJKLMNOPQRST");
    if (inside) {
      f.share(first + name, pooled + 8);
      f.share(second + name, pooled + 16);
    } else {
      f.text(first + name, "first");
      f.text(second + name, "second");
    }
    return Shared{f.bytes(), pooled, kept};
  };
  const auto arrays = [&](bool inside) {
    BehaviorFixture f;
    const std::size_t kept = f.add(kRemovalClass);
    const auto first = f.add(kRemovalClass);
    const auto second = f.add(kRemovalClass);
    const std::size_t slots = f.array(kept + listeners, 3, 8);
    for (std::size_t i = 0; i < 3; ++i) f.point(slots + i * 8, kept);
    if (inside) {
      f.share_array(first + listeners, slots + 8, 2);
      f.share_array(second + listeners, slots + 16, 1);
    } else {
      f.array(first + listeners, 1, 8);
      f.array(second + listeners, 1, 8);
    }
    return Shared{f.bytes(), slots, kept};
  };
  const auto expect_refused_at = [&](const Shared& shared, std::size_t offset) {
    HkxPackfile pf;
    ASSERT_EQ(read_packfile(shared.bytes, pf), HkxStatus::Ok);
    const auto before = pf.sections[pf.data_section].data;
    HkxRemoval removal;
    EXPECT_EQ(remove_objects(pf, request, true, removal), HkxStatus::BadSection) << removal.refusal;
    EXPECT_NE(removal.refusal.find("lands at offset " + std::to_string(offset) + ","), std::string::npos)
        << removal.refusal;
    EXPECT_TRUE(removal.removed.empty());
    EXPECT_TRUE(pf.sections[pf.data_section].data == before);
  };

  const Shared shared_string = strings(true);
  expect_refused_at(shared_string, shared_string.region + 8);
  const Shared shared_array = arrays(true);
  expect_refused_at(shared_array, shared_array.region + 8);

  const Shared own_string = strings(false);
  HkxPackfile own;
  ASSERT_EQ(read_packfile(own_string.bytes, own), HkxStatus::Ok);
  HkxRemoval taken;
  ASSERT_EQ(remove_objects(own, request, true, taken), HkxStatus::Ok) << taken.refusal;
  EXPECT_EQ(taken.removed, (std::vector<std::size_t>{1, 2}));
  EXPECT_EQ(string_at(own, own_string.kept + name), "ABCDEFGHIJKLMNOPQRST");

  const Shared own_array = arrays(false);
  HkxPackfile plain;
  ASSERT_EQ(read_packfile(own_array.bytes, plain), HkxStatus::Ok);
  HkxRemoval disjoint;
  ASSERT_EQ(remove_objects(plain, request, true, disjoint), HkxStatus::Ok) << disjoint.refusal;
  EXPECT_EQ(disjoint.removed, (std::vector<std::size_t>{1, 2}));
}

// Constraint: detects a removal that accepts an unsized class, an index outside the list or nothing to remove, any of which would zero bytes of unknown extent.
TEST(HkxRemove, RefusesAnUnsizedClassAnIndexOutsideTheListAndAnEmptyRequest) {
  const auto unsized = removal_graph(0x12345678u);
  HkxPackfile pf;
  ASSERT_EQ(read_packfile(unsized.bytes, pf), HkxStatus::Ok);
  HkxRemoval removal;
  const std::size_t request[] = {1};
  EXPECT_EQ(remove_objects(pf, request, false, removal), HkxStatus::UnsupportedLayout);
  EXPECT_NE(removal.refusal.find(kRemovalClass), std::string::npos) << removal.refusal;

  const auto sized = removal_graph();
  HkxPackfile plain;
  ASSERT_EQ(read_packfile(sized.bytes, plain), HkxStatus::Ok);
  const std::size_t outside[] = {5};
  EXPECT_EQ(remove_objects(plain, outside, false, removal), HkxStatus::MissingObject);
  EXPECT_EQ(remove_objects(plain, std::span<const std::size_t>{}, false, removal), HkxStatus::MissingObject);
  EXPECT_EQ(plain.sections[plain.data_section].virtual_fixups.size(), 5U);
}
