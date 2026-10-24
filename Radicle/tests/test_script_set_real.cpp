#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> marker_bytes(std::string_view text) {
  std::vector<std::byte> out{std::byte{0xDE}, std::byte{0xC0}, std::byte{0x57}, std::byte{0xFA}};
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::filesystem::path write_mod_archive(const std::filesystem::path& dir, std::string_view entry_path,
                                        const std::vector<std::byte>& payload) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  fo4::archive::Ba2GeneralEntry entry;
  const auto hashes = fo4::archive::compute_ba2_hashes(entry_path);
  entry.name_hash = hashes.name;
  entry.extension_hash = hashes.extension;
  entry.directory_hash = hashes.directory;
  entry.path = std::string(entry_path);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(payload));
  archive.entries.push_back(std::move(entry));
  const auto path = dir / "ShadowMod - Main.ba2";
  fo4::archive::write_ba2_general_file(archive, path);
  return path;
}

std::string archive_entry_for(const std::string& script_name) {
  std::string entry = "Scripts\\";
  for (const char ch : script_name) entry.push_back(ch == '/' ? '\\' : ch);
  entry += ".pex";
  return entry;
}

}  // namespace

TEST(ScriptSetReal, VanillaAndDlcArchivesResolveToAWinnerSet) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  const auto resolver = fo4::archive::AssetResolver::open({}, archives);
  const auto candidates = fo4::pex_analysis::enumerate_script_candidates(resolver);
  const auto winners = fo4::pex_analysis::resolve_script_set(candidates);

  std::set<std::string> distinct_names;
  std::map<std::string, std::set<std::string>> spellings;
  for (const auto& listing : resolver.list("Scripts", ".pex", true)) {
    const auto name = fo4::pex_analysis::script_name_from_entry(listing.path);
    if (!name) continue;
    distinct_names.insert(*name);
    spellings[*name].insert(listing.path);
  }
  std::size_t replaced = 0;
  for (const auto& winner : winners) replaced += winner.replaced;

  std::size_t differing_spellings = 0;
  for (const auto& entry : spellings) {
    if (entry.second.size() > 1) ++differing_spellings;
  }

  std::size_t not_pex = 0;
  for (const auto& winner : winners) {
    const auto bytes = resolver.read(winner.winner.location);
    const bool magic = bytes.size() >= 4 && bytes[0] == std::byte{0xDE} && bytes[1] == std::byte{0xC0} &&
                       bytes[2] == std::byte{0x57} && bytes[3] == std::byte{0xFA};
    if (!magic) ++not_pex;
  }

  std::cout << "ScriptSet candidates=" << candidates.size() << " winners=" << winners.size()
            << " distinct names=" << distinct_names.size() << " replaced=" << replaced
            << " names with more than one spelling=" << differing_spellings
            << " winners without pex magic=" << not_pex << "\n";
  EXPECT_GT(winners.size(), 0u);
  EXPECT_LE(winners.size(), candidates.size());
  EXPECT_EQ(winners.size(), distinct_names.size());
  EXPECT_EQ(replaced + winners.size(), candidates.size());
  EXPECT_EQ(not_pex, 0u);
}

TEST(ScriptSetReal, ModArchiveShadowingAnOfficialScriptFollowsTheLoadIndex) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  ASSERT_FALSE(archives.empty());

  std::string target_name;
  std::filesystem::path official_container;
  std::vector<std::byte> official_bytes;
  std::size_t official_winners = 0;
  {
    const auto official_resolver = fo4::archive::AssetResolver::open({}, archives);
    const auto official = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(official_resolver));
    ASSERT_FALSE(official.empty());
    const auto& target = official.front();
    ASSERT_EQ(target.winner.kind, fo4::pex_analysis::ScriptSourceKind::archive);
    target_name = target.winner.name;
    official_container = target.winner.container;
    official_bytes = official_resolver.read(target.winner.location);
    official_winners = official.size();
  }
  ASSERT_FALSE(official_bytes.empty());

  const auto mod_payload = marker_bytes(std::string_view("shadowed by a mod archive"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), archive_entry_for(target_name), mod_payload);

  std::vector<std::filesystem::path> mod_last = archives;
  mod_last.push_back(mod_archive);
  {
    const auto shadowing = fo4::archive::AssetResolver::open({}, mod_last);
    const auto shadowed_candidates = fo4::pex_analysis::enumerate_script_candidates(shadowing);
    const auto shadowed = fo4::pex_analysis::resolve_script_set(shadowed_candidates);
    EXPECT_EQ(shadowed.size(), official_winners);
    EXPECT_EQ(shadowed_candidates.size(), official_winners + 1);
    const auto winner = std::find_if(shadowed.begin(), shadowed.end(), [&](const auto& candidate) {
      return candidate.winner.name == target_name;
    });
    ASSERT_NE(winner, shadowed.end());
    EXPECT_EQ(std::filesystem::path(winner->winner.container).filename(), mod_archive.filename());
    EXPECT_EQ(winner->replaced, 1u);
    EXPECT_EQ(shadowing.read(winner->winner.location), mod_payload);
  }

  std::vector<std::filesystem::path> mod_first = archives;
  mod_first.insert(mod_first.begin(), mod_archive);
  {
    const auto losing = fo4::archive::AssetResolver::open({}, mod_first);
    const auto losing_winners = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(losing));
    ASSERT_EQ(losing_winners.size(), official_winners);
    const auto winner = std::find_if(losing_winners.begin(), losing_winners.end(), [&](const auto& candidate) {
      return candidate.winner.name == target_name;
    });
    ASSERT_NE(winner, losing_winners.end());
    EXPECT_EQ(std::filesystem::path(winner->winner.container).filename(), official_container.filename());
    EXPECT_EQ(winner->replaced, 1u);
    EXPECT_EQ(losing.read(winner->winner.location), official_bytes);
  }
}
