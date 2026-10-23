#include "fo4/papyrus/script_set.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2GeneralEntry;
using fo4::papyrus::ScriptCandidate;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::string text_of(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

Ba2GeneralEntry general(std::string path, std::string_view payload) {
  Ba2GeneralEntry entry;
  const auto hashes = fo4::archive::compute_ba2_hashes(path);
  entry.name_hash = hashes.name;
  entry.extension_hash = hashes.extension;
  entry.directory_hash = hashes.directory;
  entry.path = std::move(path);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes_of(payload)));
  return entry;
}

ScriptCandidate candidate_of(std::string name, fo4::archive::AssetOrigin origin, std::string container,
                             std::int32_t load_index) {
  ScriptCandidate candidate;
  candidate.name = std::move(name);
  candidate.path = "scripts/" + candidate.name + ".pex";
  candidate.location = {origin, {}, 0, 0};
  candidate.container = std::move(container);
  candidate.load_index = load_index;
  return candidate;
}

class ScriptSetTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_script_set_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write_loose(const std::filesystem::path& dir, std::string_view relative,
                                    std::string_view payload) {
    const auto path = dir / std::filesystem::path(std::string(relative));
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return path;
  }

  std::filesystem::path write_archive(const std::filesystem::path& dir, std::string_view name,
                                      std::vector<Ba2GeneralEntry> entries) {
    Ba2GeneralArchive archive;
    archive.has_string_table = true;
    archive.entries = std::move(entries);
    const auto path = dir / std::string(name);
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
  }

  std::filesystem::path root_;
};

}  // namespace

TEST(ScriptName, NormalizesLoosePaths) {
  EXPECT_EQ(fo4::papyrus::normalize_script_name("Actors\\Foo.PEX"), "actors/foo");
  EXPECT_EQ(fo4::papyrus::normalize_script_name("/Fragments/bar.pex"), "fragments/bar");
  EXPECT_EQ(fo4::papyrus::normalize_script_name("Foo"), "foo");
}

TEST(ScriptName, ReadsOnlySafeScriptsEntries) {
  EXPECT_EQ(fo4::papyrus::script_name_from_entry("Scripts\\Actors\\Foo.PEX"), std::optional<std::string>{"actors/foo"});
  EXPECT_EQ(fo4::papyrus::script_name_from_entry("/scripts/a b.pex"), std::optional<std::string>{"a b"});
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Data/Scripts/foo.pex").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Meshes/foo.pex").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts/foo.psc").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts/").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts/a<b.pex").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts/./foo.pex").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts/trailing./foo.pex").has_value());
  EXPECT_FALSE(fo4::papyrus::script_name_from_entry("Scripts\\..\\foo.pex").has_value());
}

TEST(ScriptPrecedence, LooseBeatsArchive) {
  auto loose = candidate_of("foo", fo4::archive::AssetOrigin::loose, "Data", 0);
  auto archived = candidate_of("foo", fo4::archive::AssetOrigin::archive, "Mod.ba2", 9);
  EXPECT_EQ(fo4::papyrus::resolve_script_set(std::vector<ScriptCandidate>{archived, loose})[0].winner.container,
            "Data");
}

TEST(ScriptPrecedence, HigherLoadIndexWins) {
  auto low = candidate_of("foo", fo4::archive::AssetOrigin::loose, "Low", 3);
  auto high = candidate_of("foo", fo4::archive::AssetOrigin::loose, "High", 4);
  EXPECT_EQ(fo4::papyrus::resolve_script_set(std::vector<ScriptCandidate>{high, low})[0].winner.container, "High");
}

TEST(ScriptPrecedence, GreaterContainerBreaksTheTie) {
  auto first = candidate_of("foo", fo4::archive::AssetOrigin::loose, "ModA", 5);
  auto second = candidate_of("foo", fo4::archive::AssetOrigin::loose, "ModB", 5);
  EXPECT_EQ(fo4::papyrus::resolve_script_set(std::vector<ScriptCandidate>{first, second})[0].winner.container,
            "ModB");
  EXPECT_EQ(fo4::papyrus::resolve_script_set(std::vector<ScriptCandidate>{second, first})[0].winner.container,
            "ModB");
}

TEST(ScriptPrecedence, CountsTheReplacedCandidates) {
  const std::vector<ScriptCandidate> candidates{
      candidate_of("foo", fo4::archive::AssetOrigin::archive, "Old.ba2", 0),
      candidate_of("foo", fo4::archive::AssetOrigin::archive, "New.ba2", 1),
      candidate_of("foo", fo4::archive::AssetOrigin::loose, "Data", 2),
  };
  const auto winners = fo4::papyrus::resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].replaced, 2u);
  EXPECT_EQ(winners[0].winner.load_index, 2);
}

TEST(ScriptPrecedence, OrdersWinnersByName) {
  const std::vector<ScriptCandidate> candidates{
      candidate_of("zeta", fo4::archive::AssetOrigin::loose, "Data", 0),
      candidate_of("alpha", fo4::archive::AssetOrigin::loose, "Data", 0),
  };
  const auto winners = fo4::papyrus::resolve_script_set(candidates);
  ASSERT_EQ(winners.size(), 2u);
  EXPECT_EQ(winners[0].name, "alpha");
  EXPECT_EQ(winners[1].name, "zeta");
}

TEST_F(ScriptSetTest, LooseWinsAndTheArchiveCopyIsCountedAsReplaced) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/Actors/Foo.pex", "loose");
  const auto archive = write_archive(root_, "Mod - Main.ba2", {general("Scripts\\Actors\\Foo.pex", "archived")});
  const std::vector<std::filesystem::path> roots{data};
  const std::vector<std::filesystem::path> archives{archive};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, archives);

  const auto winners = fo4::papyrus::resolve_script_set(assets);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].name, "actors/foo");
  EXPECT_EQ(winners[0].replaced, 1u);
  EXPECT_EQ(winners[0].winner.location.origin, fo4::archive::AssetOrigin::loose);
  EXPECT_EQ(winners[0].winner.container, data.string());
  EXPECT_EQ(winners[0].winner.path, "scripts/actors/foo.pex");
  EXPECT_EQ(text_of(assets.read(winners[0].winner.location)), "loose");
}

TEST_F(ScriptSetTest, RootOrderIsTheDefaultLoadIndex) {
  const auto high = root_ / "ModHigh";
  const auto low = root_ / "ModLow";
  write_loose(high, "Scripts/foo.pex", "high");
  write_loose(low, "Scripts/foo.pex", "low");
  const std::vector<std::filesystem::path> roots{high, low};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});

  const auto winners = fo4::papyrus::resolve_script_set(assets);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].winner.container, high.string());
  EXPECT_EQ(winners[0].winner.load_index, 1);
  EXPECT_EQ(text_of(assets.read(winners[0].winner.location)), "high");
}

TEST_F(ScriptSetTest, ExplicitLoadIndexOutranksTheRootOrder) {
  const auto high = root_ / "ModHigh";
  const auto low = root_ / "ModLow";
  write_loose(high, "Scripts/foo.pex", "high");
  write_loose(low, "Scripts/foo.pex", "low");
  // Invariant: roots are opened high to low, so only the explicit index can move the winner.
  const std::vector<std::filesystem::path> roots{high, low};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  const std::vector<std::int32_t> indexes{2, 7};

  const auto winners = fo4::papyrus::resolve_script_set(assets, indexes);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].winner.container, low.string());
  EXPECT_EQ(winners[0].winner.load_index, 7);
  EXPECT_EQ(text_of(assets.read(winners[0].winner.location)), "low");
}

TEST_F(ScriptSetTest, LaterArchiveWins) {
  const auto old_archive = write_archive(root_, "Old.ba2", {general("Scripts\\Foo.pex", "old")});
  const auto new_archive = write_archive(root_, "New.ba2", {general("Scripts\\foo.pex", "new")});
  const std::vector<std::filesystem::path> archives{old_archive, new_archive};
  const auto assets = fo4::archive::AssetResolver::open_roots({}, archives);

  const auto winners = fo4::papyrus::resolve_script_set(assets);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].replaced, 1u);
  EXPECT_EQ(winners[0].winner.container, new_archive.string());
  EXPECT_EQ(winners[0].winner.load_index, 1);
  EXPECT_EQ(text_of(assets.read(winners[0].winner.location)), "new");
}

TEST_F(ScriptSetTest, NonScriptAssetsAreLeftOut) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/foo.pex", "foo");
  write_loose(data, "Scripts/notes.txt", "text");
  write_loose(data, "Meshes/bar.pex", "mesh");
  const std::vector<std::filesystem::path> roots{data};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});

  const auto winners = fo4::papyrus::resolve_script_set(assets);
  ASSERT_EQ(winners.size(), 1u);
  EXPECT_EQ(winners[0].name, "foo");
}

TEST_F(ScriptSetTest, RejectsAMismatchedLoadIndexCount) {
  const auto data = root_ / "Data";
  write_loose(data, "Scripts/foo.pex", "foo");
  const std::vector<std::filesystem::path> roots{data};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  const std::vector<std::int32_t> indexes{1, 2};

  EXPECT_THROW(static_cast<void>(fo4::papyrus::resolve_script_set(assets, indexes)), std::invalid_argument);
}
