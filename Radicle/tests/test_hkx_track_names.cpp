#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/track_names.hpp"
#include "hkx_clip_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>
#include <vector>

using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::HkxTrackNames;
using fo4::hkx::HkxTrackNameSource;
using fo4::hkx::find_sibling_skeleton;
using fo4::hkx::name_tracks;
using fo4::hkx::read_binding;
using fo4::hkx::read_packfile;
using namespace fo4::test::hkx;

namespace {

struct TempDir {
  std::filesystem::path root;
  TempDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root = std::filesystem::temp_directory_path() / ("radicle_track_names_" + std::to_string(id));
    std::filesystem::create_directories(root);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

struct Clip {
  std::vector<std::byte> bytes;
  HkxPackfile packfile;
  HkxAnimationBinding binding;
};

Clip load_clip(bool with_skeleton) {
  Clip clip;
  clip.bytes = clip_packfile(with_skeleton);
  EXPECT_EQ(read_packfile(clip.bytes, clip.packfile), HkxStatus::Ok);
  EXPECT_EQ(read_binding(clip.packfile, clip.binding), HkxStatus::Ok);
  return clip;
}

}  // namespace

TEST(HkxTrackNames, TracksAreNamedFromTheEmbeddedSkeletonThroughTheBindingMap) {
  const TempDir dir;
  const Clip clip = load_clip(true);
  const HkxTrackNames names = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2);
  EXPECT_EQ(names.source, HkxTrackNameSource::Embedded);
  EXPECT_EQ(names.names, (std::vector<std::string>{"Root", "Head"}));
  EXPECT_TRUE(names.skeleton.empty());
}

TEST(HkxTrackNames, WithNoSkeletonAnywhereEveryTrackIsNamedByItsIndex) {
  const TempDir dir;
  const Clip clip = load_clip(false);
  const HkxTrackNames names = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2);
  EXPECT_EQ(names.source, HkxTrackNameSource::None);
  EXPECT_EQ(names.names, (std::vector<std::string>{"track 0", "track 1"}));
}

TEST(HkxTrackNames, ATrackBeyondTheBindingMapKeepsItsIndexName) {
  const TempDir dir;
  const Clip clip = load_clip(true);
  const HkxTrackNames names = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 4);
  EXPECT_EQ(names.names, (std::vector<std::string>{"Root", "Head", "track 2", "track 3"}));
}

TEST(HkxTrackNames, ABoneIndexOutsideTheSkeletonKeepsTheIndexName) {
  const TempDir dir;
  Clip clip = load_clip(true);
  clip.binding.transform_track_to_bone_indices = {1, 9};
  const HkxTrackNames names = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2);
  EXPECT_EQ(names.names, (std::vector<std::string>{"Spine", "track 1"}));
  clip.binding.transform_track_to_bone_indices = {-1, 2};
  EXPECT_EQ(name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2).names,
            (std::vector<std::string>{"track 0", "Head"}));
}

TEST(HkxTrackNames, AnExplicitSkeletonIsUsedBeforeTheEmbeddedOne) {
  const TempDir dir;
  const Clip clip = load_clip(true);
  write_bytes(dir.root / "rig.hkx", clip_packfile(true));
  const HkxTrackNames names = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2, dir.root / "rig.hkx");
  EXPECT_EQ(names.source, HkxTrackNameSource::Explicit);
  EXPECT_EQ(names.skeleton, dir.root / "rig.hkx");
  EXPECT_EQ(names.names, (std::vector<std::string>{"Root", "Head"}));
}

TEST(HkxTrackNames, AnUnreadableExplicitSkeletonFallsThroughInsteadOfFailing) {
  const TempDir dir;
  const Clip clip = load_clip(true);
  write_text(dir.root / "bad.hkx", "not a packfile");
  const HkxTrackNames missing = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2, dir.root / "none.hkx");
  EXPECT_EQ(missing.source, HkxTrackNameSource::Embedded);
  const HkxTrackNames garbage = name_tracks(dir.root / "clip.hkx", clip.packfile, clip.binding, 2, dir.root / "bad.hkx");
  EXPECT_EQ(garbage.source, HkxTrackNameSource::Embedded);
  EXPECT_EQ(garbage.names, (std::vector<std::string>{"Root", "Head"}));
}

TEST(HkxTrackNames, ASiblingSkeletonInCharacterAssetsNamesAClipThatCarriesNone) {
  const TempDir dir;
  const Clip clip = load_clip(false);
  const auto animation = dir.root / "Actors" / "Dog" / "Animations" / "Idle" / "clip.hkx";
  write_bytes(animation, clip.bytes);
  write_bytes(dir.root / "Actors" / "Dog" / "CharacterAssets" / "skeleton.hkx", clip_packfile(true));
  const HkxTrackNames names = name_tracks(animation, clip.packfile, clip.binding, 2);
  EXPECT_EQ(names.source, HkxTrackNameSource::Sibling);
  EXPECT_EQ(names.names, (std::vector<std::string>{"Root", "Head"}));
  const auto found = find_sibling_skeleton(animation);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->filename().string(), "skeleton.hkx");
}

TEST(HkxTrackNames, TheSiblingSearchSkipsFilesThatAreNotSkeletons) {
  const TempDir dir;
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip_packfile(false));
  write_text(dir.root / "Dog" / "CharacterAssets" / "a_bad.hkx", "junk");
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "b_rig.hkx", clip_packfile(true));
  const auto found = find_sibling_skeleton(animation);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->filename().string(), "b_rig.hkx");
}

TEST(HkxTrackNames, WithoutACharacterAssetsFolderTheSearchTriesTheClipsOwnFolderButNotTheClipItself) {
  const TempDir dir;
  const auto animation = dir.root / "loose" / "clip.hkx";
  write_bytes(animation, clip_packfile(true));
  EXPECT_FALSE(find_sibling_skeleton(animation).has_value());
  write_bytes(dir.root / "loose" / "rig.hkx", clip_packfile(true));
  const auto found = find_sibling_skeleton(animation);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->filename().string(), "rig.hkx");
}

TEST(HkxTrackNames, AClipInAMissingFolderHasNoSibling) {
  const TempDir dir;
  EXPECT_FALSE(find_sibling_skeleton(dir.root / "gone" / "clip.hkx").has_value());
}

// Constraint: detects a sibling search that has no candidate cap and so reads every file of a huge folder on each inspect.
TEST(HkxTrackNames, TheSiblingSearchGivesUpAfterThirtyTwoCandidates) {
  const TempDir dir;
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip_packfile(false));
  for (int i = 0; i < 31; ++i) {
    write_text(dir.root / "Dog" / "CharacterAssets" / ("a" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".hkx"), "junk");
  }
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "b_rig.hkx", clip_packfile(true));
  ASSERT_TRUE(find_sibling_skeleton(animation).has_value());
  write_text(dir.root / "Dog" / "CharacterAssets" / "a31.hkx", "junk");
  EXPECT_FALSE(find_sibling_skeleton(animation).has_value());
}

// Constraint: detects a sibling search that follows a link out of the character folder.
TEST(HkxTrackNames, TheSiblingSearchSkipsALinkedSkeleton) {
  const TempDir dir;
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip_packfile(false));
  write_bytes(dir.root / "Other" / "rig.hkx", clip_packfile(true));
  std::filesystem::create_directories(dir.root / "Dog" / "CharacterAssets");
  std::error_code ec;
  std::filesystem::create_symlink(dir.root / "Other" / "rig.hkx", dir.root / "Dog" / "CharacterAssets" / "rig.hkx", ec);
  if (ec) GTEST_SKIP() << "symlinks are not available here: " << ec.message();
  EXPECT_FALSE(find_sibling_skeleton(animation).has_value());
}

// Constraint: detects a sibling skeleton with too few bones for the clip's track map being applied, which names tracks after the wrong bones.
TEST(HkxTrackNames, ASiblingSkeletonTooSmallForTheBindingIsSkippedForTheNextOne) {
  const TempDir dir;
  const Clip clip = load_clip(false);
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip.bytes);
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "a_small.hkx", clip_packfile(true, {-1, 0}));
  EXPECT_FALSE(find_sibling_skeleton(animation, &clip.binding).has_value());
  EXPECT_EQ(name_tracks(animation, clip.packfile, clip.binding, 2).names, (std::vector<std::string>{"track 0", "track 1"}));
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "b_full.hkx", clip_packfile(true));
  const auto found = find_sibling_skeleton(animation, &clip.binding);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->filename().string(), "b_full.hkx");
  EXPECT_EQ(name_tracks(animation, clip.packfile, clip.binding, 2).names, (std::vector<std::string>{"Root", "Head"}));
}

// Constraint: detects a sibling skeleton with enough bones but a different name than the clip's original skeleton, which would attach another rig's bone names.
TEST(HkxTrackNames, ASiblingSkeletonNamedForADifferentRigIsRejectedWhileTheMatchingOneResolves) {
  const TempDir dir;
  Clip clip = load_clip(false);
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip.bytes);
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "rig.hkx", clip_packfile(true));
  clip.binding.original_skeleton_name = "OtherRig";
  EXPECT_FALSE(find_sibling_skeleton(animation, &clip.binding).has_value());
  EXPECT_EQ(name_tracks(animation, clip.packfile, clip.binding, 2).names, (std::vector<std::string>{"track 0", "track 1"}));
  clip.binding.original_skeleton_name = "rig";
  const auto found = find_sibling_skeleton(animation, &clip.binding);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->filename().string(), "rig.hkx");
  EXPECT_EQ(name_tracks(animation, clip.packfile, clip.binding, 2).names, (std::vector<std::string>{"Root", "Head"}));
}

// Constraint: detects a sibling search that opens files the caller's path policy refuses.
TEST(HkxTrackNames, TheSiblingSearchNeverOpensAFileTheReadFilterRefuses) {
  const TempDir dir;
  const Clip clip = load_clip(false);
  const auto animation = dir.root / "Dog" / "Animations" / "clip.hkx";
  write_bytes(animation, clip.bytes);
  write_bytes(dir.root / "Dog" / "CharacterAssets" / "rig.hkx", clip_packfile(true));
  std::vector<std::filesystem::path> asked;
  const auto refuse = [&asked](const std::filesystem::path& file) {
    asked.push_back(file);
    return false;
  };
  EXPECT_FALSE(find_sibling_skeleton(animation, &clip.binding, refuse).has_value());
  EXPECT_EQ(asked.size(), 1u);
  const HkxTrackNames names = name_tracks(animation, clip.packfile, clip.binding, 2, {}, refuse);
  EXPECT_EQ(names.source, HkxTrackNameSource::None);
  EXPECT_TRUE(find_sibling_skeleton(animation, &clip.binding, [](const std::filesystem::path&) { return true; }).has_value());
}
