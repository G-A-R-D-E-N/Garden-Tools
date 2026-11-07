#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// Constraint: an exports or imports tail cannot be relocated by this writer, so such a source is refused.
TEST(HkxWrite, RefusesASourceWithAnExportsOrImportsTail) {
  const auto known = known_class(kFixtureClass);
  ASSERT_GT(known.size, 0U);
  auto source = source_packfile(known);
  HkxPackfile parsed;
  ASSERT_EQ(read_packfile(source, parsed), HkxStatus::Ok);
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
  ASSERT_EQ(read_packfile(source, parsed), HkxStatus::Ok);
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
