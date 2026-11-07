#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/summary.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_animation_manifest.hpp"
#include "hkx_seam_ratio.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Requires the manifest to compare equal whatever line endings a checkout produced.
std::string without_carriage_returns(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

}  // namespace

TEST(HkxSkeletonRealData, ReadsVanillaHumanSkeletonFromAnimationsArchive) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }

  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(packfile, skeleton), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(skeleton.bone_names.size(), 95u);
  EXPECT_EQ(skeleton.parent_indices.size(), skeleton.bone_names.size());
  EXPECT_EQ(skeleton.reference_pose.size(), skeleton.bone_names.size());
  ASSERT_FALSE(skeleton.bone_names.empty());
  EXPECT_EQ(skeleton.bone_names.front(), "Root");
  EXPECT_EQ(skeleton.parent_indices.front(), -1);
  for (std::size_t i = 0; i < skeleton.parent_indices.size(); ++i) {
    const std::int16_t parent = skeleton.parent_indices[i];
    EXPECT_TRUE(parent == -1 || (parent >= 0 && static_cast<std::size_t>(parent) < i)) << "bone " << i;
  }
}

TEST(HkxAnimationRealData, ReadsAVanillaSplineClipFromTheAnimationsArchive) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }

  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/radroach/animations/turnleft90.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_GT(animation.frame_count, 1u);
  EXPECT_GT(animation.duration, 0.0f);
  EXPECT_FALSE(animation.tracks.empty());
  for (const fo4::hkx::HkxAnimationTrack& track : animation.tracks) {
    ASSERT_EQ(track.translations.size(), animation.frame_count);
    ASSERT_EQ(track.rotations.size(), animation.frame_count);
    ASSERT_EQ(track.scales.size(), animation.frame_count);
    const auto& rotation = track.rotations.front();
    const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                   rotation[2] * rotation[2] + rotation[3] * rotation[3]);
    EXPECT_NEAR(length, 1.0f, 1e-4f);
  }
}

TEST(HkxAnimationRealData, DecodesEveryFrameOfAVanillaTwoBlockClip) {
  // Detects a two-block clip decoded with the wrong block stride, which must leave the shared frame to the later block.
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }

  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location =
      assets.locate("meshes/actors/character/animations/weapon/rifle/confident/poseb_idle1.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(animation.blocks.count, 2u);
  EXPECT_EQ(animation.blocks.max_frames_per_block, 256u);
  EXPECT_EQ(animation.frame_count, 314u);
  EXPECT_EQ(animation.tracks.size(), 94u);
  EXPECT_GT(animation.frame_count, animation.blocks.max_frames_per_block);
  EXPECT_NEAR(animation.blocks.block_duration,
              static_cast<float>(animation.blocks.max_frames_per_block - 1) * animation.frame_duration, 1e-4f);
  EXPECT_NEAR(animation.duration, static_cast<float>(animation.frame_count - 1) * animation.frame_duration, 1e-3f);

  for (const fo4::hkx::HkxAnimationTrack& track : animation.tracks) {
    ASSERT_EQ(track.translations.size(), animation.frame_count);
    ASSERT_EQ(track.rotations.size(), animation.frame_count);
    ASSERT_EQ(track.scales.size(), animation.frame_count);
    for (const std::array<float, 4>& rotation : track.rotations) {
      const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                     rotation[2] * rotation[2] + rotation[3] * rotation[3]);
      ASSERT_NEAR(length, 1.0f, 1e-3f);
    }
    for (const std::array<float, 3>& translation : track.translations) {
      for (const float value : translation) ASSERT_TRUE(std::isfinite(value));
    }
    for (const std::array<float, 3>& scale : track.scales) {
      for (const float value : scale) ASSERT_TRUE(std::isfinite(value));
    }
  }

  // Requires the aggregate of the seam step against its neighbour mean, so a packed block stride fails.
  const auto seam = fo4::test::hkx::seam_ratio(animation, 1e-4f);
  ASSERT_GE(seam.moving, 16u) << "only " << seam.moving << " tracks move around the seam";
  const bool median_holds = seam.median >= 0.5f;
  const bool band_holds = seam.within_band * 5 >= seam.moving * 4;
  EXPECT_TRUE(median_holds || band_holds)
      << "median ratio " << seam.median << ", " << seam.within_band << " of " << seam.moving
      << " tracks within a quarter to four times their neighbours";
}

TEST(HkxAnimationRealData, MatchesTheBgsManifestForTheRadroachClip) {
  // Detects a decode that must match BGS pose for pose on the same clip, as the manifest fixture holds.
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }

  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/radroach/animations/turnleft90.hkx");
  ASSERT_TRUE(location.has_value());
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);

  const auto manifest_path =
      std::filesystem::path{FO4_TESTDATA_DIR} / "hkx" / "radroach_turnleft90.animation-manifest.txt";
  const std::string golden = without_carriage_returns(read_text_file(manifest_path));
  ASSERT_FALSE(golden.empty()) << "missing " << manifest_path.string();
  EXPECT_EQ(fo4::test::hkx::animation_manifest(animation), golden);
}

// Constraint: the summary must report the decoded skeleton and clip, not merely that an animation class object exists.
TEST(HkxSummaryReal, SummarisesTheVanillaSkeletonAndTheRadroachClip) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);

  const auto skeleton_location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  if (!skeleton_location) {
    GTEST_SKIP() << "no character skeleton in the installed archives";
  }
  const auto skeleton_bytes = assets.read(*skeleton_location);
  fo4::hkx::HkxPackfile skeleton_packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(skeleton_bytes, skeleton_packfile), fo4::hkx::HkxStatus::Ok);
  const auto skeleton_summary = fo4::hkx::read_summary(skeleton_packfile);
  ASSERT_TRUE(skeleton_summary.has_skeleton);
  EXPECT_EQ(skeleton_summary.bone_names.size(), 95u);
  ASSERT_FALSE(skeleton_summary.bone_names.empty());
  EXPECT_EQ(skeleton_summary.bone_names.front(), "Root");
  ASSERT_FALSE(skeleton_summary.parent_indices.empty());
  EXPECT_EQ(skeleton_summary.parent_indices.front(), -1);

  // Requires a decoded clip, so a View (undecoded) animation class must not be reported as a clip.
  const auto clip_location = assets.locate("meshes/actors/radroach/animations/turnleft90.hkx");
  if (!clip_location) {
    GTEST_SKIP() << "no animation archive in the installed data";
  }
  const auto clip_bytes = assets.read(*clip_location);
  fo4::hkx::HkxPackfile clip_packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(clip_bytes, clip_packfile), fo4::hkx::HkxStatus::Ok);
  const auto clip_summary = fo4::hkx::read_summary(clip_packfile);
  ASSERT_EQ(clip_summary.clips.size(), 1U);
  const auto& clip = clip_summary.clips.front();
  EXPECT_EQ(clip.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(clip.class_name, "hkaSplineCompressedAnimation");
  EXPECT_GT(clip.frame_count, 1U);
  EXPECT_GT(clip.duration, 0.0f);
  EXPECT_GT(clip.track_count, 0U);
  ASSERT_FALSE(clip_summary.binding_skeletons.empty());
  EXPECT_FALSE(clip_summary.binding_skeletons.front().empty());
}

// Constraint: the writer must append to a shipped packfile and read back with every existing object unchanged.
TEST(HkxAppendReal, AppendsToTheVanillaSkeletonAndVerifiesTheRoundTrip) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  if (!location) {
    GTEST_SKIP() << "no character skeleton in the installed archives";
  }
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  ASSERT_FALSE(packfile.objects.empty());
  const auto& object = packfile.objects.front();
  const auto* layout = fo4::hkx::find_layout(object.class_name, object.signature);
  if (layout == nullptr) {
    GTEST_SKIP() << "the generated layout table has no entry for " << object.class_name;
  }

  fo4::hkx::HkxAppend appended;
  const std::vector<std::byte> zeros(layout->size, std::byte{0});
  ASSERT_EQ(fo4::hkx::append_object(bytes, object.class_name, object.signature, zeros, appended),
            fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(fo4::hkx::verify_append(bytes, appended.bytes, zeros), fo4::hkx::HkxStatus::Ok);

  fo4::hkx::HkxPackfile written;
  ASSERT_EQ(fo4::hkx::read_packfile(appended.bytes, written), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(written.objects.size(), packfile.objects.size() + 1);
  EXPECT_EQ(written.objects.back().class_name, object.class_name);
}

// Constraint: a clip written into a shipped packfile must decode through read_animation with its samples intact.
TEST(HkxAnimationWriteReal, AppendsAClipToTheVanillaSkeletonAndReadsItBack) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate("meshes/actors/character/characterassets/skeleton.hkx");
  if (!location) {
    GTEST_SKIP() << "no character skeleton in the installed archives";
  }
  const auto bytes = assets.read(*location);

  fo4::hkx::HkxInterleavedAnimation animation;
  animation.duration = 2.0f;
  animation.frame_count = 2;
  animation.track_count = 2;
  for (std::size_t sample = 0; sample < 4; ++sample) {
    animation.translations.push_back({static_cast<float>(sample) * 0.25f, 1.0f, 2.0f});
    animation.rotations.push_back({0.0f, 0.0f, 0.0f, 1.0f});
    animation.scales.push_back({1.0f, 1.0f, 1.0f});
  }

  fo4::hkx::HkxAppend appended;
  ASSERT_EQ(fo4::hkx::append_interleaved_animation(bytes, animation, appended), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(fo4::hkx::verify_append(bytes, appended.bytes), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxPackfile written;
  ASSERT_EQ(fo4::hkx::read_packfile(appended.bytes, written), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(written, decoded), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(decoded.kind, fo4::hkx::HkxAnimationKind::Interleaved);
  EXPECT_EQ(decoded.frame_count, 2U);
  EXPECT_EQ(decoded.tracks.size(), 2U);
  EXPECT_FLOAT_EQ(decoded.duration, 2.0f);
  EXPECT_FLOAT_EQ(decoded.tracks[1].translations[1][0], 0.75f);
}
