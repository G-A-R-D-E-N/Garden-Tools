#include "fo4/archive/assets.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::archive::AssetResolver;
using fo4::pex_analysis::enumerate_script_candidates;
using fo4::pex_analysis::normalize_script_name;
using fo4::pex_analysis::resolve_script_set;
using fo4::pex_analysis::script_name_from_entry;
using fo4::pex_analysis::ScriptCandidate;
using fo4::pex_analysis::ScriptSourceKind;
using fo4::test::TempDataDir;

ScriptCandidate loose(std::string name, std::string container, std::int32_t load_index) {
  const auto path = "scripts/" + name + ".pex";
  return {std::move(name),
          ScriptSourceKind::loose,
          std::move(container),
          load_index,
          1,
          path,
          fo4::archive::AssetLocation{}};
}

ScriptCandidate archive(std::string name, std::string container, std::int32_t load_index) {
  const auto path = "scripts/" + name + ".pex";
  return {std::move(name),
          ScriptSourceKind::archive,
          std::move(container),
          load_index,
          1,
          path,
          fo4::archive::AssetLocation{}};
}

}  // namespace

TEST(ScriptSet, LooseScriptBeatsArchiveScriptWhateverTheLoadIndex) {
  const std::vector<ScriptCandidate> candidates{archive("foo", "Fallout4 - Main.ba2", 9),
                                                loose("foo", "Data/Scripts/foo.pex", 0)};
  const auto winners = resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].winner.kind, ScriptSourceKind::loose);
  EXPECT_EQ(winners[0].index, 1u);
  EXPECT_EQ(winners[0].replaced, 1u);
}

TEST(ScriptSet, HigherArchiveLoadIndexWinsWhateverTheInputOrder) {
  const std::vector<ScriptCandidate> low_first{archive("foo", "Fallout4 - Main.ba2", 1),
                                               archive("foo", "DLCRobot - Main.ba2", 3)};
  const std::vector<ScriptCandidate> high_first{archive("foo", "DLCRobot - Main.ba2", 3),
                                                archive("foo", "Fallout4 - Main.ba2", 1)};
  const auto a = resolve_script_set(low_first);
  const auto b = resolve_script_set(high_first);
  ASSERT_EQ(a.size(), 1u);
  ASSERT_EQ(b.size(), 1u);
  EXPECT_EQ(a[0].winner.container, "DLCRobot - Main.ba2");
  EXPECT_EQ(b[0].winner.container, "DLCRobot - Main.ba2");
  EXPECT_EQ(a[0].replaced, 1u);
  EXPECT_EQ(b[0].replaced, 1u);
}

TEST(ScriptSet, NormalizedNamesIgnoreCaseSeparatorsScriptsPrefixAndPexSuffix) {
  EXPECT_EQ(normalize_script_name("Data\\Scripts\\Quest\\Foo.PEX"), "quest/foo");
  EXPECT_EQ(normalize_script_name("Scripts/quest/foo.pex"), "quest/foo");

  const std::vector<ScriptCandidate> candidates{
      archive(normalize_script_name("Scripts/Quest/Foo.pex"), "Fallout4 - Main.ba2", 0),
      loose(normalize_script_name("Data\\SCRIPTS\\quest\\FOO.PEX"), "Data/Scripts/quest/FOO.pex", 0)};
  const auto winners = resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].winner.name, "quest/foo");
  EXPECT_EQ(winners[0].replaced, 1u);
}

TEST(ScriptSet, DuplicateNameInOneContainerKeepsTheFirstAndCountsTheRest) {
  const std::vector<ScriptCandidate> candidates{archive("foo", "Fallout4 - Main.ba2", 0),
                                                archive("foo", "Fallout4 - Main.ba2", 0),
                                                archive("foo", "Fallout4 - Main.ba2", 0)};
  const auto winners = resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].index, 0u);
  EXPECT_EQ(winners[0].replaced, 2u);
}

TEST(ScriptSet, ReplacedCountsEachLoserAndWinnersStayInNameOrder) {
  const std::vector<ScriptCandidate> candidates{
      archive("zeta", "a.ba2", 0),
      archive("alpha", "a.ba2", 0),
      loose("alpha", "Data/Scripts/alpha.pex", 0),
      archive("alpha", "b.ba2", 1),
      archive("mid", "a.ba2", 0),
      archive("mid", "b.ba2", 0),
  };
  const auto winners = resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 3u);
  EXPECT_EQ(winners[0].winner.name, "alpha");
  EXPECT_EQ(winners[0].replaced, 2u);
  EXPECT_EQ(winners[1].winner.name, "mid");
  EXPECT_EQ(winners[1].replaced, 1u);
  EXPECT_EQ(winners[2].winner.name, "zeta");
  EXPECT_EQ(winners[2].replaced, 0u);
}

TEST(ScriptSet, EqualKindAndLoadIndexTieBreaksByOrdinalContainerPath) {
  const std::vector<ScriptCandidate> a_first{loose("foo", "Data/a/foo.pex", 2), loose("foo", "Data/b/foo.pex", 2)};
  const std::vector<ScriptCandidate> b_first{loose("foo", "Data/b/foo.pex", 2), loose("foo", "Data/a/foo.pex", 2)};
  EXPECT_EQ(resolve_script_set(a_first)[0].winner.container, "Data/b/foo.pex");
  EXPECT_EQ(resolve_script_set(b_first)[0].winner.container, "Data/b/foo.pex");
}

TEST(ScriptSet, FirstLooseRootWinsLikeAssetResolverLocate) {
  TempDataDir first;
  TempDataDir second;
  std::filesystem::create_directories(first.path() / "Scripts" / "Quest");
  std::filesystem::create_directories(second.path() / "Scripts" / "QUEST");
  first.write("Scripts/Quest/Foo.pex", std::vector<std::byte>(1), 1);
  second.write("Scripts/QUEST/FOO.pex", std::vector<std::byte>(2), 2);

  const std::vector<std::filesystem::path> roots{first.path(), second.path()};
  const auto resolver = AssetResolver::open_roots(roots, {});
  const auto winners = resolve_script_set(enumerate_script_candidates(resolver));
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].winner.name, "quest/foo");
  EXPECT_EQ(winners[0].winner.load_index, 1);
  EXPECT_EQ(winners[0].winner.size, 1u);
  EXPECT_EQ(winners[0].replaced, 1u);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winners[0].winner.container),
                                          resolver.locate("Scripts/Quest/Foo.pex")->loose_path));
}

TEST(ScriptName, ReadsOnlySafeScriptsEntries) {
  EXPECT_EQ(script_name_from_entry("Scripts\\Actors\\Foo.PEX"), std::optional<std::string>{"actors/foo"});
  EXPECT_EQ(script_name_from_entry("/scripts/a b.pex"), std::optional<std::string>{"a b"});
  EXPECT_FALSE(script_name_from_entry("Data/Scripts/foo.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Meshes/foo.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts/foo.psc").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts/a<b.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts/./foo.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts/trailing./foo.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts\\..\\foo.pex").has_value());
  // Invariant: the rule runs after ".pex" is dropped, so these are refused instead of becoming "" and "dir/".
  EXPECT_FALSE(script_name_from_entry("Scripts/.pex").has_value());
  EXPECT_FALSE(script_name_from_entry("Scripts/dir/.pex").has_value());
}

TEST(ScriptSet, EqualKeysKeepTheFirstSeenCandidate) {
  // Invariant: the comparison is strict, so an inclusive one must not move the winner to the last equal candidate.
  const std::vector<ScriptCandidate> candidates{loose("foo", "Data/a/foo.pex", 2), loose("foo", "Data/a/foo.pex", 2)};
  const auto winners = resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].index, 0u);
  EXPECT_EQ(winners[0].replaced, 1u);
}

TEST(ScriptSet, ExplicitLoadIndexOverridesTheRootOrder) {
  TempDataDir first;
  TempDataDir second;
  first.write("Scripts/foo.pex", std::vector<std::byte>(1, std::byte{1}), 1);
  second.write("Scripts/foo.pex", std::vector<std::byte>(2, std::byte{2}), 2);
  const std::vector<std::filesystem::path> roots{first.path(), second.path()};
  const auto resolver = AssetResolver::open_roots(roots, {});

  const auto by_root_order = resolve_script_set(resolver);
  ASSERT_EQ(by_root_order.size(), 1u);
  EXPECT_EQ(by_root_order[0].winner.load_index, 1);
  EXPECT_EQ(by_root_order[0].winner.size, 1u);

  // Invariant: a caller whose roots are not in high-to-low order must still be able to express its load indexes.
  const std::vector<std::int32_t> indexes{0, 5};
  const auto by_explicit_index = resolve_script_set(resolver, indexes);
  ASSERT_EQ(by_explicit_index.size(), 1u);
  EXPECT_EQ(by_explicit_index[0].winner.load_index, 5);
  EXPECT_EQ(by_explicit_index[0].winner.size, 2u);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(by_explicit_index[0].winner.container),
                                          second.path() / "Scripts" / "foo.pex"));
}

TEST(ScriptSet, RejectsALoadIndexCountThatMissesARoot) {
  TempDataDir first;
  TempDataDir second;
  const std::vector<std::filesystem::path> roots{first.path(), second.path()};
  const auto resolver = AssetResolver::open_roots(roots, {});
  // Invariant: a short index list must not shift every later root's index.
  const std::vector<std::int32_t> short_indexes{1};
  EXPECT_THROW(enumerate_script_candidates(resolver, short_indexes), std::invalid_argument);
  EXPECT_THROW(resolve_script_set(resolver, short_indexes), std::invalid_argument);
}

TEST(ScriptSet, NonScriptAssetsAreLeftOut) {
  TempDataDir root;
  root.write("Scripts/foo.pex", std::vector<std::byte>(1, std::byte{1}), 1);
  root.write("Scripts/notes.txt", std::vector<std::byte>(1, std::byte{2}), 2);
  root.write("Meshes/bar.pex", std::vector<std::byte>(1, std::byte{3}), 3);
  const std::vector<std::filesystem::path> roots{root.path()};
  const auto resolver = AssetResolver::open_roots(roots, {});
  // Invariant: a non-".pex" payload or one outside Scripts/ must not shadow a real script name.
  const auto candidates = enumerate_script_candidates(resolver);
  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].name, "foo");
  EXPECT_EQ(candidates[0].path, "scripts/foo.pex");
}
