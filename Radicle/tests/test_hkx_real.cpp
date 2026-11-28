#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/animation_edit.hpp"
#include "fo4/hkx/convert_packfile.hpp"
#include "fo4/hkx/decode.hpp"
#include "fo4/hkx/gltf_animation.hpp"
#include "fo4/hkx/gltf_animation_save.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"
#include "fo4/hkx/pose.hpp"
#include "fo4/hkx/ragdoll_edit.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_document.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "fo4/hkx/skeleton_write.hpp"
#include "fo4/hkx/summary.hpp"
#include "fo4/hkx/write.hpp"
#include "fo4/io/sha256.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_plain.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/skin.hpp"
#include "hkx_animation_manifest.hpp"
#include "hkx_noop_check.hpp"
#include "hkx_seam_ratio.hpp"
#include "hkx_turret_glb.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Requires the committed HKX fixture to decode on CI: the same bytes the install holds, read without an archive.
std::vector<std::byte> read_binary_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::byte> bytes;
  char buffer[4096];
  while (stream.read(buffer, static_cast<std::streamsize>(sizeof buffer)) || stream.gcount() > 0) {
    for (std::streamsize i = 0; i < stream.gcount(); ++i) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[i])));
    }
  }
  return bytes;
}

// Requires the checkout root whatever directory the test binary runs from: the committed fixtures live off it.
std::filesystem::path repository_root() {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  for (int level = 0; level < 8 && !dir.empty(); ++level) {
    if (std::filesystem::is_directory(dir / "Radicle" / "testdata" / "hkx", ec)) return dir;
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

// Requires one vanilla HKX read the same way by every test here: from the install when FO4_DATA_DIR is set, else
// from the committed HKX fixture. Skips or fails the calling test, which must return when it did either.
void load_vanilla_hkx(const std::filesystem::path& fixture_under_meshes, std::string_view archive_key,
                      std::vector<std::byte>& bytes) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) {
    // Constraint: the committed HKX fixture runs this check on CI instead of skipping without game data.
    const auto root = repository_root();
    const auto file = root / "Radicle" / "tests" / "fixtures" / "hkx" / "Meshes" / fixture_under_meshes;
    // Constraint: the public export drops .hkx game files, so only a lane that restores them carries this file.
    if (root.empty() || !std::filesystem::exists(file)) {
      // Constraint: CI restores this fixture, so a missing file there fails instead of skipping silently.
      if (std::getenv("CI") != nullptr) {
        FAIL() << "CI is missing the committed " << file.string();
      }
      GTEST_SKIP() << "no FO4_DATA_DIR and no committed " << file.string();
    }
    bytes = read_binary_file(file);
    return;
  }
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);
  const auto location = assets.locate(archive_key);
  ASSERT_TRUE(location.has_value()) << archive_key;
  bytes = assets.read(*location);
}

std::filesystem::path lossless_clip_fixture() {
  return std::filesystem::path{"Actors"} / "Character" / "Animations" / "Paired" /
         "PairedKill2HMBashKneeAndHead_AttackerLead.hkx";
}
constexpr std::string_view kLosslessClipKey =
    "meshes/actors/character/animations/paired/pairedkill2hmbashkneeandhead_attackerlead.hkx";

// Requires the manifest to compare equal whatever line endings a checkout produced.
std::string without_carriage_returns(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

}  // namespace

TEST(HkxSkeletonRealData, ReadsVanillaHumanSkeletonFromAnimationsArchive) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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

TEST(HkxSkeletonRealData, ReferencePoseAndLockEditsSaveOnTheVanillaSkeleton) {
  // Constraint: detects the BGS SkeletonPoseSave case refused on the shipped skeleton, or a save that disturbs another bone.
  std::vector<std::byte> bytes;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx",
                   "meshes/actors/character/characterassets/skeleton.hkx", bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton before;
  ASSERT_EQ(fo4::hkx::read_skeleton(packfile, before), fo4::hkx::HkxStatus::Ok);
  ASSERT_GT(before.bone_names.size(), 1u);

  fo4::hkx::HkxSkeletonEdit pose;
  pose.kind = fo4::hkx::HkxSkeletonEditKind::SetPose;
  pose.bone = 0;
  pose.pose = before.reference_pose[0];
  pose.pose.translation[0] = 12.5f;
  fo4::hkx::HkxSkeletonEdit lock;
  lock.kind = fo4::hkx::HkxSkeletonEditKind::SetLock;
  lock.bone = 1;
  lock.locked = !before.lock_translation[1];
  fo4::hkx::HkxSkeletonWrite written;
  const std::vector<fo4::hkx::HkxSkeletonEdit> edits{pose, lock};
  ASSERT_EQ(fo4::hkx::write_skeleton(bytes, edits, written), fo4::hkx::HkxStatus::Ok) << written.refusal;
  ASSERT_TRUE(written.changed);

  fo4::hkx::HkxPackfile reopened;
  ASSERT_EQ(fo4::hkx::read_packfile(written.bytes, reopened), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton after;
  ASSERT_EQ(fo4::hkx::read_skeleton(reopened, after), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(after.reference_pose[0].translation[0], 12.5f);
  EXPECT_EQ(after.lock_translation[1], !before.lock_translation[1]);
  EXPECT_EQ(after.bone_names, before.bone_names);
  EXPECT_EQ(after.parent_indices, before.parent_indices);
  for (std::size_t i = 1; i < before.reference_pose.size(); ++i) {
    EXPECT_EQ(after.reference_pose[i].translation, before.reference_pose[i].translation) << "bone " << i;
  }
}

TEST(HkxAnimationRealData, ReadsAVanillaSplineClipFromTheAnimationsArchive) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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

TEST(HkxAnimationRealData, DecodesTheVanillaLosslessClipToTheBgsManifest) {
  // Constraint: detects a lossless decode that drifts from BGS on a shipped paired-kill clip in kind or pose.
  std::vector<std::byte> bytes;
  load_vanilla_hkx(lossless_clip_fixture(), kLosslessClipKey, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);

  EXPECT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Lossless);
  EXPECT_EQ(animation.class_name, "hkaLosslessCompressedAnimation");
  EXPECT_EQ(animation.frame_count, 79u);
  EXPECT_EQ(animation.tracks.size(), 94u);

  const auto manifest_path = std::filesystem::path{FO4_TESTDATA_DIR} / "hkx" /
                             "pairedkill2hmbashkneeandhead_attackerlead.animation-manifest.txt";
  const std::string golden = without_carriage_returns(read_text_file(manifest_path));
  ASSERT_FALSE(golden.empty()) << "missing " << manifest_path.string();
  EXPECT_EQ(fo4::test::hkx::animation_manifest(animation), golden);
}

TEST(HkxAnimationRealData, PosesTheVanillaLosslessClipOnTheVanillaCharacterSkeleton) {
  // Constraint: detects a lossless clip whose binding no longer fits the shipped character skeleton, a pose
  // sampled from the wrong frame, and a bone outside the binding that loses its reference transform.
  std::vector<std::byte> skeleton_bytes;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx",
                   "meshes/actors/character/characterassets/skeleton.hkx", skeleton_bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> clip_bytes;
  load_vanilla_hkx(lossless_clip_fixture(), kLosslessClipKey, clip_bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxPackfile skeleton_packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(skeleton_bytes, skeleton_packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(skeleton_packfile, skeleton), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxPackfile clip_packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(clip_bytes, clip_packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(clip_packfile, animation), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimationBinding binding;
  ASSERT_EQ(fo4::hkx::read_binding(clip_packfile, binding), fo4::hkx::HkxStatus::Ok);

  ASSERT_EQ(animation.kind, fo4::hkx::HkxAnimationKind::Lossless);
  ASSERT_EQ(skeleton.bone_names.size(), 95u);
  ASSERT_EQ(animation.tracks.size(), 94u);
  ASSERT_EQ(binding.transform_track_to_bone_indices.size(), animation.tracks.size());
  ASSERT_EQ(animation.frame_count, 79u);

  std::vector<bool> bound(skeleton.bone_names.size(), false);
  const auto expect_frame = [&](float time, std::size_t frame) {
    std::vector<fo4::hkx::HkxQsTransform> pose;
    ASSERT_EQ(fo4::hkx::sample_pose(skeleton, animation, binding, time, pose), fo4::hkx::HkxStatus::Ok);
    ASSERT_EQ(pose.size(), skeleton.bone_names.size());
    for (std::size_t track = 0; track < animation.tracks.size(); ++track) {
      const auto bone = static_cast<std::size_t>(binding.transform_track_to_bone_indices[track]);
      bound[bone] = true;
      const auto& sampled = animation.tracks[track];
      const auto& rotation = sampled.rotations[frame];
      const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                     rotation[2] * rotation[2] + rotation[3] * rotation[3]);
      ASSERT_GT(length, 0.0f) << "track " << track;
      for (std::size_t c = 0; c < 3; ++c) {
        EXPECT_FLOAT_EQ(pose[bone].translation[c], sampled.translations[frame][c]) << "track " << track;
        EXPECT_FLOAT_EQ(pose[bone].scale[c], sampled.scales[frame][c]) << "track " << track;
      }
      for (std::size_t c = 0; c < 4; ++c) {
        EXPECT_NEAR(pose[bone].rotation[c], rotation[c] / length, 1e-5f) << "track " << track;
      }
    }
    for (std::size_t bone = 0; bone < bound.size(); ++bone) {
      if (bound[bone]) continue;
      EXPECT_EQ(pose[bone].translation, skeleton.reference_pose[bone].translation) << "bone " << bone;
      EXPECT_EQ(pose[bone].rotation, skeleton.reference_pose[bone].rotation) << "bone " << bone;
      EXPECT_EQ(pose[bone].scale, skeleton.reference_pose[bone].scale) << "bone " << bone;
    }
    std::vector<fo4::hkx::HkxQsTransform> model;
    ASSERT_EQ(fo4::hkx::model_space_pose(skeleton, pose, model), fo4::hkx::HkxStatus::Ok);
    ASSERT_EQ(model.size(), pose.size());
    for (const auto& transform : model) {
      for (const float value : transform.translation) EXPECT_TRUE(std::isfinite(value));
      for (const float value : transform.rotation) EXPECT_TRUE(std::isfinite(value));
      for (const float value : transform.scale) EXPECT_TRUE(std::isfinite(value));
    }
  };
  expect_frame(0.0f, 0);
  expect_frame(animation.duration, animation.frame_count - 1);
  // Constraint: 94 tracks drive a 95-bone skeleton, so exactly one bone keeps its reference transform.
  EXPECT_EQ(std::count(bound.begin(), bound.end(), false), 1);
}

// Constraint: the summary must report the decoded skeleton and clip, not merely that an animation class object exists.
TEST(HkxSummaryReal, SummarisesTheVanillaSkeletonAndTheRadroachClip) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
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

namespace {

// Requires a committed sample the install does not hold, read the way load_vanilla_hkx reads a fixture: skip locally, fail on CI.
void load_sample_hkx(const std::string& name, std::vector<std::byte>& bytes) {
  const auto root = repository_root();
  const auto file = root / "Radicle" / "tests" / "fixtures" / "hkx" / "samples" / name;
  if (root.empty() || !std::filesystem::exists(file)) {
    if (std::getenv("CI") != nullptr) {
      FAIL() << "CI is missing the committed " << file.string();
    }
    GTEST_SKIP() << "no committed " << file.string();
  }
  bytes = read_binary_file(file);
}

bool same_value(const fo4::hkx::HkxValue& a, const fo4::hkx::HkxValue& b) {
  if (a.kind != b.kind || a.boolean != b.boolean || a.integer != b.integer || a.text != b.text || a.target != b.target ||
      a.names != b.names || a.items.size() != b.items.size() ||
      std::bit_cast<std::uint64_t>(a.real) != std::bit_cast<std::uint64_t>(b.real)) {
    return false;
  }
  for (std::size_t i = 0; i < a.items.size(); ++i) {
    if (!same_value(a.items[i], b.items[i])) return false;
  }
  return true;
}

bool same_document(const fo4::hkx::HkxSemanticDocument& a, const fo4::hkx::HkxSemanticDocument& b) {
  if (a.root != b.root || a.objects.size() != b.objects.size()) return false;
  for (const auto& pair : a.objects) {
    const auto other = b.objects.find(pair.first);
    if (other == b.objects.end() || other->second.type_name != pair.second.type_name ||
        other->second.members.size() != pair.second.members.size()) {
      return false;
    }
    for (const auto& member : pair.second.members) {
      const auto match = other->second.members.find(member.first);
      if (match == other->second.members.end() || !same_value(member.second, match->second)) return false;
    }
  }
  return true;
}

// Invariant: flips the lowest bit of every bool, integer and finite real, so each scalar leaf changes and stays representable.
void flip_scalars(fo4::hkx::HkxValue& value) {
  using Kind = fo4::hkx::HkxValue::Kind;
  switch (value.kind) {
    case Kind::Bool:
      value.boolean = !value.boolean;
      break;
    case Kind::Integer:
      value.integer ^= 1;
      break;
    case Kind::Real:
      if (std::isfinite(value.real)) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value.real)) ^ 1u;
        value.real = static_cast<double>(std::bit_cast<float>(bits));
      }
      break;
    case Kind::Array:
    case Kind::Struct:
      for (fo4::hkx::HkxValue& item : value.items) flip_scalars(item);
      break;
    default:
      break;
  }
}

void expect_scalar_round_trip(const std::vector<std::byte>& bytes, const fo4::hkx::HkxDecodeOptions& options) {
  const auto decoded = fo4::hkx::decode_document(bytes, options);
  ASSERT_EQ(decoded.status, fo4::hkx::HkxDecodeStatus::Ok)
      << decoded.block.class_name << " " << decoded.block.member << ": " << decoded.block.reason;
  ASSERT_FALSE(decoded.document.objects.empty());

  const auto unchanged = fo4::hkx::emit_document(bytes, decoded.document, options);
  ASSERT_EQ(unchanged.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_TRUE(unchanged.bytes == bytes);

  auto edited = decoded.document;
  for (auto& object : edited.objects) {
    for (auto& member : object.second.members) flip_scalars(member.second);
  }
  const auto flipped = fo4::hkx::emit_document(bytes, edited, options);
  ASSERT_EQ(flipped.status, fo4::hkx::HkxDecodeStatus::Ok)
      << flipped.block.class_name << " " << flipped.block.member << ": " << flipped.block.reason;
  ASSERT_EQ(flipped.bytes.size(), bytes.size());
  EXPECT_FALSE(flipped.bytes == bytes);

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  const auto& data = packfile.sections[packfile.data_section];
  const std::size_t start = data.file_offset;
  const std::size_t end = start + data.data.size();
  bool outside_untouched = true;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if ((i < start || i >= end) && flipped.bytes[i] != bytes[i]) outside_untouched = false;
  }
  EXPECT_TRUE(outside_untouched);
  for (const fo4::hkx::HkxObjectView& object : packfile.objects) {
    if (fo4::hkx::find_decode_class(object.class_name) != nullptr) continue;
    EXPECT_TRUE(std::equal(object.data.begin(), object.data.end(),
                           flipped.bytes.begin() + static_cast<std::ptrdiff_t>(start + object.offset)))
        << object.class_name;
  }

  const auto reread = fo4::hkx::decode_document(flipped.bytes, options);
  ASSERT_EQ(reread.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_TRUE(same_document(reread.document, edited));

  const auto reverted = fo4::hkx::emit_document(flipped.bytes, decoded.document, options);
  ASSERT_EQ(reverted.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_TRUE(reverted.bytes == bytes);
}

}  // namespace

TEST(HkxDecodeReal, RoundTripsTheVanillaSplineClipsByteIdenticalAndAgreesWithTheAnimationReader) {
  // Constraint: detects a decoder or emitter that rewrites a byte it does not own on a shipped clip, or reads a member at the wrong offset or width.
  std::vector<std::byte> radroach;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "RadRoach" / "Animations" / "TurnLeft90.hkx",
                   "meshes/actors/radroach/animations/turnleft90.hkx", radroach);
  if (IsSkipped() || HasFatalFailure()) return;

  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(radroach, {}));

  const auto decoded = fo4::hkx::decode_document(radroach);
  ASSERT_EQ(decoded.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_EQ(decoded.document.root, std::optional<std::uint64_t>{0});
  EXPECT_EQ(decoded.document.objects.size(), 6u);
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(radroach, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation animation;
  ASSERT_EQ(fo4::hkx::read_animation(packfile, animation), fo4::hkx::HkxStatus::Ok);
  const auto clip = std::find_if(decoded.document.objects.begin(), decoded.document.objects.end(), [](const auto& pair) {
    return pair.second.type_name == "hkaSplineCompressedAnimation";
  });
  ASSERT_NE(clip, decoded.document.objects.end());
  EXPECT_EQ(static_cast<std::uint32_t>(clip->second.members.at("numFrames").integer), animation.frame_count);
  EXPECT_EQ(clip->second.members.at("numberOfFloatTracks").integer, static_cast<std::int64_t>(animation.float_track_count));
}

TEST(HkxMotionReal, TheVanillaLeftTurnClipReadsADefaultReferenceFrameWithAPositiveTurn) {
  // Constraint: detects an extracted-motion reader that fails on a shipped clip or flips the turn sign.
  std::vector<std::byte> bytes;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "RadRoach" / "Animations" / "TurnLeft90.hkx",
                   "meshes/actors/radroach/animations/turnleft90.hkx", bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxExtractedMotion motion;
  ASSERT_EQ(fo4::hkx::read_extracted_motion(packfile, motion), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(motion.kind, fo4::hkx::HkxMotionKind::Default);
  ASSERT_FALSE(motion.samples.empty());
  EXPECT_GT(fo4::hkx::summarize_motion(motion).net_turn_radians, 0.0f);
}

TEST(HkxDecodeReal, KeepsTheUnlistedPhysicsAndMapperObjectsOfVanillaSkeletonsUntouched) {
  // Constraint: detects a decoder that refuses a skeleton file whose physics classes it does not cover, or an emitter that disturbs those opaque objects' bytes.
  std::vector<std::byte> character;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx",
                   "meshes/actors/character/characterassets/skeleton.hkx", character);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxDecodeOptions opaque;
  opaque.opaque_unlisted = true;
  const auto refused = fo4::hkx::decode_document(character);
  EXPECT_EQ(refused.status, fo4::hkx::HkxDecodeStatus::Refused);
  EXPECT_FALSE(refused.block.class_name.empty());
  EXPECT_EQ(fo4::hkx::find_decode_class(refused.block.class_name), nullptr);
  EXPECT_TRUE(refused.document.objects.empty());
  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(character, opaque));

  const auto decoded = fo4::hkx::decode_document(character, opaque);
  ASSERT_EQ(decoded.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_NE(std::find(decoded.opaque_classes.begin(), decoded.opaque_classes.end(), "hkaSkeletonMapper"),
            decoded.opaque_classes.end());
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(character, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(packfile, skeleton), fo4::hkx::HkxStatus::Ok);
  const bool agrees = std::any_of(decoded.document.objects.begin(), decoded.document.objects.end(), [&](const auto& pair) {
    return pair.second.type_name == "hkaSkeleton" &&
           pair.second.members.at("bones").items.size() == skeleton.bone_names.size();
  });
  EXPECT_TRUE(agrees);
}

TEST(HkxDecodeReal, RoundTripsTheCommittedTurretClipAndSkeletons) {
  // Constraint: detects a decoder tuned to the RadRoach and character files that fails on another rig's clip or on skeletons with different physics classes.
  std::vector<std::byte> clip;
  load_sample_hkx("TurretIdleWeapReady.hkx", clip);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> mounted;
  load_sample_hkx("TurretMountedSkeleton.hkx", mounted);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> standing;
  load_sample_hkx("TurretStandingSkeleton.hkx", standing);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxDecodeOptions opaque;
  opaque.opaque_unlisted = true;
  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(clip, {}));
  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(mounted, opaque));
  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(standing, opaque));
  EXPECT_EQ(fo4::hkx::decode_document(mounted).status, fo4::hkx::HkxDecodeStatus::Refused);
  EXPECT_EQ(fo4::hkx::decode_document(standing).status, fo4::hkx::HkxDecodeStatus::Refused);
}

TEST(HkxDecodeReal, RefusesTheLosslessClipNamingItsClassAndKeepsItsBytesWhenToldToLeaveItOpaque) {
  // Constraint: detects a lossless animation decoded from a layout nothing verifies, or one whose 50 KB of sample bytes change on save.
  std::vector<std::byte> bytes;
  load_vanilla_hkx(lossless_clip_fixture(), kLosslessClipKey, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const auto refused = fo4::hkx::decode_document(bytes);
  EXPECT_EQ(refused.status, fo4::hkx::HkxDecodeStatus::Refused);
  EXPECT_EQ(refused.block.class_name, "hkaLosslessCompressedAnimation");
  EXPECT_TRUE(refused.document.objects.empty());
  EXPECT_EQ(fo4::hkx::emit_document(bytes, {}).status, fo4::hkx::HkxDecodeStatus::Refused);

  fo4::hkx::HkxDecodeOptions opaque;
  opaque.opaque_unlisted = true;
  const auto kept = fo4::hkx::decode_document(bytes, opaque);
  ASSERT_EQ(kept.status, fo4::hkx::HkxDecodeStatus::Ok);
  EXPECT_EQ(kept.opaque_classes, std::vector<std::string>{"hkaLosslessCompressedAnimation"});
  ASSERT_NO_FATAL_FAILURE(expect_scalar_round_trip(bytes, opaque));
}

TEST(HkxConvertReal, SameVersionConversionOfVanillaFilesKeepsTheBytesAndNeverWritesAPartialFile) {
  // Constraint: detects a convert path that changes a shipped clip, mis-reads its version stamp, or returns bytes for a skeleton whose opaque classes it could not carry through.
  std::vector<std::byte> clip;
  load_sample_hkx("TurretIdleWeapReady.hkx", clip);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> character;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx",
                   "meshes/actors/character/characterassets/skeleton.hkx", character);
  if (IsSkipped() || HasFatalFailure()) return;

  const auto same = fo4::hkx::convert_packfile(clip, "hk_2014.1.0-r1");
  ASSERT_EQ(same.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << same.reason;
  EXPECT_EQ(same.source_version, "hk_2014.1.0-r1");
  EXPECT_EQ(same.changed_total, 0u);
  EXPECT_EQ(same.report.patched_objects, 0u);
  EXPECT_TRUE(same.bytes == clip);

  const auto skeleton = fo4::hkx::convert_packfile(character, "hk_2014.1.0-r1");
  EXPECT_NE(std::find(skeleton.opaque_classes.begin(), skeleton.opaque_classes.end(), "hkaSkeletonMapper"),
            skeleton.opaque_classes.end());
  EXPECT_EQ(skeleton.bytes.empty(), skeleton.status != fo4::hkx::HkxPackfileConvertStatus::Ok);
  if (skeleton.status == fo4::hkx::HkxPackfileConvertStatus::Ok) { EXPECT_TRUE(skeleton.bytes == character); }
}

namespace {

// Requires the committed manifest next to the fixtures, read the way load_sample_hkx reads a sample: skip locally, fail on CI.
void load_hkx_manifest(nlohmann::json& manifest, std::filesystem::path& folder) {
  const auto root = repository_root();
  folder = root / "Radicle" / "tests" / "fixtures" / "hkx";
  const auto file = folder / "manifest.json";
  if (root.empty() || !std::filesystem::exists(file)) {
    if (std::getenv("CI") != nullptr) {
      FAIL() << "CI is missing the committed " << file.string();
    }
    GTEST_SKIP() << "no committed " << file.string();
  }
  manifest = nlohmann::json::parse(read_text_file(file));
}

bool ends_with_text(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST(HkxFixtureManifest, EveryEntryMatchesItsFileAndEveryFixtureFileIsListed) {
  // Constraint: detects a fixture re-extracted from another build, a truncated file, or a file added without an entry, any of which silently changes what the HKX tests prove.
  nlohmann::json manifest;
  std::filesystem::path folder;
  load_hkx_manifest(manifest, folder);
  if (IsSkipped() || HasFatalFailure()) return;

  std::set<std::string> listed;
  for (const auto& entry : manifest.at("files")) {
    const auto path = entry.at("path").get<std::string>();
    listed.insert(path);
    const auto bytes = read_binary_file(folder / path);
    ASSERT_FALSE(bytes.empty()) << path;
    EXPECT_EQ(bytes.size(), entry.at("bytes").get<std::size_t>()) << path;
    auto digest = fo4::io::sha256_hex(bytes);
    for (char& ch : digest) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    EXPECT_EQ(digest, entry.at("sha256").get<std::string>()) << path;
  }
  for (const char* turret : {"samples/TurretIdleWeapReady.hkx", "samples/TurretMountedSkeleton.hkx",
                             "samples/TurretStandingSkeleton.hkx",
                             "samples/Meshes/Actors/Turret/CharacterAssets/TurretMounted.nif"}) {
    EXPECT_EQ(listed.count(turret), 1u) << turret;
  }
  std::error_code ec;
  for (const auto& item : std::filesystem::recursive_directory_iterator(folder, ec)) {
    if (!item.is_regular_file()) continue;
    const auto relative = std::filesystem::relative(item.path(), folder).generic_string();
    if (relative == "manifest.json" || relative == "README.md") continue;
    EXPECT_EQ(listed.count(relative), 1u) << "unlisted fixture " << relative;
  }
}

// Detects: a scoped HKX change altering the model of a committed shipped packfile.
TEST(HkxRepackReal, OneCommittedHkxRewritesToTheSameModel) {
  nlohmann::json manifest;
  std::filesystem::path folder;
  load_hkx_manifest(manifest, folder);
  if (IsSkipped() || HasFatalFailure()) return;
  const std::string sample = "Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx";
  for (const auto& entry : manifest.at("files")) {
    const auto path = entry.at("path").get<std::string>();
    if (path != sample) continue;
    const auto bytes = read_binary_file(folder / path);
    const auto report = fo4::hkx::repack_check(bytes);
    EXPECT_EQ(report.verdict, fo4::hkx::HkxRepackVerdict::Equal) << path << ": " << report.difference;
    EXPECT_EQ(report.rewritten_size, report.source_size) << path;
    return;
  }
  FAIL() << "the committed HKX manifest does not contain " << sample;
}

TEST(HkxRepackReal, EveryCommittedHkxRewritesToTheSameModelAndReachesAFixedPoint) {
  // Constraint: detects a rewrite that drops or reorders an object, fixup or section of a shipped file, or one that keeps changing on every save.
  nlohmann::json manifest;
  std::filesystem::path folder;
  load_hkx_manifest(manifest, folder);
  if (IsSkipped() || HasFatalFailure()) return;

  std::size_t checked = 0;
  std::size_t byte_identical = 0;
  for (const auto& entry : manifest.at("files")) {
    const auto path = entry.at("path").get<std::string>();
    if (!ends_with_text(path, ".hkx")) continue;
    SCOPED_TRACE(path);
    const auto bytes = read_binary_file(folder / path);
    const auto report = fo4::hkx::repack_check(bytes);
    EXPECT_EQ(report.verdict, fo4::hkx::HkxRepackVerdict::Equal) << report.difference;
    EXPECT_EQ(report.rewritten_size, report.source_size);
    byte_identical += report.bytes_identical ? 1 : 0;
    ++checked;

    fo4::hkx::HkxPackfile first;
    ASSERT_EQ(fo4::hkx::read_packfile(bytes, first), fo4::hkx::HkxStatus::Ok);
    std::vector<std::byte> once;
    ASSERT_EQ(fo4::hkx::rewrite_packfile(bytes, first, once), fo4::hkx::HkxStatus::Ok);
    fo4::hkx::HkxPackfile second;
    ASSERT_EQ(fo4::hkx::read_packfile(once, second), fo4::hkx::HkxStatus::Ok);
    std::vector<std::byte> twice;
    ASSERT_EQ(fo4::hkx::rewrite_packfile(once, second, twice), fo4::hkx::HkxStatus::Ok);
    EXPECT_TRUE(twice == once);
  }
  EXPECT_GT(checked, 0u);
  RecordProperty("repack_byte_identical_count", std::to_string(byte_identical));
}

TEST(HkxRepackReal, EveryVanillaHkxInTheArchivesRepacksToTheSameModelOrIsRefusedCleanly) {
  // Constraint: detects a rewrite that changes the model of any shipped HKX, which the committed samples alone cannot show.
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"Fallout4.esm"};
  const auto archives = fo4::archive::archive_load_order(dir, {}, plugins);
  if (archives.empty()) {
    GTEST_SKIP() << "no Fallout 4 archives found under FO4_DATA_DIR";
  }
  const auto assets = fo4::archive::AssetResolver::open(dir, archives);

  std::size_t equal = 0;
  std::size_t refused = 0;
  std::size_t unreadable = 0;
  std::size_t failures = 0;
  std::string first_failure;
  for (const auto& listing : assets.list("meshes", ".hkx", false)) {
    std::vector<std::byte> bytes;
    try {
      bytes = assets.read(listing.location);
    } catch (const std::exception&) {
      ++unreadable;
      continue;
    }
    const auto report = fo4::hkx::repack_check(bytes);
    switch (report.verdict) {
      case fo4::hkx::HkxRepackVerdict::Equal: ++equal; break;
      case fo4::hkx::HkxRepackVerdict::Refused: ++refused; break;
      case fo4::hkx::HkxRepackVerdict::ReadFailed: ++unreadable; break;
      default:
        ++failures;
        if (first_failure.empty()) first_failure = listing.path + ": " + report.difference;
    }
  }
  RecordProperty("repack_equal_count", std::to_string(equal));
  RecordProperty("repack_refused_count", std::to_string(refused));
  RecordProperty("repack_unreadable_count", std::to_string(unreadable));
  EXPECT_EQ(failures, 0u) << first_failure;
  EXPECT_GT(equal, 0u);
}

TEST(HkxWriterNoOpReal, EachWriterReturnsACommittedTurretSkeletonUnchangedWhenAskedToChangeNothing) {
  // Constraint: detects a ragdoll save that alters a byte of a shipped file for a same-value edit; the skeleton and mapper cases only pin their early-return no-op contract, not a patch path.
  for (const char* name : {"TurretMountedSkeleton.hkx", "TurretStandingSkeleton.hkx"}) {
    SCOPED_TRACE(name);
    std::vector<std::byte> source;
    load_sample_hkx(name, source);
    if (IsSkipped() || HasFatalFailure()) return;

    fo4::hkx::HkxSkeletonWrite saved;
    EXPECT_EQ(fo4::hkx::write_skeleton(source, {}, saved), fo4::hkx::HkxStatus::Ok);
    EXPECT_FALSE(saved.changed);
    EXPECT_TRUE(saved.bytes.empty());

    fo4::hkx::HkxPackfile pack;
    ASSERT_EQ(fo4::hkx::read_packfile(source, pack), fo4::hkx::HkxStatus::Ok);
    std::vector<fo4::hkx::HkxSkeletonMapper> mappers;
    ASSERT_EQ(fo4::hkx::read_skeleton_mappers(pack, mappers), fo4::hkx::HkxStatus::Ok);
    EXPECT_FALSE(mappers.empty());
    std::vector<std::byte> remapped;
    std::string refusal;
    ASSERT_EQ(fo4::hkx::write_skeleton_mappers(source, mappers, remapped, &refusal), fo4::hkx::HkxStatus::Ok) << refusal;
    EXPECT_TRUE(remapped == source);

    fo4::hkx::HkxRagdollModel model;
    fo4::hkx::HkxPhysicsLoadReport report;
    ASSERT_EQ(fo4::hkx::read_ragdoll(source, model, report), fo4::hkx::HkxStatus::Ok);
    ASSERT_FALSE(model.bodies.empty());
    fo4::hkx::HkxRagdollEdits edits;
    fo4::hkx::HkxRagdollBodyEdit body;
    body.body = model.bodies.front().id;
    body.position = model.bodies.front().position;
    edits.bodies.push_back(body);
    const auto result = fo4::hkx::apply_ragdoll_edits(source, edits);
    ASSERT_EQ(result.status, fo4::hkx::HkxRagdollEditStatus::Ok) << result.message;
    EXPECT_TRUE(result.bytes == source);
  }
}

TEST(HkxWriterNoOpReal, BehaviorWriterRestatingAFlagAndTheStartStateKeepsTheModelOfEveryCommittedBehavior) {
  // Constraint: detects a behaviour save that drops a section, an object or a pointer, or changes a table of a shipped graph when the edit changes nothing; byte identity is logged only.
  nlohmann::json manifest;
  std::filesystem::path folder;
  load_hkx_manifest(manifest, folder);
  if (IsSkipped() || HasFatalFailure()) return;

  std::size_t checked = 0;
  std::size_t byte_identical = 0;
  for (const auto& entry : manifest.at("files")) {
    const auto path = entry.at("path").get<std::string>();
    if (!ends_with_text(path, ".hkx") || path.find("/Behaviors/") == std::string::npos) continue;
    SCOPED_TRACE(path);
    const auto bytes = read_binary_file(folder / path);
    ASSERT_FALSE(bytes.empty());
    const auto noop = fo4::test::hkx::behavior_noop(bytes);
    EXPECT_EQ(noop.difference, "");
    EXPECT_GE(noop.edits, 1u);
    byte_identical += noop.bytes_identical ? 1 : 0;
    RecordProperty("behavior_noop_" + std::to_string(checked),
                   path + " bytes=" + std::to_string(bytes.size()) + " sha256=" + fo4::io::sha256_hex(bytes) +
                       " identical=" + (noop.bytes_identical ? "1" : "0"));
    ++checked;
  }
  EXPECT_GE(checked, 5u);
  RecordProperty("behavior_noop_count", std::to_string(checked));
  RecordProperty("behavior_noop_byte_identical_count", std::to_string(byte_identical));
}

TEST(HkxWriterNoOpReal, SkeletonPatchWriterGivesTheVanillaSkeletonBackWhenTheEditIsUndone) {
  // Constraint: detects a skeleton patch that leaves a changed lane, padding or lock byte behind, which the early-return no-op of an empty edit list never reaches.
  std::vector<std::byte> bytes;
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx",
                   "meshes/actors/character/characterassets/skeleton.hkx", bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  fo4::hkx::HkxPackfile packfile;
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton before;
  ASSERT_EQ(fo4::hkx::read_skeleton(packfile, before), fo4::hkx::HkxStatus::Ok);
  ASSERT_GT(before.bone_names.size(), 1u);

  fo4::hkx::HkxSkeletonEdit pose;
  pose.kind = fo4::hkx::HkxSkeletonEditKind::SetPose;
  pose.bone = 0;
  pose.pose = before.reference_pose[0];
  fo4::hkx::HkxSkeletonEdit lock;
  lock.kind = fo4::hkx::HkxSkeletonEditKind::SetLock;
  lock.bone = 1;
  fo4::hkx::HkxSkeletonEdit moved = pose;
  moved.pose.translation[0] += 12.5f;
  fo4::hkx::HkxSkeletonEdit flipped = lock;
  flipped.locked = !before.lock_translation[1];
  lock.locked = before.lock_translation[1];

  fo4::hkx::HkxSkeletonWrite changed;
  ASSERT_EQ(fo4::hkx::write_skeleton(bytes, std::vector{moved, flipped}, changed), fo4::hkx::HkxStatus::Ok) << changed.refusal;
  ASSERT_TRUE(changed.changed);
  fo4::hkx::HkxSkeletonWrite restored;
  ASSERT_EQ(fo4::hkx::write_skeleton(changed.bytes, std::vector{pose, lock}, restored), fo4::hkx::HkxStatus::Ok)
      << restored.refusal;
  ASSERT_TRUE(restored.changed);

  fo4::hkx::HkxPackfile again;
  ASSERT_EQ(fo4::hkx::read_packfile(restored.bytes, again), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton after;
  ASSERT_EQ(fo4::hkx::read_skeleton(again, after), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(after.bone_names, before.bone_names);
  EXPECT_EQ(after.parent_indices, before.parent_indices);
  EXPECT_EQ(after.lock_translation, before.lock_translation);
  ASSERT_EQ(after.reference_pose.size(), before.reference_pose.size());
  for (std::size_t i = 0; i < before.reference_pose.size(); ++i) {
    EXPECT_EQ(after.reference_pose[i].translation, before.reference_pose[i].translation) << "bone " << i;
    EXPECT_EQ(after.reference_pose[i].rotation, before.reference_pose[i].rotation) << "bone " << i;
    EXPECT_EQ(after.reference_pose[i].scale, before.reference_pose[i].scale) << "bone " << i;
  }
  EXPECT_EQ(fo4::hkx::first_packfile_difference(packfile, again), "");
  RecordProperty("skeleton_undo_byte_identical", restored.bytes == bytes ? "1" : "0");
}

TEST(HkxWriterNoOpReal, GltfSaveOfTheCommittedTurretClipsOwnSamplesReadsBackTheSameClip) {
  // Constraint: detects a glTF save that loses the bound track map or skeleton binding, drops an object, or drifts a shipped track past the spline budget; the turret clip has no notes or moving root motion, so notes and motion are covered by the synthetic case only, and the glTF parser is not part of this path.
  std::vector<std::byte> clip;
  load_sample_hkx("TurretIdleWeapReady.hkx", clip);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> mounted;
  load_sample_hkx("TurretMountedSkeleton.hkx", mounted);
  if (IsSkipped() || HasFatalFailure()) return;
  fo4::hkx::HkxPackfile skeleton_pack;
  ASSERT_EQ(fo4::hkx::read_packfile(mounted, skeleton_pack), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(skeleton_pack, skeleton), fo4::hkx::HkxStatus::Ok);

  fo4::hkx::HkxAnimation was;
  fo4::hkx::HkxGltfImport own;
  ASSERT_TRUE(fo4::test::hkx::import_of_clip(clip, skeleton.bone_names.size(), was, own))
      << "the clip does not decode, bind to the skeleton or carry one root-motion sample per frame";
  fo4::hkx::HkxGltfSaveOptions options;
  options.replace_annotations = true;
  options.replace_motion = true;
  fo4::hkx::HkxGltfSave saved;
  ASSERT_EQ(fo4::hkx::save_gltf_animation(clip, skeleton, own, options, saved), fo4::hkx::HkxGltfSaveStatus::Ok)
      << saved.report.message;

  fo4::hkx::HkxPackfile original;
  fo4::hkx::HkxPackfile reopened;
  ASSERT_EQ(fo4::hkx::read_packfile(clip, original), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(fo4::hkx::read_packfile(saved.bytes, reopened), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(reopened.objects.size(), original.objects.size());
  fo4::hkx::HkxAnimation now;
  ASSERT_EQ(fo4::hkx::read_animation(reopened, now), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(now.kind, was.kind);
  fo4::test::hkx::ClipGap gap;
  ASSERT_TRUE(fo4::test::hkx::clip_gap(was, now, gap));
  EXPECT_LE(gap.position, options.max_position_error);
  EXPECT_LE(gap.rotation, options.max_rotation_error);
  EXPECT_LE(gap.scale, options.max_scale_error);
  std::vector<fo4::hkx::HkxAnimationAnnotation> notes;
  for (const auto& track : now.annotation_tracks) notes.insert(notes.end(), track.annotations.begin(), track.annotations.end());
  ASSERT_EQ(notes.size(), own.annotations.size());
  for (std::size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].time, own.annotations[i].time);
    EXPECT_EQ(notes[i].text, own.annotations[i].text);
  }
  fo4::hkx::HkxAnimationBinding was_binding;
  fo4::hkx::HkxAnimationBinding now_binding;
  ASSERT_EQ(fo4::hkx::read_binding(original, was_binding), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(fo4::hkx::read_binding(reopened, now_binding), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(now_binding.transform_track_to_bone_indices, was_binding.transform_track_to_bone_indices);
  EXPECT_EQ(now_binding.original_skeleton_name, was_binding.original_skeleton_name);
  fo4::hkx::HkxExtractedMotion motion;
  ASSERT_EQ(fo4::hkx::read_extracted_motion(reopened, motion), fo4::hkx::HkxStatus::Ok);
  if (motion.kind == fo4::hkx::HkxMotionKind::Default) { EXPECT_EQ(motion.samples, own.motion.samples); }
  EXPECT_EQ(fo4::hkx::repack_check(saved.bytes).verdict, fo4::hkx::HkxRepackVerdict::Equal);
  RecordProperty("gltf_noop_sha256", fo4::io::sha256_hex(saved.bytes));
}

TEST(HkxPhysicsReal, TheCommittedTurretRagdollsReadWithTheirMeasuredCounts) {
  // Constraint: detects a ragdoll reader that sizes bodies, bones or constraints from the shape count, which a polytope-only turret rig shows (6 shapes for 4 bodies).
  struct Expected {
    const char* file;
    std::size_t bodies;
    std::int32_t polytopes;
  };
  for (const Expected expected :
       {Expected{"TurretMountedSkeleton.hkx", 4, 6}, Expected{"TurretStandingSkeleton.hkx", 8, 8}}) {
    SCOPED_TRACE(expected.file);
    std::vector<std::byte> source;
    load_sample_hkx(expected.file, source);
    if (IsSkipped() || HasFatalFailure()) return;
    fo4::hkx::HkxRagdollModel model;
    fo4::hkx::HkxPhysicsLoadReport report;
    ASSERT_EQ(fo4::hkx::read_ragdoll(source, model, report), fo4::hkx::HkxStatus::Ok);
    EXPECT_EQ(report.count_of("hknpConvexPolytopeShape"), expected.polytopes);
    EXPECT_EQ(model.bodies.size(), expected.bodies);
    EXPECT_EQ(model.bone_bindings.size(), expected.bodies);
    EXPECT_EQ(model.constraints.size() + 1, model.bodies.size());
  }
}

TEST(HkxGltfSaveReal, ReplacesTheCommittedTurretClipFromAGlbAndReadsItBack) {
  // Constraint: detects a glTF save that only works on synthetic templates: the real spline clip, binding, motion frame and eight-track map must come back as one clip on the same skeleton. A smoke test: the glb is generated, so it is no proof of axis conventions.
  std::vector<std::byte> clip;
  load_sample_hkx("TurretIdleWeapReady.hkx", clip);
  if (IsSkipped() || HasFatalFailure()) return;
  std::vector<std::byte> mounted;
  load_sample_hkx("TurretMountedSkeleton.hkx", mounted);
  if (IsSkipped() || HasFatalFailure()) return;

  fo4::hkx::HkxPackfile pack;
  ASSERT_EQ(fo4::hkx::read_packfile(mounted, pack), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxSkeleton skeleton;
  ASSERT_EQ(fo4::hkx::read_skeleton(pack, skeleton), fo4::hkx::HkxStatus::Ok);
  ASSERT_EQ(skeleton.bone_names.size(), 8u);
  const auto lid = std::find(skeleton.bone_names.begin(), skeleton.bone_names.end(), "Lid");
  ASSERT_NE(lid, skeleton.bone_names.end());

  const auto glb =
      fo4::test::hkx::turret_animation_glb(skeleton, static_cast<std::size_t>(lid - skeleton.bone_names.begin()));
  fo4::hkx::HkxGltfImport imported;
  ASSERT_EQ(fo4::hkx::import_gltf_animation(glb, skeleton, {}, imported), fo4::hkx::HkxGltfStatus::Ok)
      << imported.message;
  fo4::hkx::HkxGltfSave saved;
  ASSERT_EQ(fo4::hkx::save_gltf_animation(clip, skeleton, imported, {}, saved), fo4::hkx::HkxGltfSaveStatus::Ok)
      << saved.report.message;
  EXPECT_EQ(saved.report.tracks, 8u);
  EXPECT_EQ(saved.report.animated, std::vector<std::string>{"Lid"});
  EXPECT_EQ(saved.report.reference_tracks.size(), 7u);
  EXPECT_EQ(saved.report.skeleton_match, "match");
  EXPECT_TRUE(saved.report.binding_linked);

  fo4::hkx::HkxPackfile reread;
  ASSERT_EQ(fo4::hkx::read_packfile(saved.bytes, reread), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimation decoded;
  ASSERT_EQ(fo4::hkx::read_animation(reread, decoded), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(decoded.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(decoded.tracks.size(), 8u);
  EXPECT_EQ(decoded.frame_count, imported.animation.frame_count);
  EXPECT_EQ(fo4::hkx::repack_check(saved.bytes).verdict, fo4::hkx::HkxRepackVerdict::Equal);
}

namespace {

std::size_t annotation_total(const fo4::hkx::HkxClipEdit& clip) {
  std::size_t total = 0;
  for (const auto& track : clip.animation.annotation_tracks) total += track.annotations.size();
  return total;
}

float travel_length(const std::array<float, 4>& from, const std::array<float, 4>& to) {
  const double x = static_cast<double>(to[0]) - from[0];
  const double y = static_cast<double>(to[1]) - from[1];
  const double z = static_cast<double>(to[2]) - from[2];
  return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

void open_vanilla_turn_clip(std::vector<std::byte>& source, fo4::hkx::HkxClipEdit& clip) {
  load_vanilla_hkx(std::filesystem::path{"Actors"} / "RadRoach" / "Animations" / "TurnLeft90.hkx",
                   "meshes/actors/radroach/animations/turnleft90.hkx", source);
  if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(fo4::hkx::open_clip_edit(source, clip), fo4::hkx::HkxEditStatus::Ok);
  ASSERT_EQ(clip.animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  ASSERT_EQ(clip.animation.frame_count, 19u);
  ASSERT_EQ(clip.motion.kind, fo4::hkx::HkxMotionKind::Default);
  ASSERT_EQ(clip.motion.samples.size(), 19u);
  ASSERT_EQ(annotation_total(clip), 4u);
}

}  // namespace

TEST(HkxClipEditReal, TrimOfAVanillaTurnClipRebasesItsTravelAndASavedCopyReadsBackTheSame) {
  // Constraint: detects a clip edit that refuses a shipped spline clip with root motion, mis-rebases its travel or turn, or loses annotations, motion or the binding in the saved file.
  std::vector<std::byte> source;
  fo4::hkx::HkxClipEdit clip;
  ASSERT_NO_FATAL_FAILURE(open_vanilla_turn_clip(source, clip));
  if (IsSkipped()) return;
  EXPECT_GT(std::fabs(clip.motion.samples.back()[3] - clip.motion.samples.front()[3]), 0.5f);
  const fo4::hkx::HkxClipEdit original = clip;

  fo4::hkx::HkxEditReport report;
  ASSERT_EQ(fo4::hkx::trim_clip(clip, 4, 14, &report), fo4::hkx::HkxEditStatus::Ok);
  EXPECT_EQ(clip.animation.frame_count, 11u);
  EXPECT_EQ(report.annotations_dropped, 1u);
  EXPECT_EQ(annotation_total(clip), 3u);
  ASSERT_EQ(clip.motion.samples.size(), 11u);
  for (std::size_t lane = 0; lane < 4; ++lane) EXPECT_NEAR(clip.motion.samples[0][lane], 0.0f, 1e-6f);
  EXPECT_NEAR(travel_length({}, clip.motion.samples.back()),
              travel_length(original.motion.samples[4], original.motion.samples[14]), 1e-3f);
  EXPECT_NEAR(clip.motion.samples.back()[3], original.motion.samples[14][3] - original.motion.samples[4][3], 1e-5f);
  EXPECT_NEAR(clip.motion.duration, clip.animation.duration, 1e-5f);

  fo4::hkx::HkxSaveOptions plain;
  plain.spline = false;
  std::vector<std::byte> bytes;
  ASSERT_EQ(fo4::hkx::save_clip_edit(source, clip, plain, bytes), fo4::hkx::HkxEditStatus::Ok);
  fo4::hkx::HkxClipEdit saved;
  ASSERT_EQ(fo4::hkx::open_clip_edit(bytes, saved), fo4::hkx::HkxEditStatus::Ok);
  EXPECT_EQ(saved.animation.frame_count, 11u);
  EXPECT_EQ(saved.animation.tracks.size(), original.animation.tracks.size());
  EXPECT_EQ(annotation_total(saved), 3u);
  EXPECT_EQ(saved.motion.samples, clip.motion.samples);
  EXPECT_EQ(saved.motion.duration, clip.motion.duration);
  for (std::size_t t = 0; t < saved.animation.tracks.size(); ++t) {
    for (std::size_t f = 0; f < saved.animation.frame_count; ++f) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR(saved.animation.tracks[t].translations[f][axis], clip.animation.tracks[t].translations[f][axis], 1e-5f);
      }
    }
  }
}

TEST(HkxClipEditReal, SplineSaveOfALoopedVanillaTurnClipKeepsTheSeamTravelAndBinding) {
  // Constraint: detects a spline save of a shipped clip that exceeds the default error budget, moves the loop seam or rewrites the root motion; UNVERIFIED until the data runner (#1731) runs it.
  std::vector<std::byte> source;
  fo4::hkx::HkxClipEdit clip;
  ASSERT_NO_FATAL_FAILURE(open_vanilla_turn_clip(source, clip));
  if (IsSkipped()) return;
  const fo4::hkx::HkxClipEdit original = clip;

  ASSERT_EQ(fo4::hkx::loop_clip(clip, 4), fo4::hkx::HkxEditStatus::Ok);
  EXPECT_EQ(clip.motion.samples, original.motion.samples);
  fo4::hkx::HkxSaveOptions options;
  options.spline = true;
  std::vector<std::byte> bytes;
  fo4::hkx::HkxEditReport report;
  ASSERT_EQ(fo4::hkx::save_clip_edit(source, clip, options, bytes, &report), fo4::hkx::HkxEditStatus::Ok);
  fo4::hkx::HkxClipEdit saved;
  ASSERT_EQ(fo4::hkx::open_clip_edit(bytes, saved), fo4::hkx::HkxEditStatus::Ok);
  EXPECT_EQ(saved.animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(saved.animation.frame_count, 19u);
  EXPECT_EQ(saved.motion.samples, original.motion.samples);
  EXPECT_EQ(annotation_total(saved), 4u);
  for (std::size_t t = 0; t < saved.animation.tracks.size(); ++t) {
    const auto& track = saved.animation.tracks[t];
    const std::array<float, 4> first{track.translations.front()[0], track.translations.front()[1], track.translations.front()[2], 0.0f};
    const std::array<float, 4> last{track.translations.back()[0], track.translations.back()[1], track.translations.back()[2], 0.0f};
    EXPECT_LT(travel_length(first, last), 0.05f) << "track " << t;
  }
}

TEST(HkxClipEditReal, SplineSaveOfATrimmedVanillaTurnClipKeepsFramesDurationAndNoteTexts) {
  std::vector<std::byte> source;
  fo4::hkx::HkxClipEdit clip;
  ASSERT_NO_FATAL_FAILURE(open_vanilla_turn_clip(source, clip));
  if (IsSkipped()) return;

  ASSERT_EQ(fo4::hkx::trim_clip(clip, 4, 14), fo4::hkx::HkxEditStatus::Ok);
  ASSERT_EQ(clip.animation.frame_count, 11u);
  ASSERT_EQ(annotation_total(clip), 3u);
  fo4::hkx::HkxSaveOptions options;
  options.spline = true;
  std::vector<std::byte> bytes;
  ASSERT_EQ(fo4::hkx::save_clip_edit(source, clip, options, bytes), fo4::hkx::HkxEditStatus::Ok);
  fo4::hkx::HkxClipEdit saved;
  ASSERT_EQ(fo4::hkx::open_clip_edit(bytes, saved), fo4::hkx::HkxEditStatus::Ok);
  EXPECT_EQ(saved.animation.kind, fo4::hkx::HkxAnimationKind::Spline);
  EXPECT_EQ(saved.animation.frame_count, 11u);
  EXPECT_NEAR(saved.animation.duration, clip.animation.duration, 1e-3f);
  EXPECT_EQ(saved.motion.samples, clip.motion.samples);
  ASSERT_EQ(annotation_total(saved), 3u);
  const auto texts = [](const fo4::hkx::HkxClipEdit& edit) {
    std::vector<std::string> out;
    for (const auto& track : edit.animation.annotation_tracks) {
      for (const auto& note : track.annotations) out.push_back(note.text);
    }
    return out;
  };
  EXPECT_EQ(texts(saved), texts(clip));
}

TEST(HkxWriterNoOpReal, ClipEditSaveWithNoEditReadsBackTheSameVanillaTurnClip) {
  // Constraint: detects a clip save, plain or spline, that drifts a shipped track past the budget, changes its root motion or notes, or loses its binding or a neighbouring object when nothing was edited; UNVERIFIED until the data runner (#1731) runs it.
  std::vector<std::byte> source;
  fo4::hkx::HkxClipEdit clip;
  ASSERT_NO_FATAL_FAILURE(open_vanilla_turn_clip(source, clip));
  if (IsSkipped()) return;
  fo4::hkx::HkxPackfile before;
  ASSERT_EQ(fo4::hkx::read_packfile(source, before), fo4::hkx::HkxStatus::Ok);
  fo4::hkx::HkxAnimationBinding was_binding;
  ASSERT_EQ(fo4::hkx::read_binding(before, was_binding), fo4::hkx::HkxStatus::Ok);

  for (const bool spline : {false, true}) {
    SCOPED_TRACE(spline ? "spline" : "plain");
    fo4::hkx::HkxSaveOptions options;
    options.spline = spline;
    std::vector<std::byte> bytes;
    ASSERT_EQ(fo4::hkx::save_clip_edit(source, clip, options, bytes), fo4::hkx::HkxEditStatus::Ok);
    fo4::hkx::HkxClipEdit saved;
    ASSERT_EQ(fo4::hkx::open_clip_edit(bytes, saved), fo4::hkx::HkxEditStatus::Ok);
    EXPECT_EQ(saved.animation.kind, spline ? fo4::hkx::HkxAnimationKind::Spline : fo4::hkx::HkxAnimationKind::Interleaved);
    EXPECT_EQ(saved.animation.frame_count, clip.animation.frame_count);
    EXPECT_NEAR(saved.animation.duration, clip.animation.duration, 1e-3f);
    fo4::test::hkx::ClipGap gap;
    ASSERT_TRUE(fo4::test::hkx::clip_gap(clip.animation, saved.animation, gap));
    EXPECT_LE(gap.position, options.max_position_error);
    EXPECT_LE(gap.rotation, options.max_rotation_error);
    EXPECT_LE(gap.scale, options.max_scale_error);
    EXPECT_EQ(saved.motion.samples, clip.motion.samples);
    EXPECT_EQ(saved.motion.duration, clip.motion.duration);
    ASSERT_EQ(saved.animation.annotation_tracks.size(), clip.animation.annotation_tracks.size());
    for (std::size_t t = 0; t < clip.animation.annotation_tracks.size(); ++t) {
      const auto& was = clip.animation.annotation_tracks[t];
      const auto& now = saved.animation.annotation_tracks[t];
      EXPECT_EQ(now.name, was.name);
      ASSERT_EQ(now.annotations.size(), was.annotations.size());
      for (std::size_t i = 0; i < was.annotations.size(); ++i) {
        EXPECT_EQ(now.annotations[i].time, was.annotations[i].time);
        EXPECT_EQ(now.annotations[i].text, was.annotations[i].text);
      }
    }
    fo4::hkx::HkxPackfile reopened;
    ASSERT_EQ(fo4::hkx::read_packfile(bytes, reopened), fo4::hkx::HkxStatus::Ok);
    EXPECT_EQ(reopened.objects.size(), before.objects.size());
    fo4::hkx::HkxAnimationBinding now_binding;
    ASSERT_EQ(fo4::hkx::read_binding(reopened, now_binding), fo4::hkx::HkxStatus::Ok);
    EXPECT_EQ(now_binding.transform_track_to_bone_indices, was_binding.transform_track_to_bone_indices);
    EXPECT_TRUE(now_binding.animation_offset.has_value());
    EXPECT_EQ(fo4::hkx::repack_check(bytes).verdict, fo4::hkx::HkxRepackVerdict::Equal);
    RecordProperty(spline ? "clip_noop_spline_sha256" : "clip_noop_plain_sha256", fo4::io::sha256_hex(bytes));
  }
}

namespace {

constexpr const char* k_turret_mesh = "Meshes/Actors/Turret/CharacterAssets/TurretMounted.nif";

}  // namespace

TEST(TurretNifReal, TheCommittedTurretMeshHasNoCollisionSystemAndYieldsNoBodies) {
  // Constraint: detects a collision reader that invents a body for a shipped mesh with no collision system.
  std::vector<std::byte> bytes;
  load_sample_hkx(k_turret_mesh, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const auto blobs = fo4::nif::read_collision_blobs(fo4::nif::read_nif(bytes));
  EXPECT_TRUE(blobs.empty());
  const auto plain = fo4::nif::build_plain_collision(blobs);
  EXPECT_TRUE(plain.bodies.empty());
  EXPECT_TRUE(plain.shapes.empty());
  EXPECT_EQ(plain.skipped_shapes, 0u);
  EXPECT_EQ(plain.undecodable_systems, 0u);
}

TEST(TurretNifReal, AWeightEditOnTheCommittedTurretMeshTouchesOnlyOneVertexSkinRecord) {
  // Constraint: detects a skin save that shifts bytes outside the vertex it edits on a real skinned shape.
  std::vector<std::byte> bytes;
  load_sample_hkx(k_turret_mesh, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const auto nif = fo4::nif::read_nif(bytes);
  std::optional<fo4::nif::Shape> skinned;
  for (auto& shape : fo4::nif::read_shapes(nif)) {
    if (shape.skin_ref >= 0 && shape.bones.size() >= 2 && !shape.weights.empty()) {
      skinned = std::move(shape);
      break;
    }
  }
  ASSERT_TRUE(skinned.has_value());
  const auto before = fo4::nif::influences_of(*skinned, 0);
  ASSERT_FALSE(before.empty());
  const auto first = before.front().bone;
  const auto second = before.size() > 1 ? before[1].bone : (first == 0 ? 1 : 0);
  const float heavy = std::fabs(before.front().weight - 0.75f) < 0.01f ? 0.5f : 0.75f;

  fo4::nif::SkinEditRequest request;
  request.kind = fo4::nif::SkinEditKind::set_influences;
  request.block = skinned->block;
  request.vertex = 0;
  request.influences = {{first, heavy}, {second, 1.0f - heavy}};
  fo4::nif::SkinEditReport report;
  std::vector<std::byte> edited;
  ASSERT_NO_THROW(edited = fo4::nif::apply_skin_edit(nif, request, &report));
  EXPECT_EQ(report.vertices_changed, 1u);

  const auto baseline = fo4::nif::write_nif(nif);
  if (baseline != bytes) GTEST_LOG_(WARNING) << "an unedited rewrite of the shipped mesh is not byte-identical to it; the edit is compared against the rewrite";
  ASSERT_EQ(edited.size(), baseline.size());
  std::size_t differing = 0;
  for (std::size_t at = 0; at < edited.size(); ++at) differing += edited[at] != baseline[at] ? 1u : 0u;
  EXPECT_GE(differing, 1u);
  EXPECT_LE(differing, 12u);

  const auto after = fo4::nif::read_shape(fo4::nif::read_nif(edited), skinned->block);
  const auto changed = fo4::nif::influences_of(after, 0);
  ASSERT_EQ(changed.size(), 2u);
  for (const auto& wanted : request.influences) {
    const auto found = std::find_if(changed.begin(), changed.end(), [&](const auto& item) { return item.bone == wanted.bone; });
    ASSERT_NE(found, changed.end());
    EXPECT_NEAR(found->weight, wanted.weight, 1.0e-3f);
  }
  ASSERT_EQ(after.weights.size(), skinned->weights.size());
  EXPECT_TRUE(std::equal(after.weights.begin() + 4, after.weights.end(), skinned->weights.begin() + 4));
  EXPECT_TRUE(std::equal(after.bone_indices.begin() + 4, after.bone_indices.end(), skinned->bone_indices.begin() + 4));
  EXPECT_EQ(after.verts, skinned->verts);
}
