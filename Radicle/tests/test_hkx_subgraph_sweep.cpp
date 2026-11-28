#include "fo4/hkx/subgraph_sweep.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation_sources.hpp"
#include "fo4/hkx/modlist_scan.hpp"
#include "fo4/hkx/project_chain.hpp"
#include "fo4/hkx/subgraph.hpp"
#include "fo4/hkx/weapon_subgraph.hpp"
#include "ba2_fixture.hpp"
#include "climate_record_fixture.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_clip_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace hkx = fo4::hkx;
using Bytes = std::vector<std::byte>;

void write_bytes(const std::filesystem::path& file, const Bytes& bytes) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const std::filesystem::path& file, std::string_view text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out << text;
}

std::string manifest_text(const std::string& id, const std::vector<std::string>& paths) {
  std::string text = "V4\n0\n" + id + "\n0\n";
  for (const auto& path : paths) text += path + "\n";
  return text;
}

fo4::archive::AssetResolver open_roots(const std::vector<std::filesystem::path>& roots) {
  return fo4::archive::AssetResolver::open_roots(roots, {});
}

Bytes weapon_behavior(const std::string& generic_a, const std::string& generic_b, const std::string& typed) {
  fo4::test::hkx::BehaviorFixtureOptions options;
  options.clip_a = generic_a;
  options.clip_b = generic_b;
  options.clip_c = typed;
  return fo4::test::hkx::behavior_graph_packfile(options);
}

void write_foo_project(const fo4::test::TempDataDir& dir, const Bytes& behavior) {
  write_bytes(dir.path() / "Meshes/Actors/Foo/Foo.hkx", fo4::test::hkx::project_packfile({"Characters\\Foo.hkx"}));
  write_bytes(dir.path() / "Meshes/Actors/Foo/Characters/Foo.hkx",
              fo4::test::hkx::character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx", {std::vector<std::string>{}}));
  write_bytes(dir.path() / "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx", behavior);
  write_bytes(dir.path() / "Meshes/Actors/Foo/CharacterAssets/skeleton.hkx", fo4::test::hkx::clip_packfile(true));
}

void write_master_with_weapon_sets(const fo4::test::TempDataDir& dir, const std::vector<std::string>& types) {
  namespace sky = fo4::test::sky;
  std::vector<sky::Sub> subrecords{{"EDID", sky::zstr("HumanRace")}};
  std::uint32_t set_id = 7;
  for (const std::string& type : types) {
    subrecords.push_back({"SAKD", sky::words({set_id++})});
    subrecords.push_back({"SGNM", sky::zstr("Actors\\Character\\Behaviors\\WeaponBehavior.hkx")});
    subrecords.push_back({"SAPT", sky::zstr("Actors\\Character\\Animations\\Weapon\\" + type)});
    subrecords.push_back({"SRAF", sky::words({0})});
  }
  const sky::Entry race{"RACE", 0x800, subrecords, 131};
  write_bytes(dir.path() / "Fallout4.esm", sky::build({race}, false));
}

void write_master_with_rifle_set(const fo4::test::TempDataDir& dir) { write_master_with_weapon_sets(dir, {"Rifle"}); }

const hkx::HkxCheckFinding* find_code(const hkx::HkxProjectCheck& check, std::string_view code) {
  for (const auto& finding : check.findings) {
    if (finding.code == code) return &finding;
  }
  return nullptr;
}

std::size_t count_code(const hkx::HkxProjectCheck& check, std::string_view code) {
  return static_cast<std::size_t>(std::count_if(check.findings.begin(), check.findings.end(),
                                                [&](const hkx::HkxCheckFinding& finding) { return finding.code == code; }));
}

hkx::HkxProjectCheck check_foo(const fo4::archive::AssetResolver& assets) {
  const auto chain = hkx::resolve_project_chain(assets, "meshes/actors/foo/foo.hkx");
  return hkx::check_project_behaviors(assets, chain, 10);
}

}  // namespace

// Constraint: detects an AnimationOffsets path losing to an earlier bare number, a bare-number hit passing as confirmed, or a digit run that overflows 64 bits ending the search.
TEST(CrashSubgraphId, PrefersTheOffsetsPathAndFlagsABareNumber) {
  const auto path = hkx::find_crash_subgraph_id("frame 1234567890 then Data\\Meshes\\AnimTextData\\AnimationOffsets\\10067747564108604006.txt");
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(path->id, 10067747564108604006ull);
  EXPECT_EQ(path->matched, "animation_offsets_path");

  const auto lowered = hkx::find_crash_subgraph_id("meshes/animtextdata/animationoffsets/42.txt");
  ASSERT_TRUE(lowered.has_value());
  EXPECT_EQ(lowered->id, 42u);
  EXPECT_EQ(lowered->matched, "animation_offsets_path");

  const auto bare = hkx::find_crash_subgraph_id("crash at 123456789012 in the animation system");
  ASSERT_TRUE(bare.has_value());
  EXPECT_EQ(bare->id, 123456789012ull);
  EXPECT_EQ(bare->matched, "bare_number");

  const auto skipped = hkx::find_crash_subgraph_id("99999999999999999999 then 4455667788");
  ASSERT_TRUE(skipped.has_value());
  EXPECT_EQ(skipped->id, 4455667788ull);
}

// Constraint: detects a short number or empty text being read as an id.
TEST(CrashSubgraphId, FindsNothingInTextWithoutAnId) {
  EXPECT_FALSE(hkx::find_crash_subgraph_id("").has_value());
  EXPECT_FALSE(hkx::find_crash_subgraph_id("error 12345678 at 0x7ff6").has_value());
  EXPECT_FALSE(hkx::find_crash_subgraph_id("AnimationOffsets\\notanumber.txt").has_value());
}

// Constraint: detects a block that is not opened by SAKD, a block with no prefix being kept, or the SRAF flags being lost.
TEST(RaceAnimationBlocks, GroupBySakdAndDropIncompleteBlocks) {
  namespace sky = fo4::test::sky;
  fo4::Record race;
  race.header.signature = fo4::Signature::from_string("RACE");
  const auto add = [&](const char* signature, const Bytes& data) {
    race.subrecords.push_back(fo4::Subrecord{fo4::Signature::from_string(signature), data});
  };
  add("SGNM", sky::zstr("Actors\\A\\Behaviors\\NoId.hkx"));
  add("SAPT", sky::zstr("Actors\\A\\Animations\\NoId"));
  add("SAKD", sky::words({5}));
  add("SGNM", sky::zstr("Actors\\A\\Behaviors\\WeaponA.hkx"));
  add("SAPT", sky::zstr("Actors\\A\\Animations\\Weapon\\Rifle"));
  add("SAPT", sky::zstr("Actors\\A\\Animations\\Weapon\\Common"));
  add("SRAF", sky::words({3}));
  add("STKD", sky::words({1}));
  add("SAKD", sky::words({6}));
  add("SGNM", sky::zstr("Actors\\A\\Behaviors\\NoPrefix.hkx"));
  add("SAKD", sky::words({8}));
  add("SGNM", sky::zstr("Actors\\A\\Behaviors\\WeaponB.hkx"));
  add("SAPT", sky::zstr("Actors\\A\\Animations\\Weapon\\Pistol"));

  const auto blocks = hkx::race_animation_blocks(race);
  ASSERT_EQ(blocks.size(), 2u);
  EXPECT_EQ(blocks[0].set_id, 5u);
  EXPECT_EQ(blocks[0].behavior, "Actors\\A\\Behaviors\\WeaponA.hkx");
  EXPECT_EQ(blocks[0].prefixes.size(), 2u);
  EXPECT_EQ(blocks[0].flags, 3u);
  EXPECT_EQ(blocks[1].set_id, 8u);
  EXPECT_EQ(blocks[1].prefixes.size(), 1u);

  race.header.signature = fo4::Signature::from_string("NPC_");
  EXPECT_TRUE(hkx::race_animation_blocks(race).empty());
}

// Constraint: detects a non-weapon behaviour contributing a type, a type cut at an embedded Actors folder, a case duplicate kept, or an unsorted result.
TEST(WeaponTypeSets, KeepOnlyWeaponBehavioursCutEmbeddedActorsAndSortByType) {
  std::vector<hkx::RaceAnimBlock> blocks(3);
  blocks[0].set_id = 1;
  blocks[0].behavior = "Actors\\Character\\Behaviors\\WeaponBehavior.hkx";
  blocks[0].prefixes = {"Actors\\Character\\Animations\\Weapon\\Rifle", "Actors\\Character\\Animations\\Weapon\\Rifle\\Actors\\Other",
                        "Actors\\Character\\Animations\\weapon\\RIFLE"};
  blocks[1].set_id = 2;
  blocks[1].behavior = "Actors\\Character\\Behaviors\\RootBehavior.hkx";
  blocks[1].prefixes = {"Actors\\Character\\Animations\\Weapon\\Sword"};
  blocks[2].set_id = 3;
  blocks[2].behavior = "Actors\\Character\\Behaviors\\WeaponBehavior2.hkx";
  blocks[2].prefixes = {"Actors\\Character\\Animations\\Weapon\\Axe"};

  const auto sets = hkx::weapon_type_sets(blocks);
  ASSERT_EQ(sets.size(), 2u);
  EXPECT_EQ(sets[0].type, "Axe");
  EXPECT_EQ(sets[1].type, "Rifle");
  EXPECT_EQ(sets[1].prefixes, (std::vector<std::string>{"Animations\\Weapon\\Rifle", "Animations\\Weapon\\Rifle\\"}));
}

// Constraint: detects a SAPT entry that glues two paths together creating a bogus weapon type or keeping the embedded Actors path as a prefix.
TEST(WeaponTypeSets, AMergedSaptPathKeepsTheLeadingPrefix) {
  std::vector<hkx::RaceAnimBlock> blocks(1);
  blocks[0].set_id = 1;
  blocks[0].behavior = "Actors\\Character\\Behaviors\\WeaponBehavior.hkx";
  blocks[0].prefixes = {"Actors\\Character\\Animations\\Weapon\\44Pistol",
                        "Actors\\Character\\Animations\\Weapon\\44PistolActors\\Character\\Animations\\Weapon\\Pistol\\Injured\\Right"};

  const auto sets = hkx::weapon_type_sets(blocks);
  ASSERT_EQ(sets.size(), 1u);
  EXPECT_EQ(sets[0].type, "44Pistol");
  EXPECT_EQ(sets[0].prefixes, (std::vector<std::string>{"Animations\\Weapon\\44Pistol"}));
}

// Constraint: detects the race-set check missing a clip that has no copy in the type's prefix chain and no generic copy, or flagging a clip a prefix copy or the generic copy covers.
TEST(WeaponSubgraphCheck, RaceSetsReportAClipNoPrefixOrGenericCopyCovers) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_master_with_rifle_set(dir);
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Fire.hkx", "x");
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Weapon/Rifle/Fire.hkx", "x");
  const auto assets = open_roots({dir.path()});

  const auto check = check_foo(assets);
  ASSERT_EQ(count_code(check, "chain.weapon_coverage"), 1u);
  const auto* gap = find_code(check, "chain.weapon_coverage");
  EXPECT_EQ(gap->level, "warning");
  EXPECT_NE(gap->message.find("'Rifle' cannot resolve Idle.hkx"), std::string::npos);
  EXPECT_NE(gap->message.find("1-prefix chain starting at Animations\\Weapon\\Rifle\\Idle.hkx"), std::string::npos);

  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Idle.hkx", "x");
  EXPECT_EQ(find_code(check_foo(open_roots({dir.path()})), "chain.weapon_coverage"), nullptr);
}

// Constraint: detects the folder fallback running when a master exists, a generic copy being treated as a crash, or a missing generic copy being treated as a fallback.
TEST(WeaponSubgraphCheck, FolderFallbackNamesFallbackAndCrashWhenNoMasterIsPresent) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Weapon/Pistol/Idle.hkx", "x");
  const auto assets = open_roots({dir.path()});

  const auto crash_check = check_foo(assets);
  const auto* crash = find_code(crash_check, "chain.weapon_coverage");
  ASSERT_NE(crash, nullptr);
  EXPECT_NE(crash->message.find("'Rifle' lacks Idle.hkx under Animations\\Weapon\\Rifle"), std::string::npos);
  EXPECT_NE(crash->message.find("is a crash"), std::string::npos);

  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Idle.hkx", "x");
  const auto fallback_check = check_foo(open_roots({dir.path()}));
  const auto* fallback = find_code(fallback_check, "chain.weapon_coverage");
  ASSERT_NE(fallback, nullptr);
  EXPECT_NE(fallback->message.find("the engine falls back and the clip still plays"), std::string::npos);
}

// Constraint: detects a probe that could not be read being reported as a crash gap, or leaving the check looking clean.
TEST(WeaponSubgraphCheck, AProbeThatCannotBeReadIsNotReportedAsACrashGap) {
  fo4::test::TempDataDir dir;
  write_master_with_rifle_set(dir);
  const auto assets = open_roots({dir.path()});

  hkx::WeaponSetCache cache;
  std::vector<hkx::HkxGraphClip> clips(2);
  clips[0].animation = "Animations/Idle.hkx";
  clips[1].animation = "Animations/Weapon/Rifle/Idle.hkx";
  const auto check = hkx::check_weapon_subgraph(assets, cache, "Actors/../Foo", clips);
  EXPECT_TRUE(check.weapon_subgraph);
  EXPECT_TRUE(check.gaps.empty());
  EXPECT_TRUE(cache.probe_failed);
  EXPECT_FALSE(cache.probe_reason.empty());

  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Idle.hkx", "x");
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Weapon/Rifle/Idle.hkx", "x");
  hkx::WeaponSetCache covered;
  const auto covered_check = hkx::check_weapon_subgraph(assets, covered, "Actors/Foo", clips);
  EXPECT_TRUE(covered_check.gaps.empty());
  EXPECT_FALSE(covered.probe_failed);
}

// Constraint: detects a plain behaviour (no weapon clips) reporting weapon coverage.
TEST(WeaponSubgraphCheck, ABehaviourWithoutWeaponClipsReportsNothing) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, fo4::test::hkx::behavior_graph_packfile());
  write_master_with_rifle_set(dir);
  EXPECT_EQ(find_code(check_foo(open_roots({dir.path()})), "chain.weapon_coverage"), nullptr);
}

// Constraint: detects the "more" line losing its count.
TEST(WeaponSubgraphCheck, MoreLineNamesTheExtraCount) {
  std::vector<hkx::WeaponGap> gaps(8);
  EXPECT_EQ(hkx::k_max_weapon_findings, 6u);
  EXPECT_NE(hkx::weapon_gaps_more_message(gaps.size() - hkx::k_max_weapon_findings).find("2 more missing clip(s)"), std::string::npos);
}

// Constraint: detects the project check printing more than six gap warnings, or dropping the "more" line, when four weapon types each miss two clips.
TEST(WeaponSubgraphCheck, TheProjectCheckPrintsSixGapsAndAMoreLine) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_master_with_weapon_sets(dir, {"Rifle", "Pistol", "Axe", "Bat"});
  const auto check = check_foo(open_roots({dir.path()}));
  EXPECT_EQ(count_code(check, "chain.weapon_coverage"), 6u);
  const auto* more = find_code(check, "chain.weapon_coverage_more");
  ASSERT_NE(more, nullptr);
  EXPECT_NE(more->message.find("2 more missing clip(s)"), std::string::npos);
}

// Constraint: detects the sweep counting a non-weapon subgraph, missing a crash gap, keeping both paths of one id, or reporting a cut sweep as complete.
TEST(SubgraphSweep, ReportsWeaponSubgraphsWithGapsCountsDuplicatesAndFlagsCuts) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/Weapon/Pistol/Idle.hkx", "x");
  write_bytes(dir.path() / "Meshes/Actors/Bar/Behaviors/BarBehavior.hkx", fo4::test::hkx::behavior_graph_packfile());
  const std::string base = "Meshes/AnimTextData/AnimationFileData/";
  write_text(dir.path() / (base + "111.txt"), manifest_text("111", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  write_text(dir.path() / (base + "222.txt"), manifest_text("222", {"Actors\\Bar\\Behaviors\\BarBehavior.hkx"}));
  write_text(dir.path() / (base + "333.txt"), manifest_text("111", {"Actors\\Bar\\Behaviors\\BarBehavior.hkx"}));
  const auto assets = open_roots({dir.path()});

  const auto sweep = hkx::sweep_subgraphs(assets, {});
  EXPECT_EQ(sweep.manifests, 2u);
  EXPECT_EQ(sweep.duplicate_ids, 1u);
  EXPECT_EQ(sweep.weapon_checked, 1u);
  EXPECT_EQ(sweep.failures_total, 1u);
  ASSERT_EQ(sweep.failures.size(), 1u);
  EXPECT_EQ(sweep.failures[0].id, 111u);
  EXPECT_EQ(sweep.failures[0].crash_gaps, 1u);
  EXPECT_FALSE(sweep.ok());
  EXPECT_TRUE(sweep.complete());
  EXPECT_FALSE(sweep.weapon_sets_reason.empty());
  ASSERT_EQ(sweep.failures[0].gaps.size(), 1u);

  hkx::SweepLimits no_failures;
  no_failures.max_failures = 0;
  const auto unlisted = hkx::sweep_subgraphs(assets, no_failures);
  EXPECT_EQ(unlisted.failures_total, 1u);
  EXPECT_TRUE(unlisted.truncated_failures);
  EXPECT_TRUE(unlisted.complete());

  write_text(dir.path() / (base + "bad.txt"), "not a manifest");
  const auto with_bad = hkx::sweep_subgraphs(open_roots({dir.path()}), {});
  EXPECT_EQ(with_bad.manifests_unread, 1u);
  EXPECT_FALSE(with_bad.complete());
  std::filesystem::remove(dir.path() / (base + "bad.txt"));

  hkx::SweepLimits one_manifest;
  one_manifest.max_manifests = 1;
  const auto cut = hkx::sweep_subgraphs(assets, one_manifest);
  EXPECT_TRUE(cut.truncated_manifests);
  EXPECT_FALSE(cut.complete());

  hkx::SweepLimits one_behavior;
  one_behavior.max_behaviors = 1;
  const auto short_sweep = hkx::sweep_subgraphs(assets, one_behavior);
  EXPECT_TRUE(short_sweep.truncated_behaviors);
  EXPECT_EQ(short_sweep.unchecked, 1u);
  EXPECT_FALSE(short_sweep.complete());
}

// Constraint: detects a sweep that passes when no manifest exists as if it had checked something.
TEST(SubgraphSweep, AnEmptyDataFolderChecksNothing) {
  fo4::test::TempDataDir dir;
  const auto sweep = hkx::sweep_subgraphs(open_roots({dir.path()}), {});
  EXPECT_EQ(sweep.manifests, 0u);
  EXPECT_EQ(sweep.weapon_checked, 0u);
  EXPECT_TRUE(sweep.ok());
  EXPECT_FALSE(sweep.complete());
  EXPECT_FALSE(sweep.notes.empty());
}

// Constraint: detects a manifest naming a behaviour file that does not exist passing as a complete sweep.
TEST(SubgraphSweep, AMissingBehaviourFileMakesTheSweepIncomplete) {
  fo4::test::TempDataDir dir;
  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/222.txt", manifest_text("222", {"Actors\\Gone\\Behaviors\\GoneBehavior.hkx"}));
  const auto sweep = hkx::sweep_subgraphs(open_roots({dir.path()}), {});
  EXPECT_EQ(sweep.manifests, 1u);
  EXPECT_EQ(sweep.behaviors_missing, 1u);
  EXPECT_TRUE(sweep.ok());
  EXPECT_FALSE(sweep.complete());
}

// Constraint: detects a behaviour path outside a Behaviors folder, or an archive that could not be opened, being left out of the sweep verdict.
TEST(SubgraphSweep, ASkippedBehaviourPathOrArchiveMakesTheSweepIncomplete) {
  fo4::test::TempDataDir dir;
  const std::string base = "Meshes/AnimTextData/AnimationFileData/";
  write_bytes(dir.path() / "Meshes/Actors/Bar/Behaviors/BarBehavior.hkx", fo4::test::hkx::behavior_graph_packfile());
  write_text(dir.path() / (base + "222.txt"), manifest_text("222", {"Actors\\Bar\\Behaviors\\BarBehavior.hkx"}));
  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();
  const auto clean = hkx::run_subgraph_sweep(request, {});
  EXPECT_EQ(clean.behaviors_read, 1u);
  EXPECT_EQ(clean.behaviors_skipped, 0u);
  EXPECT_TRUE(clean.complete());

  write_bytes(dir.path() / "Bad.ba2", Bytes{std::byte{1}, std::byte{2}, std::byte{3}});
  const auto bad_archive = hkx::run_subgraph_sweep(request, {});
  EXPECT_TRUE(bad_archive.incomplete);
  EXPECT_FALSE(bad_archive.complete());
  EXPECT_TRUE(std::any_of(bad_archive.notes.begin(), bad_archive.notes.end(),
                          [](const std::string& note) { return note.find("Bad.ba2 was skipped") != std::string::npos; }));
  std::filesystem::remove(dir.path() / "Bad.ba2");

  write_text(dir.path() / (base + "333.txt"), manifest_text("333", {"Actors\\Odd\\Odd.hkx"}));
  const auto skipped = hkx::run_subgraph_sweep(request, {});
  EXPECT_EQ(skipped.manifests, 2u);
  EXPECT_EQ(skipped.behaviors_skipped, 1u);
  EXPECT_EQ(skipped.behaviors_missing, 0u);
  EXPECT_TRUE(skipped.ok());
  EXPECT_FALSE(skipped.complete());
}

// Constraint: detects the AnimationOffsets hint behaviour being ignored, or the gaps of one failure being kept past six while gap_total stays exact.
TEST(SubgraphSweep, ChecksTheOffsetsHintBehaviourAndCapsTheGapsPerFailure) {
  fo4::test::TempDataDir dir;
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_master_with_weapon_sets(dir, {"Rifle", "Pistol", "Axe", "Bat"});
  write_bytes(dir.path() / "Meshes/Actors/Bar/Behaviors/BarBehavior.hkx", fo4::test::hkx::behavior_graph_packfile());
  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/111.txt", manifest_text("111", {"Actors\\Bar\\Behaviors\\BarBehavior.hkx"}));
  const std::string hint = "Actors\\Foo\\Behaviors\\FooBehavior.hkx";
  std::string offsets = "V4\n";
  offsets += static_cast<char>(hint.size());
  offsets += hint;
  write_text(dir.path() / "Meshes/AnimTextData/AnimationOffsets/111.txt", offsets);

  const auto sweep = hkx::sweep_subgraphs(open_roots({dir.path()}), {});
  EXPECT_EQ(sweep.weapon_checked, 1u);
  ASSERT_EQ(sweep.failures.size(), 1u);
  EXPECT_EQ(sweep.failures[0].id, 111u);
  EXPECT_EQ(sweep.failures[0].gap_total, 8u);
  EXPECT_EQ(sweep.failures[0].gaps.size(), hkx::k_max_weapon_findings);
}

// Constraint: detects new, gone and clustered ids being mixed up, a shadowed vanilla manifest still counting as present, or a new weapon behaviour being missed.
TEST(SubgraphDiff, ComparesVanillaWithAModdedOverlay) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  const std::string base = "Meshes/AnimTextData/AnimationFileData/";
  write_text(data.path() / (base + "111.txt"), manifest_text("111", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  const std::string fresh = "Actors\\New\\Behaviors\\NewWeapon.hkx";
  write_text(mod.path() / (base + "111.txt"), manifest_text("999", {fresh}));
  write_text(mod.path() / (base + "222.txt"), manifest_text("222", {fresh}));
  write_bytes(mod.path() / "Meshes/Actors/New/Behaviors/NewWeapon.hkx",
              weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));

  const auto diff = hkx::diff_subgraph_coverage(open_roots({data.path()}), open_roots({mod.path(), data.path()}), {});
  EXPECT_EQ(diff.vanilla_manifests, 1u);
  EXPECT_EQ(diff.modded_manifests, 2u);
  EXPECT_EQ(diff.new_ids, (std::vector<std::uint64_t>{222, 999}));
  EXPECT_EQ(diff.gone_ids, (std::vector<std::uint64_t>{111}));
  ASSERT_EQ(diff.new_behaviors.size(), 1u);
  EXPECT_EQ(diff.new_behaviors[0], "actors\\new\\behaviors\\newweapon.hkx");
  ASSERT_EQ(diff.clusters.size(), 1u);
  EXPECT_EQ(diff.clusters[0].second, 2u);
  EXPECT_EQ(diff.new_weapon_behaviors, diff.new_behaviors);
  EXPECT_TRUE(diff.complete());

  hkx::DiffLimits tiny;
  tiny.max_listed = 1;
  const auto capped = hkx::diff_subgraph_coverage(open_roots({data.path()}), open_roots({mod.path(), data.path()}), tiny);
  EXPECT_EQ(capped.new_ids_total, 2u);
  EXPECT_EQ(capped.new_ids.size(), 1u);
  EXPECT_TRUE(capped.truncated_lists);
  EXPECT_TRUE(capped.complete());
}

// Constraint: detects a new behaviour whose file cannot be found being left out of the verdict.
TEST(SubgraphDiff, ANewBehaviourThatCannotBeReadMakesTheDiffIncomplete) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  write_text(data.path() / "Meshes/AnimTextData/AnimationFileData/111.txt", manifest_text("111", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  write_text(mod.path() / "Meshes/AnimTextData/AnimationFileData/222.txt", manifest_text("222", {"Actors\\New\\Behaviors\\Gone.hkx"}));
  const auto diff = hkx::diff_subgraph_coverage(open_roots({data.path()}), open_roots({mod.path(), data.path()}), {});
  EXPECT_FALSE(diff.incomplete);
  EXPECT_EQ(diff.new_behaviors_total, 1u);
  EXPECT_EQ(diff.weapon_missing, 1u);
  EXPECT_FALSE(diff.complete());
}

// Constraint: detects a diff with an empty vanilla or modded manifest index reading as complete, or the note not naming the empty side.
TEST(SubgraphDiff, AnEmptySideMakesTheDiffIncompleteAndNamesTheSide) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  const std::string manifest = "Meshes/AnimTextData/AnimationFileData/222.txt";
  const auto has_note = [](const hkx::SubgraphDiff& diff, std::string_view side) {
    return std::any_of(diff.notes.begin(), diff.notes.end(), [&](const std::string& note) { return note.rfind(side, 0) == 0; });
  };

  write_text(mod.path() / manifest, manifest_text("222", {"Actors\\New\\Behaviors\\New.hkx"}));
  const auto no_vanilla = hkx::diff_subgraph_coverage(open_roots({data.path()}), open_roots({mod.path(), data.path()}), {});
  EXPECT_TRUE(no_vanilla.incomplete);
  EXPECT_FALSE(no_vanilla.complete());
  EXPECT_TRUE(has_note(no_vanilla, "vanilla: no subgraph manifest"));
  EXPECT_FALSE(has_note(no_vanilla, "modded: no subgraph manifest"));

  fo4::test::TempDataDir other;
  write_text(data.path() / manifest, manifest_text("222", {"Actors\\New\\Behaviors\\New.hkx"}));
  const auto no_mods = hkx::diff_subgraph_coverage(open_roots({data.path()}), open_roots({other.path()}), {});
  EXPECT_TRUE(no_mods.incomplete);
  EXPECT_FALSE(no_mods.complete());
  EXPECT_TRUE(has_note(no_mods, "modded: no subgraph manifest"));
  EXPECT_FALSE(has_note(no_mods, "vanilla: no subgraph manifest"));
}

// Constraint: detects a diff running without an MO2 instance and silently comparing the Data folder with itself.
TEST(SubgraphDiff, RefusesWithoutAnMo2Instance) {
  fo4::test::TempDataDir dir;
  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();
  EXPECT_THROW(hkx::run_subgraph_diff(request, {}), hkx::AnimationSourceError);
}

// Constraint: detects a missing animation counted as present, a bare-number id shown as confirmed, an unreadable or empty log read as no id, or an oversized log read in part.
TEST(CrashAnalysis, NamesTheManifestMissingAnimationsAndTheConfidence) {
  fo4::test::TempDataDir dir;
  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/4455667788.txt",
             manifest_text("4455667788", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx", "Actors\\Foo\\Animations\\a.hkx",
                                           "Actors\\Foo\\Animations\\missing.hkx"}));
  write_text(dir.path() / "Meshes/Actors/Foo/Animations/a.hkx", "x");
  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();

  hkx::CrashInput input;
  input.text = "crashed near 4455667788 in the animation graph";
  const auto guess = hkx::analyze_crash_subgraph(request, input, {});
  ASSERT_TRUE(guess.id_found);
  EXPECT_FALSE(guess.confirmed);
  EXPECT_TRUE(guess.manifest_found);
  EXPECT_EQ(guess.manifests, 1u);
  EXPECT_EQ(guess.animation_total, 2u);
  EXPECT_EQ(guess.animation_present, 1u);
  EXPECT_EQ(guess.missing, (std::vector<std::string>{"Actors\\Foo\\Animations\\missing.hkx"}));

  input.text = "AnimationOffsets\\4455667788.txt";
  EXPECT_TRUE(hkx::analyze_crash_subgraph(request, input, {}).confirmed);

  input.text = "AnimationOffsets\\9999999999.txt";
  const auto unknown = hkx::analyze_crash_subgraph(request, input, {});
  EXPECT_TRUE(unknown.id_found);
  EXPECT_FALSE(unknown.manifest_found);
  EXPECT_EQ(unknown.manifests, 0u);
  EXPECT_TRUE(unknown.incomplete);

  const auto log = dir.path() / "crash.log";
  write_text(log, "AnimationOffsets\\4455667788.txt");
  hkx::CrashInput from_file;
  from_file.log = log;
  EXPECT_TRUE(hkx::analyze_crash_subgraph(request, from_file, {}).manifest_found);

  hkx::CrashLimits tiny_log;
  tiny_log.max_log_bytes = 4;
  const auto refused = hkx::analyze_crash_subgraph(request, from_file, tiny_log);
  EXPECT_FALSE(refused.refusal.empty());
  EXPECT_FALSE(refused.id_found);

  hkx::CrashInput nothing;
  EXPECT_FALSE(hkx::analyze_crash_subgraph(request, nothing, {}).refusal.empty());
  from_file.log = dir.path() / "absent.log";
  EXPECT_FALSE(hkx::analyze_crash_subgraph(request, from_file, {}).refusal.empty());

  const auto empty_log = dir.path() / "empty.log";
  write_text(empty_log, "");
  hkx::CrashInput empty_input;
  empty_input.log = empty_log;
  const auto empty = hkx::analyze_crash_subgraph(request, empty_input, {});
  EXPECT_NE(empty.refusal.find("is empty"), std::string::npos);
  EXPECT_FALSE(empty.id_found);
}

// Constraint: detects a log and text together being read as one of them without saying so, or a named behaviour that was never opened leaving a result that looks fully checked.
TEST(CrashAnalysis, RefusesLogAndTextTogetherAndNamesUncheckedBehaviours) {
  fo4::test::TempDataDir dir;
  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/4455667788.txt",
             manifest_text("4455667788", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();
  const auto log = dir.path() / "crash.log";
  write_text(log, "AnimationOffsets\\4455667788.txt");

  hkx::CrashInput both;
  both.log = log;
  both.text = "AnimationOffsets\\4455667788.txt";
  const auto refused = hkx::analyze_crash_subgraph(request, both, {});
  EXPECT_FALSE(refused.refusal.empty());
  EXPECT_FALSE(refused.id_found);

  hkx::CrashInput from_text;
  from_text.text = "AnimationOffsets\\4455667788.txt";
  const auto missing = hkx::analyze_crash_subgraph(request, from_text, {});
  EXPECT_TRUE(missing.refusal.empty());
  EXPECT_TRUE(missing.incomplete);
  EXPECT_EQ(missing.behaviors_checked, 0u);
  EXPECT_EQ(missing.unchecked_behaviors, (std::vector<std::string>{"Actors\\Foo\\Behaviors\\FooBehavior.hkx: missing"}));

  write_bytes(dir.path() / "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx", fo4::test::hkx::behavior_graph_packfile());
  const auto read = hkx::analyze_crash_subgraph(request, from_text, {});
  EXPECT_EQ(read.behaviors_checked, 1u);
  EXPECT_TRUE(read.unchecked_behaviors.empty());
  EXPECT_FALSE(read.incomplete);
}

// Constraint: detects crash lookup with no manifest for the id or with no behaviour paths leaving incomplete false.
TEST(CrashAnalysis, MissingManifestOrNoBehavioursMarkIncomplete) {
  fo4::test::TempDataDir dir;
  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();
  hkx::CrashInput input;

  input.text = "AnimationOffsets\\9999999999.txt";
  const auto no_manifest = hkx::analyze_crash_subgraph(request, input, {});
  EXPECT_TRUE(no_manifest.id_found);
  EXPECT_FALSE(no_manifest.manifest_found);
  EXPECT_TRUE(no_manifest.incomplete);
  EXPECT_TRUE(std::any_of(no_manifest.notes.begin(), no_manifest.notes.end(), [](const std::string& note) {
    return note.find("no manifest carries subgraph id") != std::string::npos;
  }));

  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/4455667788.txt", manifest_text("4455667788", {}));
  input.text = "AnimationOffsets\\4455667788.txt";
  const auto no_behaviours = hkx::analyze_crash_subgraph(request, input, {});
  EXPECT_TRUE(no_behaviours.manifest_found);
  EXPECT_TRUE(no_behaviours.behavior_paths.empty());
  EXPECT_EQ(no_behaviours.behaviors_checked, 0u);
  EXPECT_TRUE(no_behaviours.incomplete);
  EXPECT_TRUE(std::any_of(no_behaviours.notes.begin(), no_behaviours.notes.end(), [](const std::string& note) {
    return note.find("no behaviour files") != std::string::npos;
  }));
}

namespace {

const hkx::ModlistFinding* find_kind(const hkx::ModlistScan& scan, std::string_view kind) {
  for (const auto& finding : scan.findings) {
    if (finding.kind == kind) return &finding;
  }
  return nullptr;
}

}  // namespace

// Constraint: detects a vanilla file being scanned as a mod's, a missing declared animation passing, or an existing one being reported.
TEST(ModlistScan, CharactersListOnlyTheModsFilesAndTheirMissingAnimations) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  write_bytes(mod.path() / "Meshes/Actors/Foo/Characters/Foo.hkx",
              fo4::test::hkx::character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx",
                                                 {{"Animations\\a.hkx", "Animations\\b.hkx"}}));
  write_bytes(data.path() / "Meshes/Actors/Vanilla/Characters/Vanilla.hkx",
              fo4::test::hkx::character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\V.hkx", {{"Animations\\gone.hkx"}}));
  write_text(data.path() / "Meshes/Actors/Foo/Animations/a.hkx", "x");

  const auto scan = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), hkx::ModlistScanMode::characters, {});
  EXPECT_EQ(scan.mod_roots, 1u);
  EXPECT_EQ(scan.candidates, 1u);
  EXPECT_EQ(scan.checked, 1u);
  EXPECT_EQ(scan.broken, 1u);
  EXPECT_EQ(scan.missing, 1u);
  EXPECT_EQ(scan.round_trip_losses, 0u);
  EXPECT_EQ(scan.findings_total, 1u);
  const auto* finding = find_kind(scan, "missing-animations");
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->path, "meshes/actors/foo/characters/foo.hkx");
  EXPECT_EQ(finding->details, (std::vector<std::string>{"Animations\\b.hkx"}));
  EXPECT_FALSE(scan.ok());
  EXPECT_TRUE(scan.complete());
}

// Constraint: detects a clip that a weapon folder copy resolves being reported missing, or a clip that resolves nowhere being passed.
TEST(ModlistScan, ClipsResolveThroughWeaponFoldersAndReportTheRest) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  fo4::test::hkx::BehaviorFixtureOptions options;
  options.clip_a = "Animations\\Idle.hkx";
  options.clip_b = "Animations\\A.hkt";
  options.clip_c = "Animations\\C.hkt";
  write_bytes(mod.path() / "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx", fo4::test::hkx::behavior_graph_packfile(options));
  write_text(data.path() / "Meshes/Actors/Foo/Animations/Weapon/Pistol/Idle.hkx", "x");
  write_text(data.path() / "Meshes/Actors/Foo/Animations/A.hkt", "x");

  const auto scan = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), hkx::ModlistScanMode::clips, {});
  EXPECT_EQ(scan.candidates, 1u);
  EXPECT_EQ(scan.checked, 1u);
  EXPECT_EQ(scan.missing, 1u);
  const auto* finding = find_kind(scan, "missing-clip-animations");
  ASSERT_NE(finding, nullptr);
  ASSERT_EQ(finding->details.size(), 1u);
  EXPECT_NE(finding->details[0].find("Animations\\C.hkt"), std::string::npos);

  hkx::ModlistScanLimits no_animations;
  no_animations.max_animations = 0;
  const auto cut = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), hkx::ModlistScanMode::clips, no_animations);
  EXPECT_TRUE(cut.truncated_animations);
  EXPECT_FALSE(cut.complete());
}

// Constraint: detects an animation that could not be read counting as checked, with the scan still reporting a clean and complete result.
TEST(ModlistScan, AnAnimationThatCannotBeReadMakesTheScanIncomplete) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  fo4::test::hkx::BehaviorFixtureOptions options;
  options.clip_a = "Animations/Idle.hkx";
  options.clip_b = "Animations/Idle.hkx";
  options.clip_c = "Animations/Idle.hkx";
  write_bytes(mod.path() / "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx", fo4::test::hkx::behavior_graph_packfile(options));

  const std::filesystem::path archive_path = data.path() / "Anims.ba2";
  fo4::archive::Ba2GeneralArchive archive;
  archive.entries.push_back(ba2_fixture::general("meshes\\actors\\foo\\animations\\idle.hkx", "x"));
  fo4::archive::write_ba2_general_file(archive, archive_path);

  const std::vector<std::filesystem::path> roots{mod.path(), data.path()};
  const std::vector<std::filesystem::path> archives{archive_path};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, archives);
  std::ofstream appended(archive_path, std::ios::binary | std::ios::app);
  appended.put('x');
  appended.close();

  const auto scan = hkx::scan_modlist(assets, data.path(), hkx::ModlistScanMode::clips, {});
  EXPECT_EQ(scan.candidates, 1u);
  EXPECT_EQ(scan.checked, 1u);
  EXPECT_EQ(scan.missing, 0u);
  EXPECT_EQ(scan.unreadable, 0u);
  EXPECT_EQ(scan.findings_total, 0u);
  EXPECT_TRUE(scan.incomplete);
  EXPECT_FALSE(scan.complete());
  EXPECT_TRUE(std::any_of(scan.notes.begin(), scan.notes.end(), [](const std::string& note) {
    return note.find("could not read") != std::string::npos && note.find("round trip") != std::string::npos;
  }));
}

// Constraint: detects a file cap passing as a complete scan, and a data-only load order scanning the game's own files.
TEST(ModlistScan, FlagsACutScanAndHasNothingToScanWithoutAModRoot) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  for (const char* name : {"A", "B"}) {
    write_bytes(mod.path() / (std::string("Meshes/Actors/") + name + "/Characters/" + name + ".hkx"),
                fo4::test::hkx::character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\X.hkx", {{"Animations\\a.hkx"}}));
  }
  hkx::ModlistScanLimits one;
  one.max_files = 1;
  const auto cut = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), hkx::ModlistScanMode::characters, one);
  EXPECT_EQ(cut.candidates, 2u);
  EXPECT_EQ(cut.scanned, 1u);
  EXPECT_EQ(cut.round_trip_losses, 0u);
  EXPECT_TRUE(cut.truncated_files);
  EXPECT_FALSE(cut.complete());

  hkx::ModlistScanLimits one_finding;
  one_finding.max_findings = 1;
  const auto few = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), hkx::ModlistScanMode::characters, one_finding);
  EXPECT_EQ(few.round_trip_losses, 0u);
  EXPECT_EQ(few.broken, 2u);
  EXPECT_EQ(few.findings_total, 2u);
  EXPECT_EQ(few.findings.size(), 1u);
  EXPECT_TRUE(few.truncated_findings);
  EXPECT_TRUE(few.complete());

  const auto none = hkx::scan_modlist(open_roots({data.path()}), data.path(), hkx::ModlistScanMode::characters, {});
  EXPECT_EQ(none.candidates, 0u);
  EXPECT_FALSE(none.notes.empty());
  EXPECT_TRUE(none.incomplete);
  EXPECT_FALSE(none.complete());

  hkx::AnimationSourceRequest request;
  request.data_dir = data.path();
  EXPECT_THROW(hkx::run_modlist_scan(request, hkx::ModlistScanMode::characters, {}), hkx::AnimationSourceError);
}

// Constraint: detects a scan that found no mod-owned file reading as complete, or the note not saying what was not scanned.
TEST(ModlistScan, AnEmptyModlistIsIncompleteAndNamesWhatWasNotScanned) {
  fo4::test::TempDataDir data;
  fo4::test::TempDataDir mod;
  write_text(mod.path() / "Meshes/Actors/Foo/Animations/a.hkx", "x");
  for (const auto mode : {hkx::ModlistScanMode::characters, hkx::ModlistScanMode::clips}) {
    const auto scan = hkx::scan_modlist(open_roots({mod.path(), data.path()}), data.path(), mode, {});
    EXPECT_EQ(scan.mod_roots, 1u);
    EXPECT_EQ(scan.candidates, 0u);
    EXPECT_TRUE(scan.incomplete);
    EXPECT_FALSE(scan.complete());
    ASSERT_FALSE(scan.notes.empty());
    EXPECT_NE(scan.notes.back().find(mode == hkx::ModlistScanMode::characters ? "/characters/" : "/behaviors/"), std::string::npos);
  }
}

// Constraint: detects a weapon probe that could not be read leaving the sweep, crash, project and modlist verdicts complete and clean.
TEST(WeaponProbeFailure, MakesTheSweepCrashProjectAndModlistVerdictsIncomplete) {
  fo4::test::TempDataDir dir;
  write_master_with_weapon_sets(dir, {"Rifle\\..\\..\\Z"});
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_bytes(dir.path() / "Meshes/Actors/Foo/Behaviors/OtherBehavior.hkx",
              weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Walk.hkx"));
  write_bytes(dir.path() / "Meshes/Actors/Foo/Behaviors/SecondWeapon.hkx",
              weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  const std::string base = "Meshes/AnimTextData/AnimationFileData/";
  write_text(dir.path() / (base + "111.txt"), manifest_text("111", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  const auto assets = open_roots({dir.path()});

  const auto sweep = hkx::sweep_subgraphs(assets, {});
  EXPECT_EQ(sweep.failures_total, 0u);
  EXPECT_TRUE(sweep.incomplete);
  EXPECT_FALSE(sweep.complete());

  hkx::AnimationSourceRequest request;
  request.data_dir = dir.path();
  hkx::CrashInput input;
  input.text = "AnimationOffsets\\111.txt";
  const auto crash = hkx::analyze_crash_subgraph(request, input, {});
  EXPECT_TRUE(crash.incomplete);

  const auto check = check_foo(assets);
  const auto* probe = find_code(check, "chain.weapon_probe_failed");
  ASSERT_NE(probe, nullptr);
  EXPECT_EQ(probe->level, "error");
  ASSERT_EQ(check.per_file.size(), 3u);
  EXPECT_EQ(check.read, 3u);
  EXPECT_EQ(count_code(check, "chain.weapon_probe_failed"), 2u);
  for (const auto& file : check.per_file) {
    const bool weapon_free = file.path.find("OtherBehavior") != std::string::npos;
    std::size_t own = 0;
    for (const auto& finding : check.findings) own += finding.file == file.path && finding.code == "chain.weapon_probe_failed";
    EXPECT_EQ(own, weapon_free ? 0u : 1u) << file.path;
  }

  fo4::test::TempDataDir mod;
  write_bytes(mod.path() / "Meshes/Actors/Foo/Behaviors/FooBehavior.hkx",
              weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  const auto scan = hkx::scan_modlist(open_roots({mod.path(), dir.path()}), dir.path(), hkx::ModlistScanMode::clips, {});
  EXPECT_GT(scan.unreadable, 0u);
  EXPECT_NE(find_kind(scan, "unreadable-behaviour"), nullptr);
}

// Constraint: detects an unreadable Fallout4.esm falling back to folder inference while the sweep still reads as complete.
TEST(WeaponProbeFailure, AnUnreadableMasterMakesTheSweepIncomplete) {
  fo4::test::TempDataDir dir;
  write_bytes(dir.path() / "Fallout4.esm", Bytes{std::byte{1}, std::byte{2}, std::byte{3}});
  write_foo_project(dir, weapon_behavior("Animations\\Idle.hkx", "Animations\\Fire.hkx", "Animations\\Weapon\\Rifle\\Idle.hkx"));
  write_text(dir.path() / "Meshes/AnimTextData/AnimationFileData/111.txt", manifest_text("111", {"Actors\\Foo\\Behaviors\\FooBehavior.hkx"}));
  const auto sweep = hkx::sweep_subgraphs(open_roots({dir.path()}), {});
  EXPECT_TRUE(sweep.incomplete);
  EXPECT_FALSE(sweep.complete());
  EXPECT_NE(sweep.weapon_sets_reason.find("Fallout4.esm could not be read"), std::string::npos);
}
