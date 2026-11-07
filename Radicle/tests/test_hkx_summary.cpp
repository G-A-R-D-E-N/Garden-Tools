#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/summary.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

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
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 8);
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
  EXPECT_TRUE(summary.binding_skeletons.empty());
  EXPECT_EQ(summary.object_count, 0U);
}

// Constraint: bytes that are not a packfile must fail the header read, which is the guard every caller runs first.
TEST(HkxSummary, GarbageBytesDoNotDecodeAsAPackfile) {
  const std::vector<std::byte> garbage(64, std::byte{0xAB});
  fo4::hkx::HkxPackfile packfile;
  EXPECT_NE(read_packfile(garbage, packfile), HkxStatus::Ok);
}
