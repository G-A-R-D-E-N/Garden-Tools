#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/summary.hpp"

#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace fo4::test::hkx;

namespace {

using fo4::hkx::HkxAnimationKind;
using fo4::hkx::HkxStatus;
using fo4::hkx::HkxSummary;
using fo4::hkx::HkxSummaryClip;
using fo4::hkx::read_packfile;
using fo4::hkx::read_summary;
using fo4::hkx::render_summary;

HkxSummary sample_summary() {
  HkxSummary summary;
  summary.object_count = 7;
  summary.has_skeleton = true;
  summary.skeleton_name = "HumanSkeleton";
  summary.bone_names = {"Root", "Spine"};
  summary.parent_indices = {-1, 0};
  HkxSummaryClip clip;
  clip.kind = HkxAnimationKind::Spline;
  clip.class_name = "hkaSplineCompressedAnimation";
  clip.duration = 1.25f;
  clip.frame_count = 30;
  clip.track_count = 2;
  clip.annotation_count = 1;
  summary.clips.push_back(clip);
  summary.binding_skeletons = {"HumanSkeleton"};
  return summary;
}

}  // namespace

// Constraint: the renderer is the editor's only view, so every section must stay one entry per line.
TEST(HkxSummary, RendersEverySectionOnePerLine) {
  EXPECT_EQ(render_summary(sample_summary()),
            "objects 7\n"
            "skeleton HumanSkeleton bones 2\n"
            "bone 0 Root parent -1\n"
            "bone 1 Spine parent 0\n"
            "clips 1\n"
            "clip hkaSplineCompressedAnimation kind spline duration 1.2500 frames 30 tracks 2 annotations 1\n"
            "undecoded 0\n"
            "bindings 1\n"
            "binding HumanSkeleton\n");
}

// Constraint: a stored name is a raw cstring, so a newline in it must not add a line to the summary.
TEST(HkxSummary, EscapesControlBytesSoANameCannotForgeALine) {
  auto summary = sample_summary();
  summary.bone_names[1] = "Spine\nbone 9 Fake parent -1";
  const auto text = render_summary(summary);
  EXPECT_EQ(text.find("\nbone 9 Fake"), std::string::npos);
  EXPECT_NE(text.find("bone 1 Spine?bone 9 Fake parent -1 parent 0"), std::string::npos);
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 9);
}

// Constraint: an empty stored name renders a placeholder, so the line shapes never depend on the name.
TEST(HkxSummary, RendersAPlaceholderForEmptyNames) {
  HkxSummary summary;
  summary.has_skeleton = true;
  HkxSummaryClip clip;
  clip.kind = HkxAnimationKind::Interleaved;
  summary.clips.push_back(clip);
  summary.binding_skeletons = {""};
  EXPECT_EQ(render_summary(summary),
            "objects 0\n"
            "skeleton (unnamed) bones 0\n"
            "clips 1\n"
            "clip (unnamed) kind interleaved duration 0.0000 frames 0 tracks 0 annotations 0\n"
            "undecoded 0\n"
            "bindings 1\n"
            "binding (unnamed)\n");
}

// Constraint: an empty packfile must summarise to empty sections instead of inventing a skeleton or a clip.
TEST(HkxSummary, AnEmptyPackfileSummarisesToNothing) {
  const fo4::hkx::HkxPackfile packfile;
  const auto summary = read_summary(packfile);
  EXPECT_FALSE(summary.has_skeleton);
  EXPECT_TRUE(summary.bone_names.empty());
  EXPECT_TRUE(summary.clips.empty());
  EXPECT_TRUE(summary.undecoded.empty());
  EXPECT_TRUE(summary.binding_skeletons.empty());
  EXPECT_EQ(summary.object_count, 0U);
}

// Constraint: the one kind spelling must cover every enumerator, so a lossless clip cannot report as a view.
TEST(HkxSummary, NamesEveryAnimationKind) {
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::Interleaved), "interleaved");
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::Spline), "spline");
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::Lossless), "lossless");
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::Predictive), "predictive");
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::Quantized), "quantized");
  EXPECT_STREQ(fo4::hkx::hkx_animation_kind_name(HkxAnimationKind::View), "view");
}

// Constraint: two animation objects make read_animation report UnsupportedLayout, so the summary must name them.
TEST(HkxSummary, ReportsTwoAnimationObjectsAsUndecodedInsteadOfNoClips) {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                      "hkaInterleavedUncompressedAnimation");
  const std::size_t size = fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size;
  const auto second_at = static_cast<std::uint32_t>(size);
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(2 * size, std::byte{0}), {}, {}, {0, 0, name_at, second_at, 0, name_at}},
  });
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  const auto summary = read_summary(packfile);
  EXPECT_TRUE(summary.clips.empty());
  ASSERT_EQ(summary.undecoded.size(), 1U);
  EXPECT_EQ(summary.undecoded[0].class_name, "hkaInterleavedUncompressedAnimation");
  EXPECT_EQ(summary.undecoded[0].reason, "more than one animation object: 2");
  EXPECT_NE(render_summary(summary).find("undecoded hkaInterleavedUncompressedAnimation more than one animation object: 2"),
            std::string::npos);
}

// Constraint: a supported clip whose channel runs past its array is BadSection, which the summary must report.
TEST(HkxSummary, ReportsAMalformedClipWithItsStatusInsteadOfNoClips) {
  const std::uint64_t dynamic_offset_two = std::uint64_t{2} | (std::uint64_t{2} << 2);
  const auto bytes = lossless_shape_packfile(3, 1, 3, 1, 0, 0, dynamic_offset_two);
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  const auto summary = read_summary(packfile);
  EXPECT_TRUE(summary.clips.empty());
  ASSERT_EQ(summary.undecoded.size(), 1U);
  EXPECT_EQ(summary.undecoded[0].class_name, "hkaLosslessCompressedAnimation");
  EXPECT_EQ(summary.undecoded[0].reason, "bad-section");
}

// Constraint: a refused predictive layout must reach the summary with the case it declined, not only as unsupported-layout.
// Constraint: detects a refusal list that names the class but not which unresolved layout to close, so the decode backlog cannot be ordered.
TEST(HkxSummary, NamesTheRefusedCaseOfAPredictiveClip) {
  const char* const predictive = "hkaPredictiveCompressedAnimation";
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of(predictive), predictive);
  std::vector<std::byte> data = filled(fo4::hkx::find_layout(predictive)->size, std::byte{0});
  set_int_member(data, predictive, "numFrames", 1);
  set_int_member(data, predictive, "numFloatSlots", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 1);
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), {}, {}, {0, 0, name_at}},
  });
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  const auto summary = read_summary(packfile);
  EXPECT_TRUE(summary.clips.empty());
  ASSERT_EQ(summary.undecoded.size(), 1U);
  EXPECT_EQ(summary.undecoded[0].class_name, predictive);
  EXPECT_EQ(summary.undecoded[0].reason, "unsupported-layout");
  EXPECT_EQ(summary.undecoded[0].detail, "float-track and float/bone boundary sampling");
  EXPECT_NE(render_summary(summary).find(
                "undecoded hkaPredictiveCompressedAnimation unsupported-layout float-track and float/bone boundary sampling\n"),
            std::string::npos);
}

// Constraint: a float-only lossless clip must summarise its declared frames and kind, not read as an empty file.
TEST(HkxSummary, SummarisesAFloatOnlyLosslessClipWithItsFrames) {
  const auto bytes = lossless_shape_packfile(4, 0, 0, 0, 2, 8);
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  const auto summary = read_summary(packfile);
  ASSERT_EQ(summary.clips.size(), 1U);
  EXPECT_EQ(summary.clips[0].kind, HkxAnimationKind::Lossless);
  EXPECT_EQ(summary.clips[0].frame_count, 4U);
  EXPECT_TRUE(summary.undecoded.empty());
  EXPECT_NE(render_summary(summary).find("kind lossless duration 2.0000 frames 4 tracks 0"), std::string::npos);
}

// Constraint: bytes that are not a packfile must fail the header read, which is the guard every caller runs first.
TEST(HkxSummary, GarbageBytesDoNotDecodeAsAPackfile) {
  const std::vector<std::byte> garbage(64, std::byte{0xAB});
  fo4::hkx::HkxPackfile packfile;
  EXPECT_NE(read_packfile(garbage, packfile), HkxStatus::Ok);
}
