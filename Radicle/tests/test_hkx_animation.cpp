#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

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
constexpr std::size_t kDataSectionOffset = 0x50 + 3 * 0x40;

std::uint32_t signature_of(const char* class_name) { return fo4::hkx::find_layout(class_name)->signature; }

void patch_float(std::vector<std::byte>& bytes, std::size_t at, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  patch_u32(bytes, at, bits);
}

// Requires room for the pointer, count and capacity of one array member.
void reserve_array(std::vector<std::byte>& bytes, std::size_t field) {
  if (bytes.size() < field + 12) bytes.resize(field + 12, std::byte{0});
}

void set_count(std::vector<std::byte>& bytes, std::size_t field, std::size_t count) {
  reserve_array(bytes, field);
  patch_u32(bytes, field + 8, static_cast<std::uint32_t>(count));
}

void set_float_member(std::vector<std::byte>& bytes, const char* layout, const char* member, float value) {
  patch_float(bytes, member_at(layout, member), value);
}

void set_int_member(std::vector<std::byte>& bytes, const char* layout, const char* member, std::uint32_t value) {
  patch_u32(bytes, member_at(layout, member), value);
}

// Offset of the data section: the packfile builder lays sections out in order.
std::size_t data_section_offset(std::size_t names_size) { return kDataSectionOffset + names_size; }

void pad_to(std::vector<std::byte>& data, std::size_t data_offset, std::size_t to) {
  while ((data_offset + data.size()) % to != 0) data.push_back(std::byte{0});
}

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

// Invariant: one track, two frames, a linear spline on x, a static rotation and an identity scale.
std::vector<std::byte> spline_packfile(int block_offset_override, int mask_size_override) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t blob_at = data.size();
  const std::size_t mask_size = mask_size_override < 0 ? 4 : static_cast<std::size_t>(mask_size_override);
  // Invariant: 8-bit position quantisation, uncompressed rotation, x spline, static rotation.
  data.push_back(std::byte{0x14});
  data.push_back(std::byte{0x10});
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});

  // Invariant: one control interval, degree 1, two control points, knots 0 0 1 1.
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  for (std::uint8_t knot : {0, 0, 1, 1}) data.push_back(std::byte{knot});
  pad_to(data, data_offset, 4);

  put_float(data, 0.0f);
  put_float(data, 10.0f);
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0xFF});
  pad_to(data, data_offset, 4);

  // Invariant: a static identity rotation in the uncompressed format.
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);
  pad_to(data, data_offset, 4);
  const std::size_t blob_size = data.size() - blob_at;

  pad_to(data, data_offset, 4);
  const std::size_t offsets_at = data.size();
  put_u32(data, static_cast<std::uint32_t>(block_offset_override < 0 ? 0 : block_offset_override));

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize",
                 static_cast<std::uint32_t>(mask_size));
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

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

std::vector<std::byte> binding_packfile() {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  link(local, member_at("hkaAnimationBinding", "originalSkeletonName"), append_string(data, "Rig"));
  patch_u32(data, member_at("hkaAnimationBinding", "blendHint"), 1);

  const std::size_t bones_at = data.size();
  for (std::int16_t index : {std::int16_t{0}, std::int16_t{4}, std::int16_t{-1}}) {
    const auto bits = static_cast<std::uint16_t>(index);
    data.push_back(static_cast<std::byte>(bits & 0xFF));
    data.push_back(static_cast<std::byte>(bits >> 8));
  }
  link(local, member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), bones_at);
  set_count(data, member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), 3);

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
}

TEST(HkxAnimation, EveryCutOffLengthOfASplineFileIsRejectedWithoutReadingPastTheEnd) {
  const auto bytes = spline_packfile(-1, -1);
  const std::span<const std::byte> whole(bytes);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    HkxPackfile packfile;
    const auto status = read_packfile(whole.first(length), packfile);
    if (status != HkxStatus::Ok) continue;
    HkxAnimation animation;
    EXPECT_NE(read_animation(packfile, animation), HkxStatus::Ok) << "length " << length;
  }
}

TEST(HkxAnimationBinding, DecodesSkeletonNameTrackMapAndBlendHint) {
  const auto bytes = binding_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimationBinding binding;
  ASSERT_EQ(read_binding(packfile, binding), HkxStatus::Ok);
  EXPECT_EQ(binding.original_skeleton_name, "Rig");
  EXPECT_EQ(binding.transform_track_to_bone_indices, (std::vector<std::int16_t>{0, 4, -1}));
  EXPECT_TRUE(binding.float_track_to_float_slot_indices.empty());
  EXPECT_EQ(binding.blend_hint, 1);
}

TEST(HkxAnimationBinding, ReadBindingReportsMissingObjectWhenNoBindingIsPresent) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", signature_of("hkRootLevelContainer"),
                                            filled(32, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimationBinding binding;
  EXPECT_EQ(read_binding(packfile, binding), HkxStatus::MissingObject);
}
