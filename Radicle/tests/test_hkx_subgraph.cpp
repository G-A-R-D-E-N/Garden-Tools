#include "fo4/hkx/subgraph.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation_sources.hpp"
#include "fo4/hkx/project_chain.hpp"
#include "fo4/hkx/write.hpp"
#include "ba2_fixture.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_clip_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <initializer_list>
#include <set>
#include <string_view>
#include <system_error>
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

// Constraint: the id must ignore case and forward slashes in the behaviour path and every prefix, or a mod path typed the other way resolves a different manifest.
TEST(SubgraphHash, IgnoresCaseAndForwardSlashesInTheBehaviourPathAndPrefixes) {
  const std::vector<std::string> canonical{"Actors\\Character\\Animations\\Weapon\\44Pistol",
                                           "Actors\\Character\\Animations\\Weapon\\Pistol"};
  const std::vector<std::string> mixed{"Actors\\Character\\Animations\\Weapon\\44Pistol",
                                       "Actors\\Character\\Animations\\Weapon\\PISTOL"};
  const std::vector<std::string> slashes{"Actors/Character/Animations/Weapon/44Pistol",
                                         "Actors/Character/Animations/Weapon/Pistol"};
  const std::vector<std::string> lower{"actors\\character\\animations\\weapon\\44pistol",
                                       "actors\\character\\animations\\weapon\\pistol"};
  const std::uint64_t id = fo4::hkx::subgraph_id("Actors\\Character\\Behaviors\\WeaponBehavior.hkx", canonical);
  EXPECT_EQ(fo4::hkx::subgraph_id("Actors\\Character\\Behaviors\\WeaponBehavior.HKX", mixed), id);
  EXPECT_EQ(fo4::hkx::subgraph_id("Actors/Character/Behaviors/WeaponBehavior.hkx", slashes), id);
  EXPECT_EQ(fo4::hkx::subgraph_id("actors\\character\\behaviors\\weaponbehavior.hkx", lower), id);
  const std::vector<std::string> other{"Actors\\Character\\Animations\\Weapon\\44Pistol"};
  EXPECT_NE(fo4::hkx::subgraph_id("Actors\\Character\\Behaviors\\WeaponBehavior.hkx", other), id);
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
                                    std::vector<ba2_fixture::Ba2GeneralEntry> entries, bool with_names = true) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = with_names;
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

namespace {

void write_loose_bytes(const fo4::test::TempDataDir& dir, const std::string& rel, const std::vector<std::byte>& bytes) {
  const auto file = dir.path() / rel;
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

fo4::hkx::HkxPackfile parse_packfile(const std::vector<std::byte>& bytes) {
  fo4::hkx::HkxPackfile packfile;
  EXPECT_EQ(fo4::hkx::read_packfile(bytes, packfile), fo4::hkx::HkxStatus::Ok);
  return packfile;
}

const fo4::hkx::HkxChainLink* find_link(const std::vector<fo4::hkx::HkxChainLink>& links, std::string_view role) {
  for (const auto& link : links) {
    if (link.role == role) return &link;
  }
  return nullptr;
}

bool has_problem(const fo4::hkx::HkxProjectChain& chain, std::string_view needle) {
  for (const auto& problem : chain.problems) {
    if (problem.find(needle) != std::string::npos) return true;
  }
  return false;
}

const fo4::hkx::HkxCheckFinding* find_finding(const fo4::hkx::HkxProjectCheck& check, std::string_view code) {
  for (const auto& finding : check.findings) {
    if (finding.code == code) return &finding;
  }
  return nullptr;
}

std::size_t count_errors(const fo4::hkx::HkxProjectCheck& check) {
  std::size_t errors = 0;
  for (const auto& finding : check.findings) errors += finding.level == "error" ? 1 : 0;
  return errors;
}

void write_foo_animations(const fo4::test::TempDataDir& dir, std::initializer_list<const char*> names) {
  for (const char* name : names) write_loose(dir, std::string("Meshes/Actors/Foo/Animations/") + name + ".hkx", "x");
}

// Requires the committed vanilla fixture; a missing file fails on CI and skips elsewhere, and the test must return then.
void load_radroach_character(std::vector<std::byte>& bytes) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  std::filesystem::path file;
  for (int level = 0; level < 8 && !dir.empty(); ++level) {
    const auto candidate = dir / "Radicle" / "tests" / "fixtures" / "hkx" / "Meshes" / "Actors" / "RadRoach" /
                           "Characters" / "RadRoachCharacter.hkx";
    if (std::filesystem::exists(candidate, ec)) {
      file = candidate;
      break;
    }
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
  if (file.empty()) {
    if (std::getenv("CI") != nullptr) FAIL() << "CI is missing the committed fixture RadRoachCharacter.hkx";
    GTEST_SKIP() << "no committed fixture RadRoachCharacter.hkx";
  }
  std::ifstream stream(file, std::ios::binary);
  char buffer[4096];
  bytes.clear();
  while (stream.read(buffer, static_cast<std::streamsize>(sizeof buffer)) || stream.gcount() > 0) {
    for (std::streamsize i = 0; i < stream.gcount(); ++i) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[i])));
  }
}

void write_foo_project(const fo4::test::TempDataDir& dir, const std::string& character_link,
                       const std::vector<std::vector<std::string>>& bundles,
                       const std::vector<std::byte>& behavior = fo4::test::hkx::behavior_graph_packfile()) {
  write_loose_bytes(dir, "Meshes/Actors/Foo/Foo.hkx", fo4::test::hkx::project_packfile({character_link}));
  write_loose_bytes(dir, "Meshes/Actors/Foo/Characters/Foo.hkx",
                    fo4::test::hkx::character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx", bundles));
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx", behavior);
  write_loose_bytes(dir, "Meshes/Actors/Foo/CharacterAssets/skeleton.hkx", fo4::test::hkx::clip_packfile(true));
}

}  // namespace

// Constraint: detects a bundle name read as an animation, a wrong stride for the 24-byte hkbAssetBundleStringData, or a lost de-duplication.
TEST(ProjectChain, ReadsTheProjectAndCharacterStringData) {
  std::vector<std::string> characters;
  const auto project = parse_packfile(fo4::test::hkx::project_packfile({"Characters\\Foo.hkx", "Characters\\Bar.hkx"}));
  ASSERT_EQ(fo4::hkx::read_project_characters(project, characters), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(characters, (std::vector<std::string>{"Characters\\Foo.hkx", "Characters\\Bar.hkx"}));

  fo4::hkx::HkxCharacterLinks links;
  const auto character = parse_packfile(fo4::test::hkx::character_packfile(
      "CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx",
      {{"Animations\\a.hkx", "Animations\\b.hkx"}, {"Animations\\b.hkx", "..\\Other\\Animations\\c.hkx"}}));
  ASSERT_EQ(fo4::hkx::read_character_links(character, links), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(links.rig, "CharacterAssets\\skeleton.hkx");
  EXPECT_EQ(links.behavior, "Behaviors\\FooBehavior.hkx");
  EXPECT_EQ(links.animations,
            (std::vector<std::string>{"Animations\\a.hkx", "Animations\\b.hkx", "..\\Other\\Animations\\c.hkx"}));
}

// Constraint: detects a file of another class read as string data instead of reporting the object as missing.
TEST(ProjectChain, RefusesAFileWithoutTheStringDataClass) {
  const auto packfile = parse_packfile(fo4::test::hkx::clip_generator_packfile({{"clip", "Animations\\a.hkx"}}, {0}, 1.0f));
  std::vector<std::string> characters;
  fo4::hkx::HkxCharacterLinks links;
  EXPECT_EQ(fo4::hkx::read_project_characters(packfile, characters), fo4::hkx::HkxStatus::MissingObject);
  EXPECT_EQ(fo4::hkx::read_character_links(packfile, links), fo4::hkx::HkxStatus::MissingObject);
}

// Constraint: detects a chain that stops at the first link, a borrowed animation read as missing, a packed animation not named by its archive, or a missing one not reported.
TEST(ProjectChain, ResolvesLoosePackedAndBorrowedLinksAndNamesTheMissingOnes) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx",
                    {{"Animations\\a.hkx", "Animations\\packed.hkx", "..\\Other\\Animations\\c.hkx", "Animations\\d.hkx",
                      "..\\..\\..\\..\\outside.hkx"}});
  write_loose(dir, "Meshes/Actors/Foo/Animations/a.hkx", "x");
  write_loose(dir, "Meshes/Actors/Other/Animations/c.hkx", "x");
  const auto archive =
      write_archive(dir, "Anim.ba2", {ba2_fixture::general("meshes\\actors\\foo\\animations\\packed.hkx", "x")});
  const auto assets = open_assets(dir, {archive});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "Meshes\\Actors\\Foo\\Behaviors\\FooBehavior.hkx");
  EXPECT_EQ(chain.root, "meshes/actors/foo");
  ASSERT_NE(find_link(chain.links, "project"), nullptr);
  EXPECT_EQ(find_link(chain.links, "project")->resolved, "meshes/actors/foo/foo.hkx");
  ASSERT_NE(find_link(chain.links, "character"), nullptr);
  EXPECT_TRUE(find_link(chain.links, "character")->exists);
  ASSERT_NE(find_link(chain.links, "behaviour"), nullptr);
  EXPECT_TRUE(find_link(chain.links, "behaviour")->exists);
  ASSERT_NE(find_link(chain.links, "skeleton"), nullptr);
  EXPECT_EQ(find_link(chain.links, "skeleton")->note, "3 bones");

  ASSERT_EQ(chain.animations.size(), 5u);
  EXPECT_EQ(chain.animations[0].source, "loose");
  EXPECT_EQ(chain.animations[1].source, "Anim.ba2");
  EXPECT_EQ(chain.animations[2].source, "loose");
  EXPECT_EQ(chain.animations[2].resolved, "meshes/actors/other/animations/c.hkx");
  EXPECT_FALSE(chain.animations[3].exists);
  EXPECT_TRUE(chain.animations[4].blocked);
  EXPECT_EQ(chain.problems.size(), 2u);
  EXPECT_TRUE(has_problem(chain, "missing animation: Animations\\d.hkx"));
  EXPECT_TRUE(has_problem(chain, "animation path is outside the allowed folders: ..\\..\\..\\..\\outside.hkx"));
}

// Constraint: detects a project file given directly resolving to another root than its siblings, or a missing borrow that does not name its lender.
TEST(ProjectChain, AGivenProjectFileIsItsOwnRootAndAMissingBorrowNamesTheLender) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {{"..\\Other\\Animations\\c.hkx"}});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  EXPECT_EQ(chain.root, "meshes/actors/foo");
  EXPECT_TRUE(has_problem(chain, "missing animation, borrowed from Other: ..\\Other\\Animations\\c.hkx"));
  EXPECT_EQ(fo4::hkx::borrowed_from("..\\Other\\Animations\\c.hkx"), "Other");
  EXPECT_EQ(fo4::hkx::borrowed_from("Animations\\c.hkx"), "");
}

// Constraint: detects a link read from file content opening a file outside the project tree: a climb above meshes, a drive, or a rooted path.
TEST(ProjectChain, BlocksALinkThatLeavesTheMeshesTree) {
  for (const char* link : {"..\\..\\..\\outside.hkx", "C:\\outside.hkx", "\\outside.hkx", "..\\..\\..\\..\\..\\outside.hkx"}) {
    fo4::test::TempDataDir dir;
    write_loose_bytes(dir, "Meshes/Actors/Foo/Foo.hkx", fo4::test::hkx::project_packfile({link}));
    write_loose(dir, "outside.hkx", "x");
    const auto assets = open_assets(dir, {});

    const auto chain = fo4::hkx::resolve_project_chain(assets, "Meshes/Actors/Foo/Foo.hkx");
    const auto* character = find_link(chain.links, "character");
    ASSERT_NE(character, nullptr) << link;
    EXPECT_TRUE(character->blocked) << link;
    EXPECT_FALSE(character->exists) << link;
    EXPECT_TRUE(character->resolved.empty()) << link;
    EXPECT_TRUE(has_problem(chain, "character path is outside the allowed folders")) << link;
  }
}

// Constraint: detects a guess between two project files, or a chain reported for a root that holds none.
TEST(ProjectChain, ReportsAnAmbiguousAndAnAbsentProject) {
  fo4::test::TempDataDir dir;
  write_loose_bytes(dir, "Meshes/Actors/Foo/One.hkx", fo4::test::hkx::project_packfile({"Characters\\Foo.hkx"}));
  write_loose_bytes(dir, "Meshes/Actors/Foo/Two.hkx", fo4::test::hkx::project_packfile({"Characters\\Foo.hkx"}));
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/Any.hkx", fo4::test::hkx::behavior_graph_packfile());
  write_loose_bytes(dir, "Meshes/Actors/Bar/Behaviors/Any.hkx", fo4::test::hkx::behavior_graph_packfile());
  const auto assets = open_assets(dir, {});

  const auto ambiguous = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/behaviors/any.hkx");
  EXPECT_TRUE(has_problem(ambiguous, "ambiguous project files"));
  EXPECT_TRUE(ambiguous.links.empty());

  const auto absent = fo4::hkx::resolve_project_chain(assets, "meshes/actors/bar/behaviors/any.hkx");
  EXPECT_TRUE(has_problem(absent, "no project .hkx containing hkbProjectStringData"));

  const auto outside = fo4::hkx::resolve_project_chain(assets, "textures/x.hkx");
  EXPECT_TRUE(has_problem(outside, "below meshes"));
  const auto missing = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/nothing.hkx");
  EXPECT_TRUE(has_problem(missing, "the file is missing"));
}

// Constraint: detects a declared-animation reader that mis-strides the vanilla bundle array, reads the rig as an animation, or loses the borrowed entries of a real character.
TEST(ProjectChain, ReadsTheDeclaredAnimationsOfAVanillaCharacter) {
  std::vector<std::byte> bytes;
  load_radroach_character(bytes);
  if (bytes.empty()) return;
  const auto packfile = parse_packfile(bytes);
  fo4::hkx::HkxCharacterLinks links;
  ASSERT_EQ(fo4::hkx::read_character_links(packfile, links), fo4::hkx::HkxStatus::Ok);
  EXPECT_EQ(links.rig, "CharacterAssets\\skeleton.hkt");
  EXPECT_EQ(links.behavior, "Behaviors\\RadRoachRootBehavior.hkx");
  ASSERT_FALSE(links.animations.empty());
  for (const char* known : {R"(Animations\Idle.hkt)", R"(Animations\RunForward.hkt)", R"(Animations\RunBackward.hkt)", R"(Animations\TurnLeft.hkt)",
                            R"(..\MirelurkQueen\Animations\Recoil3.hkt)", R"(..\FeralGhoul\Animations\Attack5.hkt)"}) {
    EXPECT_NE(std::find(links.animations.begin(), links.animations.end(), known), links.animations.end()) << known;
  }
  EXPECT_EQ(links.animations.front(), "Animations\\Idle.hkt");
  bool borrowed = false;
  for (const auto& animation : links.animations) {
    borrowed = borrowed || animation.rfind("..\\", 0) == 0;
    EXPECT_NE(animation, links.rig);
  }
  EXPECT_TRUE(borrowed);
  const std::set<std::string> unique(links.animations.begin(), links.animations.end());
  EXPECT_EQ(unique.size(), links.animations.size());
}

// Constraint: detects a character with no declared animations passing without a word, or that warning turning a usable chain into a failed one.
TEST(ProjectChain, WarnsWhenACharacterDeclaresNoAnimationsWithoutFailingTheChain) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  EXPECT_TRUE(chain.problems.empty());
  ASSERT_EQ(chain.warnings.size(), 1u);
  EXPECT_NE(chain.warnings[0].find("declares no animations"), std::string::npos);
}

// Constraint: detects a project that names several characters being followed silently as if it named one.
TEST(ProjectChain, WarnsWhenAProjectNamesMoreThanOneCharacter) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {{"Animations\\a.hkx"}});
  write_loose_bytes(dir, "Meshes/Actors/Foo/Foo.hkx",
                    fo4::test::hkx::project_packfile({"Characters\\Foo.hkx", "Characters\\Bar.hkx"}));
  write_foo_animations(dir, {"a"});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  EXPECT_TRUE(chain.problems.empty());
  ASSERT_EQ(chain.warnings.size(), 1u);
  EXPECT_NE(chain.warnings[0].find("names 2 characters"), std::string::npos);
}

// Constraint: detects an animation list past the cap being resolved in full, which lets a hostile character file cost one lookup per string.
TEST(ProjectChain, CutsAnAnimationListPastTheCapAndSaysSo) {
  fo4::test::TempDataDir dir;
  std::vector<std::string> names;
  for (std::size_t i = 0; i < fo4::hkx::k_max_declared_animations + 5; ++i) {
    names.push_back("Animations\\n" + std::to_string(i) + ".hkx");
  }
  write_foo_project(dir, "Characters\\Foo.hkx", {names});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  EXPECT_EQ(chain.animations.size(), fo4::hkx::k_max_declared_animations);
  EXPECT_TRUE(has_problem(chain, "only the first 10000 are resolved"));
}

// Constraint: detects a sibling that cannot be read as a packfile vanishing from the report, which would turn an unreadable project into no project found.
TEST(ProjectChain, NamesASiblingThatCannotBeReadAsAPackfile) {
  fo4::test::TempDataDir dir;
  write_loose_bytes(dir, "Meshes/Actors/Foo/Broken.hkx", {std::byte{1}, std::byte{2}, std::byte{3}});
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/Any.hkx", fo4::test::hkx::behavior_graph_packfile());
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/behaviors/any.hkx");
  EXPECT_TRUE(has_problem(chain, "no project .hkx containing hkbProjectStringData"));
  bool named = false;
  for (const auto& warning : chain.warnings) {
    named = named || warning.find("could not read meshes/actors/foo/broken.hkx as a packfile") != std::string::npos;
  }
  EXPECT_TRUE(named);
}

// Constraint: detects a listing of a nameless archive passing as complete, which would report no project for a project that is packed.
TEST(ProjectChain, FlagsAListingThatANamelessArchiveMakesIncomplete) {
  fo4::test::TempDataDir dir;
  const auto archive = write_archive(dir, "Nameless.ba2",
                                     {ba2_fixture::general("meshes\\actors\\foo\\behaviors\\any.hkx", "x")}, false);
  const auto assets = open_assets(dir, {archive});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/behaviors/any.hkx");
  EXPECT_TRUE(has_problem(chain, "the listing of meshes/actors/foo is incomplete"));
}

// Constraint: detects a check that drops a refused file, calls a file with no graph clean, or stops short without saying so.
TEST(ProjectCheck, ReportsRefusedAndGraphlessBehaviourFilesAndSaysWhenItStops) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {});
  write_foo_animations(dir, {"A", "B", "C"});
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/Junk.hkx", {std::byte{1}, std::byte{2}, std::byte{3}});
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/Rig.hkx", fo4::test::hkx::clip_packfile(true));
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  EXPECT_EQ(check.files, 3u);
  EXPECT_EQ(check.read, 2u);
  EXPECT_FALSE(check.truncated);
  const auto level_of = [&](std::string_view file) {
    for (const auto& finding : check.findings) {
      if (finding.file == file) return finding.level;
    }
    return std::string{};
  };
  EXPECT_EQ(level_of("meshes/actors/foo/behaviors/junk.hkx"), "error");
  EXPECT_EQ(level_of("meshes/actors/foo/behaviors/rig.hkx"), "warning");
  EXPECT_NE(level_of("meshes/actors/foo/behaviors/foobehavior.hkx"), "error");
  EXPECT_EQ(find_finding(check, "chain.clip_animation_absent"), nullptr);
  EXPECT_NE(find_finding(check, "file.not_packfile"), nullptr);
  EXPECT_NE(find_finding(check, "graph.absent"), nullptr);

  const auto limited = fo4::hkx::check_project_behaviors(assets, chain, 1);
  EXPECT_EQ(limited.files, 1u);
  EXPECT_TRUE(limited.truncated);
}

// Constraint: detects the check passing a graph whose clip trigger names an event that does not exist, or a clip naming an animation that is not in the project.
TEST(ProjectCheck, ReportsADanglingEventIdAndAClipAnimationThatIsAbsent) {
  fo4::test::TempDataDir dir;
  fo4::test::hkx::BehaviorFixtureOptions options;
  options.trigger_event = 9;
  write_foo_project(dir, "Characters\\Foo.hkx", {{"Animations\\A.hkt", "Animations\\B.hkt"}},
                    fo4::test::hkx::behavior_graph_packfile(options));
  write_foo_animations(dir, {"A", "B"});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  ASSERT_TRUE(chain.problems.empty());
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  const auto* event = find_finding(check, "clip.trigger_event_range");
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(event->level, "error");
  const auto* absent = find_finding(check, "chain.clip_animation_absent");
  ASSERT_NE(absent, nullptr);
  EXPECT_EQ(absent->level, "error");
  EXPECT_NE(absent->message.find("Animations\\C.hkt"), std::string::npos);
  EXPECT_EQ(find_finding(check, "chain.clip_animation_undeclared"), nullptr);
  EXPECT_GE(count_errors(check), 2u);
}

// Constraint: detects a clip animation that exists but is missing from the declared list passing as clean, or that warning being raised as an error.
TEST(ProjectCheck, WarnsOnAClipAnimationTheCharacterDoesNotDeclare) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {{"Animations\\a.hkt", "animations/B"}});
  write_foo_animations(dir, {"A", "B", "C"});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  const auto* undeclared = find_finding(check, "chain.clip_animation_undeclared");
  ASSERT_NE(undeclared, nullptr);
  EXPECT_EQ(undeclared->level, "warning");
  EXPECT_NE(undeclared->message.find("Animations\\C.hkt"), std::string::npos);
  EXPECT_EQ(count_errors(check), 0u);
}

// Constraint: detects a behaviour listing that a nameless archive cut short passing as a complete check.
TEST(ProjectCheck, FlagsABehaviourListingThatANamelessArchiveMakesIncomplete) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {});
  const auto archive = write_archive(dir, "Nameless.ba2",
                                     {ba2_fixture::general("meshes\\actors\\foo\\behaviors\\packed.hkx", "x")}, false);
  const auto assets = open_assets(dir, {archive});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  const auto* incomplete = find_finding(check, "check.listing_incomplete");
  ASSERT_NE(incomplete, nullptr);
  EXPECT_EQ(incomplete->level, "error");
}

namespace {

const fo4::hkx::HkxFileCheck* find_file(const fo4::hkx::HkxProjectCheck& check, std::string_view path) {
  for (const auto& file : check.per_file) {
    if (file.path == path) return &file;
  }
  return nullptr;
}

// Requires a packfile read_packfile accepts; only the data section's header changes, and the file grows by one exports row.
std::vector<std::byte> with_exports_tail(std::vector<std::byte> bytes) {
  fo4::hkx::HkxPackfile parsed;
  EXPECT_EQ(fo4::hkx::read_packfile(bytes, parsed), fo4::hkx::HkxStatus::Ok);
  const auto word = [&bytes](std::size_t at, std::size_t width) {
    std::size_t value = 0;
    for (std::size_t i = 0; i < width; ++i) value |= static_cast<std::size_t>(std::to_integer<unsigned>(bytes[at + i])) << (8 * i);
    return value;
  };
  const std::size_t header = 0x40 + word(0x3E, 2) + parsed.data_section * 0x40;
  const std::size_t mark3 = word(header + 0x18 + 12, 4);
  bytes.insert(bytes.end(), 8, std::byte{0});
  const auto set32 = [&bytes](std::size_t at, std::size_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  };
  set32(header + 0x18 + 16, mark3);
  set32(header + 0x18 + 20, mark3 + 8);
  return bytes;
}

}  // namespace

// Constraint: detects a project check that passes a behaviour file whose class signature disagrees with the class table, counts that loss as a graph error, or reports a clean file as lossy.
TEST(ProjectCheck, AClassTheTableDisagreesWithIsARoundTripLossAndACleanFileHasNone) {
  fo4::test::TempDataDir dir;
  fo4::test::hkx::BehaviorFixtureOptions options;
  options.machine_signature = 1234U;
  write_foo_project(dir, "Characters\\Foo.hkx", {}, fo4::test::hkx::behavior_graph_packfile(options));
  write_loose_bytes(dir, "Meshes/Actors/Foo/Behaviors/Clean.hkx", fo4::test::hkx::behavior_graph_packfile());
  write_foo_animations(dir, {"A", "B", "C"});
  ASSERT_EQ(fo4::hkx::repack_check(fo4::test::hkx::behavior_graph_packfile()).verdict, fo4::hkx::HkxRepackVerdict::Equal);
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  ASSERT_EQ(check.per_file.size(), 2u);

  const auto* lossy = find_file(check, "meshes/actors/foo/behaviors/foobehavior.hkx");
  ASSERT_NE(lossy, nullptr);
  ASSERT_EQ(lossy->round_trip.size(), 1u);
  EXPECT_EQ(lossy->round_trip[0].kind, "UnsupportedClass");
  EXPECT_NE(lossy->round_trip[0].message.find("hkbStateMachine is signed 0x000004d2 in the file and 0x"), std::string::npos);
  EXPECT_FALSE(lossy->error.empty());
  EXPECT_EQ(lossy->errors, 0u);

  const auto* clean = find_file(check, "meshes/actors/foo/behaviors/clean.hkx");
  ASSERT_NE(clean, nullptr);
  EXPECT_TRUE(clean->round_trip.empty());
  EXPECT_TRUE(clean->error.empty());
  EXPECT_EQ(clean->errors, 0u);

  EXPECT_EQ(fo4::hkx::round_trip_losses(check), 1u);
  EXPECT_EQ(fo4::hkx::files_with_round_trip_losses(check), 1u);
  EXPECT_EQ(fo4::hkx::unreadable_files(check), 1u);
  const auto text = fo4::hkx::describe_project_check(check);
  EXPECT_EQ(text.rfind("1 of 2 behaviour files read: 0 errors, ", 0), 0u) << text;
  EXPECT_NE(text.find(", 1 round-trip issue in 1 file, 1 could not be unpacked"), std::string::npos) << text;
}

// Constraint: detects a repack refusal (unmeasured on vanilla files) being counted as a round-trip loss that flips the verdict, or being dropped without a trace.
TEST(ProjectCheck, ARepackRefusalIsAWarningFindingNotALossOrAGraphError) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, "Characters\\Foo.hkx", {}, with_exports_tail(fo4::test::hkx::behavior_graph_packfile()));
  write_foo_animations(dir, {"A", "B", "C"});
  const auto assets = open_assets(dir, {});

  const auto chain = fo4::hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  const auto check = fo4::hkx::check_project_behaviors(assets, chain, 10);
  const auto* file = find_file(check, "meshes/actors/foo/behaviors/foobehavior.hkx");
  ASSERT_NE(file, nullptr);
  EXPECT_TRUE(file->round_trip.empty());
  EXPECT_TRUE(file->error.empty());
  EXPECT_EQ(file->errors, 0u);
  EXPECT_EQ(count_errors(check), 0u);
  const auto* refused = find_finding(check, "repack.refused");
  ASSERT_NE(refused, nullptr);
  EXPECT_EQ(refused->level, "warning");
  EXPECT_GE(file->warnings, 1u);
  EXPECT_EQ(fo4::hkx::round_trip_losses(check), 0u);
  EXPECT_EQ(fo4::hkx::describe_project_check(check).find("round-trip"), std::string::npos);
}

// Constraint: detects a report that drops a loss or an unreadable file, lets a spreadsheet run a finding's text, or writes untrusted text into HTML unencoded.
TEST(ProjectCheck, ReportRowsQuoteFormulasEncodeHtmlAndCarryLossesAndUnreadableFiles) {
  using namespace fo4::hkx;
  HkxProjectChain chain;
  chain.root = "meshes/actors/test & project";
  HkxProjectCheck check;
  check.files = 4;
  check.findings = {
      {"meshes/actors/test & project/behaviors", "error", "check.listing_incomplete", "the listing is incomplete"},
      {"meshes/actors/test & project/behaviors/broken & bad.hkx", "error", "file.not_packfile", "could not <parse>"},
      {"meshes/actors/test & project/behaviors/findings.hkx", "error", "clip.trigger_event_range", "=HYPERLINK(\"bad\"), still bad"},
      {"meshes/actors/test & project/behaviors/findings.hkx", "warning", "graph.absent", " +1"},
  };
  HkxFileCheck clean;
  clean.path = "meshes/actors/test & project/behaviors/clean.hkx";
  clean.name = "clean.hkx";
  clean.first_finding = 1;
  HkxFileCheck broken;
  broken.path = check.findings[1].file;
  broken.name = "broken & bad.hkx";
  broken.error = "could not <parse>";
  broken.first_finding = 1;
  broken.finding_count = 1;
  HkxFileCheck noisy;
  noisy.path = check.findings[2].file;
  noisy.name = "findings.hkx";
  noisy.first_finding = 2;
  noisy.finding_count = 2;
  noisy.errors = 1;
  noisy.warnings = 1;
  HkxFileCheck lossy;
  lossy.path = "meshes/actors/test & project/behaviors/unsafe.hkx";
  lossy.name = "unsafe.hkx";
  lossy.first_finding = 4;
  lossy.round_trip.push_back({"UnsupportedClass", "unsupported <class> & value"});
  check.per_file = {clean, broken, noisy, lossy};

  EXPECT_EQ(describe_project_check(check),
            "3 of 4 behaviour files read: 1 error, 1 warning, 1 round-trip issue in 1 file, 1 could not be unpacked");

  const std::string csv = render_project_report(chain, check, HkxReportFormat::Csv);
  EXPECT_EQ(csv.rfind("project,file,path,status,severity,object_id,where,message,blocks_save\n", 0), 0u);
  EXPECT_EQ(csv.find('\r'), std::string::npos);
  EXPECT_NE(csv.find(",unreadable,error,,,could not <parse>,\n"), std::string::npos);
  EXPECT_NE(csv.find(",round-trip-loss,error,,,unsupported <class> & value,true\n"), std::string::npos);
  EXPECT_NE(csv.find(",clean,,,,,\n"), std::string::npos);
  EXPECT_NE(csv.find(",project-finding,error,,check.listing_incomplete,the listing is incomplete,\n"), std::string::npos);
  EXPECT_NE(csv.find("\"'=HYPERLINK(\"\"bad\"\"), still bad\""), std::string::npos);
  EXPECT_NE(csv.find("\"' +1\""), std::string::npos);

  check.truncated = true;
  const std::string html = render_project_report(chain, check, HkxReportFormat::Html);
  EXPECT_EQ(html.rfind("<!doctype html>", 0), 0u);
  EXPECT_NE(html.find("broken &amp; bad.hkx"), std::string::npos);
  EXPECT_NE(html.find("could not &lt;parse&gt;"), std::string::npos);
  EXPECT_NE(html.find("unsupported &lt;class&gt; &amp; value"), std::string::npos);
  EXPECT_NE(html.find("=HYPERLINK(&quot;bad&quot;)"), std::string::npos);
  EXPECT_EQ(html.find("could not <parse>"), std::string::npos);
  EXPECT_NE(html.find("Round-trip issues"), std::string::npos);
  EXPECT_NE(html.find("Stopped at the file limit"), std::string::npos);
}

// Constraint: detects a project with no behaviour file reading as a clean zero, or a plural slipping in for a single issue.
TEST(ProjectCheck, DescribesAnEmptyProjectAndSingularCounts) {
  fo4::hkx::HkxProjectCheck check;
  EXPECT_EQ(fo4::hkx::describe_project_check(check), "no behaviour files were found in this project");
  fo4::hkx::HkxFileCheck file;
  file.errors = 1;
  file.warnings = 1;
  check.per_file.push_back(file);
  EXPECT_EQ(fo4::hkx::describe_project_check(check), "1 of 1 behaviour files read: 1 error, 1 warning");
}

// Constraint: detects a mod archive losing to a base archive, loose Data losing to an archive, or an unreadable archive being dropped without a note, in the shared source helper the chain, check and manifest tools open.
TEST(AnimationSources, AModArchiveBeatsABaseArchiveLooseBeatsBothAndABadArchiveIsNoted) {
  fo4::test::TempDataDir root;
  const std::string inner = "meshes\\animtextdata\\animationfiledata\\10067747564108604006.txt";
  write_loose(root, "ModOrganizer.ini", "selected_profile=Test\ngamePath=" + (root.path() / "Game").string() + "\n");
  write_loose(root, "profiles/Test/modlist.txt", "+Mod\n");
  write_loose(root, "profiles/Test/plugins.txt", "");
  std::filesystem::create_directories(root.path() / "Game" / "Data");
  std::filesystem::create_directories(root.path() / "mods" / "Mod");
  std::filesystem::create_directories(root.path() / "overwrite");
  fo4::archive::Ba2GeneralArchive base;
  base.has_string_table = true;
  base.entries = {ba2_fixture::general(inner, "from base")};
  fo4::archive::write_ba2_general_file(base, root.path() / "Game" / "Data" / "Base.ba2");
  fo4::archive::Ba2GeneralArchive mod;
  mod.has_string_table = true;
  mod.entries = {ba2_fixture::general(inner, "from mod")};
  fo4::archive::write_ba2_general_file(mod, root.path() / "mods" / "Mod" / "Mod.ba2");
  write_loose_bytes(root, "Game/Data/Bad.ba2", {std::byte{1}, std::byte{2}, std::byte{3}});

  fo4::hkx::AnimationSourceRequest request;
  request.mo2_instance = root.path();
  const auto sources = fo4::hkx::open_animation_sources(request);
  const auto manifest = fo4::hkx::read_subgraph_manifest(sources.resolver, k_manifest_id);
  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->text, "from mod");
  EXPECT_EQ(manifest->source, "Mod.ba2");
  bool noted = false;
  for (const auto& note : sources.notes) noted = noted || note.find("Bad.ba2 was skipped") != std::string::npos;
  EXPECT_TRUE(noted);

  write_loose(root, "Game/Data/Meshes/AnimTextData/AnimationFileData/10067747564108604006.txt", "loose base");
  const auto with_loose = fo4::hkx::open_animation_sources(request);
  const auto loose = fo4::hkx::read_subgraph_manifest(with_loose.resolver, k_manifest_id);
  ASSERT_TRUE(loose.has_value());
  EXPECT_EQ(loose->text, "loose base");
  EXPECT_EQ(loose->source, "loose");
}
