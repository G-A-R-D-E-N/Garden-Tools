#include "fo4/archive/assets.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/summary.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using fo4::hkx::HkxStatus;
using fo4::hkx::HkxSummary;
using fo4::hkx::HkxSummaryClip;
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
  clip.class_name = "hkaSplineCompressedAnimation";
  clip.duration = 1.25f;
  clip.frame_count = 30;
  clip.track_count = 2;
  clip.key_count = 12;
  clip.max_keys_per_track = 7;
  clip.annotation_count = 1;
  summary.clips.push_back(clip);
  summary.binding_skeletons = {"HumanSkeleton"};
  return summary;
}

}  // namespace

TEST(HkxSummary, RendersEverySectionOnePerLine) {
  const auto text = render_summary(sample_summary());
  EXPECT_EQ(text,
            "objects 7\n"
            "skeleton HumanSkeleton bones 2\n"
            "bone 0 Root parent -1\n"
            "bone 1 Spine parent 0\n"
            "clips 1\n"
            "clip hkaSplineCompressedAnimation duration 1.2500 frames 30 tracks 2 keys 12 maxkeys 7 "
            "annotations 1\n"
            "bindings 1\n"
            "binding HumanSkeleton\n");
}

TEST(HkxSummary, AnEmptyPackfileSummarisesToNothing) {
  const fo4::hkx::HkxPackfile packfile;
  const auto summary = read_summary(packfile);
  EXPECT_FALSE(summary.has_skeleton);
  EXPECT_TRUE(summary.bone_names.empty());
  EXPECT_TRUE(summary.clips.empty());
  EXPECT_TRUE(summary.binding_skeletons.empty());
  EXPECT_EQ(summary.object_count, 0U);
}

TEST(HkxSummary, RendersAnEmptySummaryWithoutInventingEntries) {
  const auto text = render_summary(HkxSummary{});
  EXPECT_EQ(text, "objects 0\nskeleton none bones 0\nclips 0\nbindings 0\n");
}

TEST(HkxSummaryReal, VanillaArchiveYieldsAClipOrASkeleton) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t decoded = 0;
  std::size_t summarised = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && decoded < 12; ++i) {
      if (!reader.entries()[i].path.ends_with(".hkx")) continue;
      const auto bytes = reader.extract(i);
      fo4::hkx::HkxPackfile packfile;
      if (fo4::hkx::read_packfile(bytes, packfile) != HkxStatus::Ok) continue;
      ++decoded;
      const auto summary = read_summary(packfile);
      if (summary.has_skeleton || !summary.clips.empty()) ++summarised;
    }
    if (decoded >= 12) break;
  }
  EXPECT_GT(decoded, 0U);
  EXPECT_GT(summarised, 0U);
}
