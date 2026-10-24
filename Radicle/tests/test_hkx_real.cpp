#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "hkx_animation_manifest.hpp"
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

float rotation_distance(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  return 1.0f - std::fabs(dot);
}

// Requires the same scale for both parts: the step is the translation distance plus the quaternion turn.
float pose_step(const fo4::hkx::HkxAnimationTrack& track, std::size_t frame) {
  const auto& from = track.translations[frame];
  const auto& to = track.translations[frame + 1];
  const float dx = to[0] - from[0];
  const float dy = to[1] - from[1];
  const float dz = to[2] - from[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz) +
         rotation_distance(track.rotations[frame], track.rotations[frame + 1]);
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

  // Requires the later block to own the shared frame: a packed stride must duplicate frame 255.
  // The step out of the seam must therefore collapse while its neighbours do not.
  const std::size_t seam = animation.blocks.max_frames_per_block - 1;
  ASSERT_LT(std::size_t{3}, seam);
  ASSERT_LT(seam + 2, animation.frame_count);
  for (std::size_t track = 0; track < animation.tracks.size(); ++track) {
    const fo4::hkx::HkxAnimationTrack& frames = animation.tracks[track];
    const float neighbour = std::max({pose_step(frames, seam - 3), pose_step(frames, seam - 2),
                                      pose_step(frames, seam + 1), pose_step(frames, seam + 2)});
    const float into_seam = pose_step(frames, seam - 1);
    const float out_of_seam = pose_step(frames, seam);
    if (neighbour > 1e-5f) {
      EXPECT_GE(into_seam, 0.25f * neighbour) << "track " << track;
      EXPECT_GE(out_of_seam, 0.25f * neighbour) << "track " << track;
    }
    EXPECT_LE(into_seam, 4.0f * neighbour + 1e-3f) << "track " << track;
    EXPECT_LE(out_of_seam, 4.0f * neighbour + 1e-3f) << "track " << track;
  }
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
