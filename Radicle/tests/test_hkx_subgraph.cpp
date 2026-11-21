#include "fo4/hkx/subgraph.hpp"

#include "fo4/archive/assets.hpp"
#include "ba2_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> ztext(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  }
  out.push_back(std::byte{0});
  return out;
}

fo4::Record race_record(const std::vector<std::string>& graphs,
                        const std::vector<std::string>& prefixes) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("RACE");
  for (const auto& graph : graphs) {
    record.subrecords.push_back(fo4::Subrecord{fo4::Signature::from_string("SGNM"), ztext(graph)});
  }
  for (const auto& prefix : prefixes) {
    record.subrecords.push_back(fo4::Subrecord{fo4::Signature::from_string("SAPT"), ztext(prefix)});
  }
  return record;
}

std::string manifest_with_paths(const std::string& id, const std::vector<std::string>& paths) {
  std::string text = "V4\n0\n" + id + "\n0\n";
  for (const auto& path : paths) {
    text += path;
    text += '\n';
  }
  return text;
}

}

// Constraint: each shipped race record must reproduce its own manifest id, so a wrong CRC parameter or a lost case/slash normalisation cannot resolve a crash hash.
TEST(SubgraphHash, RecomputesEveryShippedSubgraphIdFromItsRaceRecord) {
  struct Known {
    std::uint64_t id;
    std::vector<std::string> graphs;
    std::vector<std::string> prefixes;
  };
  const std::vector<Known> known{
      {10063049680076617785ull,
       {"Actors\\Shared\\Behaviors\\BloodbugSharedCoreWrappingBehavior.hkx"},
       {"Actors\\Mosquito\\Animations\\Injured", "Actors\\Mosquito\\Animations"}},
      {10121085405136975821ull,
       {"Actors\\Character\\Behaviors\\MTBehavior.hkx"},
       {"Actors\\Character\\Animations\\MT\\Injured\\Right",
        "Actors\\Character\\Animations\\MT\\Player",
        "Actors\\Character\\Animations\\MT\\Neutral", "Actors\\Character\\Animations",
        "actors\\Character\\Animations\\Furniture\\Water"}},
      {10669390995239814013ull,
       {"Actors\\Vertibird\\Behaviors\\FlightBehavior.hkx"},
       {"Actors\\Vertibird\\Animations\\InjuredLeftWing", "Actors\\Vertibird\\Animations"}},
      {10715738121632629791ull,
       {"Actors\\Shared\\Behaviors\\MirelurkQueenCoreWrappingBehavior.hkx"},
       {"Actors\\MirelurkQueen\\Animations\\Injured\\RightLegs",
        "Actors\\MirelurkQueen\\Animations\\Injured\\Arms\\LeftArm",
        "Actors\\MirelurkQueen\\Animations"}},
      {11081849303742838364ull,
       {"Actors\\Character\\Behaviors\\LeftArmInjuredMeleeWrappingBehavior.hkx"},
       {"Actors\\Character\\Animations\\H2H", "Actors\\Character\\Animations\\1HM",
        "Actors\\Character\\Animations\\Paired", "Actors\\Character\\Animations"}},
  };

  for (const auto& entry : known) {
    const auto set = fo4::hkx::race_animation_set(race_record(entry.graphs, entry.prefixes));
    ASSERT_EQ(set.subgraphs.size(), 1u) << entry.graphs.front();
    EXPECT_EQ(set.subgraphs.front().id, entry.id) << entry.graphs.front();
  }
}

// Constraint: the low half must be the behaviour path's CRC alone, or every per-weapon lookup breaks.
TEST(SubgraphHash, LowHalfIsTheBehaviourPathCrc) {
  EXPECT_EQ(fo4::hkx::subgraph_behavior_half("Actors\\Character\\Behaviors\\WeaponBehavior.hkx"),
            0xa6b50f9eu);
  EXPECT_EQ(fo4::hkx::subgraph_behavior_half(
                "Actors\\Character\\Behaviors\\RightArmInjuredWeaponWrappingBehavior.hkx"),
            0x7eff6266u);
  EXPECT_EQ(fo4::hkx::subgraph_behavior_half(
                "Actors\\Character\\Behaviors\\BothArmInjuredWeaponWrappingBehavior.hkx"),
            0x85c72449u);
  EXPECT_EQ(fo4::hkx::subgraph_behavior_half(
                "Actors\\Character\\Behaviors\\LeftArmInjuredWeaponWrappingBehavior.hkx"),
            0x8b1f2debu);
}

// Constraint: a record naming several graphs must report its lists and no id, so an invented id cannot name a manifest.
TEST(SubgraphHash, ARecordWithSeveralBehaviorGraphsReportsItsListsButNoId) {
  const auto set = fo4::hkx::race_animation_set(
      race_record({"Actors\\A\\Behaviors\\One.hkx", "Actors\\A\\Behaviors\\Two.hkx"},
                  {"Actors\\A\\Animations"}));

  EXPECT_EQ(set.behavior_graphs.size(), 2u);
  ASSERT_EQ(set.path_prefixes.size(), 1u);
  EXPECT_EQ(set.path_prefixes.front(), "Actors\\A\\Animations");
  EXPECT_TRUE(set.subgraphs.empty());
}

// Constraint: a non-RACE record and an empty SGNM must not count as a graph, or the pairing shifts.
TEST(SubgraphHash, IgnoresRecordsThatAreNotRacesAndEmptyGraphNames) {
  auto not_a_race = race_record({"Actors\\A\\Behaviors\\One.hkx"}, {"Actors\\A\\Animations"});
  not_a_race.header.signature = fo4::Signature::from_string("NPC_");
  const auto skipped = fo4::hkx::race_animation_set(not_a_race);
  EXPECT_TRUE(skipped.behavior_graphs.empty());
  EXPECT_TRUE(skipped.subgraphs.empty());

  const auto empty_name = fo4::hkx::race_animation_set(
      race_record({"", "Actors\\A\\Behaviors\\One.hkx"}, {"Actors\\A\\Animations"}));
  ASSERT_EQ(empty_name.subgraphs.size(), 1u);
  EXPECT_EQ(empty_name.subgraphs.front().behavior_graph, "Actors\\A\\Behaviors\\One.hkx");
}

// Constraint: the id must come from the third line and the paths from the fifth, or the resolved subgraph names the wrong graph.
TEST(AnimationFileData, ReadsTheGamesManifestShape) {
  const std::string text = manifest_with_paths(
      "12345",
      {"Actors\\Character\\Behaviors\\MTBehavior.hkx", "Actors\\Character\\Animations\\MT\\Player",
       "Actors\\Character\\Animations"});

  const auto parsed = fo4::hkx::parse_animation_file_data(text);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->id, 12345u);
  ASSERT_EQ(parsed->behavior_paths.size(), 1u);
  EXPECT_EQ(parsed->behavior_paths.front(), "Actors\\Character\\Behaviors\\MTBehavior.hkx");
  ASSERT_EQ(parsed->animation_paths.size(), 2u);
  EXPECT_EQ(parsed->animation_paths.front(), "Actors\\Character\\Animations\\MT\\Player");
}

// Constraint: a manifest that names no \Behaviors\ path must still take its first path as the behaviour.
TEST(AnimationFileData, TreatsItsFirstPathAsTheBehaviorWhenNoneIsNamed) {
  const auto parsed =
      fo4::hkx::parse_animation_file_data(manifest_with_paths("7", {"A\\One", "A\\Two"}));

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->behavior_paths.size(), 1u);
  EXPECT_EQ(parsed->behavior_paths.front(), "A\\One");
  ASSERT_EQ(parsed->animation_paths.size(), 1u);
  EXPECT_EQ(parsed->animation_paths.front(), "A\\Two");
}

// Constraint: a short file, a non-numeric id line or a path list with no behaviour must be refused instead of yielding id 0.
TEST(AnimationFileData, RefusesTextThatIsNotAManifest) {
  EXPECT_FALSE(fo4::hkx::parse_animation_file_data("V4\n0\n").has_value());
  EXPECT_FALSE(fo4::hkx::parse_animation_file_data("V4\n0\nnot-a-number\n0\nA\\One\n").has_value());
  EXPECT_FALSE(fo4::hkx::parse_animation_file_data("V4\n0\n9\n0\n\n").has_value());
}

// Constraint: the hint must be the first path at the offset a V4 dump declares.
TEST(AnimationOffsets, HintIsTheFirstV4Path) {
  const std::string_view path = "Actors\\Character\\Behaviors\\WeaponBehavior.hkx";
  std::vector<std::byte> dump{std::byte{'V'}, std::byte{'4'}, std::byte{'\n'},
                              static_cast<std::byte>(path.size())};
  for (const char ch : path) dump.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  dump.push_back(std::byte{0});

  const auto hint = fo4::hkx::parse_animation_offset_hint(dump);
  ASSERT_TRUE(hint.has_value());
  EXPECT_EQ(*hint, path);
}

// Constraint: bytes that are not a V4 dump, or a length that runs past the buffer, must be refused rather than read.
TEST(AnimationOffsets, RefusesBytesThatAreNotAV4Dump) {
  const std::vector<std::byte> other{std::byte{'X'}, std::byte{'4'}, std::byte{'\n'}, std::byte{4},
                                     std::byte{'a'}};
  EXPECT_FALSE(fo4::hkx::parse_animation_offset_hint(other).has_value());
  EXPECT_FALSE(fo4::hkx::parse_animation_offset_hint(std::span<const std::byte>{}).has_value());

  const std::vector<std::byte> short_path{std::byte{'V'}, std::byte{'4'}, std::byte{'\n'},
                                          std::byte{9}, std::byte{'a'}, std::byte{0}, std::byte{0},
                                          std::byte{0}};
  EXPECT_FALSE(fo4::hkx::parse_animation_offset_hint(short_path).has_value());
}

namespace {

constexpr std::uint64_t k_manifest_id = 10067747564108604006ull;

void write_loose(const fo4::test::TempDataDir& dir, const std::string& rel, std::string_view text) {
  const auto file = dir.path() / rel;
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out << text;
}

std::filesystem::path write_archive(const fo4::test::TempDataDir& dir, const std::string& name,
                                    std::vector<ba2_fixture::Ba2GeneralEntry> entries) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  archive.entries = std::move(entries);
  const auto path = dir.path() / name;
  fo4::archive::write_ba2_general_file(archive, path);
  return path;
}

fo4::archive::AssetResolver open_assets(const fo4::test::TempDataDir& dir,
                                        const std::vector<std::filesystem::path>& archives) {
  const std::vector<std::filesystem::path> roots{dir.path()};
  return fo4::archive::AssetResolver::open_roots(roots, archives);
}

}  // namespace

// Constraint: the tool reports this exact text as the manifest path, so the separators and case must not change.
TEST(SubgraphManifest, PathIsLowercaseBackslashBelowMeshes) {
  EXPECT_EQ(fo4::hkx::subgraph_manifest_path(k_manifest_id),
            "meshes\\animtextdata\\animationfiledata\\10067747564108604006.txt");
}

// Constraint: a loose manifest must beat an archive entry of the same path, or a mod's edit is hidden.
TEST(SubgraphManifest, LooseFileWinsOverAnArchive) {
  fo4::test::TempDataDir dir;
  write_loose(dir, "Meshes/AnimTextData/AnimationFileData/10067747564108604006.txt", "loose text");
  const auto archive = write_archive(
      dir, "Meshes.ba2", {ba2_fixture::general("meshes\\animtextdata\\animationfiledata\\10067747564108604006.txt", "archive text")});
  const auto assets = open_assets(dir, {archive});

  const auto manifest = fo4::hkx::read_subgraph_manifest(assets, k_manifest_id);
  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->text, "loose text");
  EXPECT_EQ(manifest->source, "loose");
}

// Constraint: an archive-only manifest must be read and its source must be the archive file name.
TEST(SubgraphManifest, ReadsFromAnArchiveAndNamesIt) {
  fo4::test::TempDataDir dir;
  const auto archive = write_archive(
      dir, "Fallout4 - Meshes.ba2",
      {ba2_fixture::general("meshes\\animtextdata\\animationfiledata\\10067747564108604006.txt", "archive text", true)});
  const auto assets = open_assets(dir, {archive});

  const auto manifest = fo4::hkx::read_subgraph_manifest(assets, k_manifest_id);
  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->text, "archive text");
  EXPECT_EQ(manifest->source, "Fallout4 - Meshes.ba2");
}

// Constraint: the last archive must win, as the tool passes its .ba2 files sorted by descending path so the first by path wins.
TEST(SubgraphManifest, LaterArchiveOverridesAnEarlierOne) {
  fo4::test::TempDataDir dir;
  const std::string inner = "meshes\\animtextdata\\animationfiledata\\10067747564108604006.txt";
  const auto first = write_archive(dir, "A.ba2", {ba2_fixture::general(inner, "from a")});
  const auto second = write_archive(dir, "B.ba2", {ba2_fixture::general(inner, "from b")});
  const auto assets = open_assets(dir, {first, second});

  const auto manifest = fo4::hkx::read_subgraph_manifest(assets, k_manifest_id);
  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->text, "from b");
  EXPECT_EQ(manifest->source, "B.ba2");
}

TEST(SubgraphManifest, MissingManifestIsNullopt) {
  fo4::test::TempDataDir dir;
  write_loose(dir, "Meshes/AnimTextData/AnimationFileData/1.txt", "other id");
  const auto assets = open_assets(dir, {});
  EXPECT_FALSE(fo4::hkx::read_subgraph_manifest(assets, k_manifest_id).has_value());
}

// Constraint: a folder at the manifest path must read as no manifest and must not throw.
TEST(SubgraphManifest, FolderAtTheManifestPathIsNotAManifest) {
  fo4::test::TempDataDir dir;
  std::filesystem::create_directories(dir.path() / "Meshes/AnimTextData/AnimationFileData/10067747564108604006.txt");
  const auto assets = open_assets(dir, {});
  EXPECT_FALSE(fo4::hkx::read_subgraph_manifest(assets, k_manifest_id).has_value());
}

// Constraint: an extension-less manifest line must find "<line>.hkx", loose or archived.
TEST(AnimationExists, FindsAFileWithOrWithoutTheExtensionLooseOrArchived) {
  fo4::test::TempDataDir dir;
  write_loose(dir, "Meshes/Actors/Character/Animations/MT/Neutral.hkx", "x");
  write_loose(dir, "Meshes/Actors/Character/Animations/MT/Exact.hkx", "x");
  const auto archive = write_archive(
      dir, "Anim.ba2", {ba2_fixture::general("meshes\\actors\\character\\animations\\mt\\packed.hkx", "x")});
  const auto assets = open_assets(dir, {archive});

  EXPECT_TRUE(fo4::hkx::animation_exists(assets, "Actors\\Character\\Animations\\MT\\Neutral"));
  EXPECT_TRUE(fo4::hkx::animation_exists(assets, "Actors\\Character\\Animations\\MT\\Exact.hkx"));
  EXPECT_TRUE(fo4::hkx::animation_exists(assets, "Actors/Character/Animations/MT/PACKED"));
  EXPECT_TRUE(fo4::hkx::animation_exists(assets, "Actors\\Character\\Animations\\MT\\Packed.hkx"));
}

// Constraint: a missing animation, a folder of its name and an unsafe path must read as absent without throwing.
TEST(AnimationExists, RefusesMissingFoldersAndUnsafePaths) {
  fo4::test::TempDataDir dir;
  std::filesystem::create_directories(dir.path() / "Meshes/Actors/Character/Animations/MT/Neutral");
  write_loose(dir, "Meshes/Actors/Outside.hkx", "x");
  const auto assets = open_assets(dir, {});

  EXPECT_FALSE(fo4::hkx::animation_exists(assets, "Actors\\Character\\Animations\\MT\\Neutral"));
  EXPECT_FALSE(fo4::hkx::animation_exists(assets, "Actors\\Character\\Animations\\MT\\Missing"));
  EXPECT_FALSE(fo4::hkx::animation_exists(assets, "Actors\\Character\\..\\Outside"));
  EXPECT_FALSE(fo4::hkx::animation_exists(assets, "C:\\Outside"));
  EXPECT_FALSE(fo4::hkx::animation_exists(assets, ""));
}
