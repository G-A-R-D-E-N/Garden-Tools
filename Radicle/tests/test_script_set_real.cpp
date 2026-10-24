#include "fo4/archive/assets.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_script_set.h"
#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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

struct ModEntry {
  std::string path;
  std::vector<std::byte> payload;
};

std::filesystem::path write_mod_archive(const std::filesystem::path& dir, const std::vector<ModEntry>& files) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  for (const auto& file : files) {
    fo4::archive::Ba2GeneralEntry entry;
    const auto hashes = fo4::archive::compute_ba2_hashes(file.path);
    entry.name_hash = hashes.name;
    entry.extension_hash = hashes.extension;
    entry.directory_hash = hashes.directory;
    entry.path = file.path;
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(file.payload));
    archive.entries.push_back(std::move(entry));
  }
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
  std::set<std::string> official_names;
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
    for (const auto& winner : official) official_names.insert(winner.winner.name);
  }
  ASSERT_FALSE(official_bytes.empty());

  std::string added_name = "radicle_mod_new_script";
  while (official_names.contains(added_name)) added_name += "_x";
  const auto mod_payload = marker_bytes(std::string_view("shadowed by a mod archive"));
  const auto added_payload = marker_bytes(std::string_view("added by a mod archive"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), {
      {archive_entry_for(target_name), mod_payload},
      {archive_entry_for(added_name), added_payload},
  });

  std::vector<std::filesystem::path> mod_last = archives;
  mod_last.push_back(mod_archive);
  {
    const auto shadowing = fo4::archive::AssetResolver::open({}, mod_last);
    const auto shadowed_candidates = fo4::pex_analysis::enumerate_script_candidates(shadowing);
    const auto shadowed = fo4::pex_analysis::resolve_script_set(shadowed_candidates);
    EXPECT_EQ(shadowed.size(), official_winners + 1);
    EXPECT_EQ(shadowed_candidates.size(), official_winners + 2);
    const auto winner = std::find_if(shadowed.begin(), shadowed.end(), [&](const auto& candidate) {
      return candidate.winner.name == target_name;
    });
    ASSERT_NE(winner, shadowed.end());
    EXPECT_EQ(std::filesystem::path(winner->winner.container).filename(), mod_archive.filename());
    EXPECT_EQ(winner->replaced, 1u);
    EXPECT_EQ(shadowing.read(winner->winner.location), mod_payload);
    // Invariant: a name only the mod defines is added, not shadowed, so it must carry no replaced count.
    const auto added = std::find_if(shadowed.begin(), shadowed.end(), [&](const auto& candidate) {
      return candidate.winner.name == added_name;
    });
    ASSERT_NE(added, shadowed.end());
    EXPECT_EQ(std::filesystem::path(added->winner.container).filename(), mod_archive.filename());
    EXPECT_EQ(added->replaced, 0u);
    EXPECT_EQ(shadowing.read(added->winner.location), added_payload);
    std::size_t replaced = 0;
    for (const auto& candidate : shadowed) replaced += candidate.replaced;
    EXPECT_EQ(replaced, 1u);
  }

  std::vector<std::filesystem::path> mod_first = archives;
  mod_first.insert(mod_first.begin(), mod_archive);
  {
    const auto losing = fo4::archive::AssetResolver::open({}, mod_first);
    const auto losing_winners = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(losing));
    ASSERT_EQ(losing_winners.size(), official_winners + 1);
    const auto winner = std::find_if(losing_winners.begin(), losing_winners.end(), [&](const auto& candidate) {
      return candidate.winner.name == target_name;
    });
    ASSERT_NE(winner, losing_winners.end());
    EXPECT_EQ(std::filesystem::path(winner->winner.container).filename(), official_container.filename());
    EXPECT_EQ(winner->replaced, 1u);
    EXPECT_EQ(losing.read(winner->winner.location), official_bytes);
    // Invariant: the added name wins from the mod archive whatever its load index, because nothing competes for it.
    const auto added = std::find_if(losing_winners.begin(), losing_winners.end(), [&](const auto& candidate) {
      return candidate.winner.name == added_name;
    });
    ASSERT_NE(added, losing_winners.end());
    EXPECT_EQ(std::filesystem::path(added->winner.container).filename(), mod_archive.filename());
    EXPECT_EQ(added->replaced, 0u);
    EXPECT_EQ(losing.read(added->winner.location), added_payload);
  }
}

TEST(ScriptSetReal, LooseScriptBeatsAnOfficialArchiveWhateverTheLoadIndex) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));

  std::string target_name;
  std::filesystem::path official_container;
  std::vector<std::byte> official_bytes;
  std::size_t official_winners = 0;
  std::size_t official_archive_index = 0;
  {
    const auto official_resolver = fo4::archive::AssetResolver::open({}, archives);
    const auto winners = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(official_resolver));
    const auto target = std::find_if(winners.begin(), winners.end(), [](const auto& winner) {
      return winner.winner.kind == fo4::pex_analysis::ScriptSourceKind::archive &&
             winner.winner.location.archive > 0;
    });
    ASSERT_NE(target, winners.end());
    target_name = target->winner.name;
    official_container = target->winner.container;
    official_archive_index = target->winner.location.archive;
    official_bytes = official_resolver.read(target->winner.location);
    official_winners = winners.size();
  }
  ASSERT_FALSE(official_bytes.empty());

  fo4::test::TempDataDir loose_root;
  const auto loose_payload = marker_bytes(std::string_view("loose copy wins"));
  loose_root.write("Scripts/" + target_name + ".pex", loose_payload, 1);

  const std::vector<std::filesystem::path> roots{loose_root.path()};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, archives);
  // Invariant: the loose copy takes the lowest index an archive can hold, so only the kind rule can pick it.
  const std::array<std::int32_t, 1> lowest{0};
  ASSERT_GT(official_archive_index, static_cast<std::size_t>(lowest.front()));
  const auto candidates = fo4::pex_analysis::enumerate_script_candidates(resolver, lowest);
  EXPECT_EQ(candidates.size(), official_winners + 1);
  EXPECT_NE(std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
    return candidate.name == target_name && candidate.kind == fo4::pex_analysis::ScriptSourceKind::archive &&
           std::filesystem::path(candidate.container).filename() == official_container.filename();
  }), candidates.end());
  const auto winners = fo4::pex_analysis::resolve_script_set(resolver, lowest);
  ASSERT_EQ(winners.size(), official_winners);
  const auto winner = std::find_if(winners.begin(), winners.end(), [&](const auto& candidate) {
    return candidate.winner.name == target_name;
  });
  ASSERT_NE(winner, winners.end());
  EXPECT_EQ(winner->winner.kind, fo4::pex_analysis::ScriptSourceKind::loose);
  EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(winner->winner.container),
                                          loose_root.path() / "Scripts" / (target_name + ".pex")));
  EXPECT_EQ(winner->replaced, 1u);
  EXPECT_EQ(resolver.read(winner->winner.location), loose_payload);
}

TEST(ScriptSetReal, CApiResolvesTheShadowingModArchiveLikeTheCppResolver) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  ASSERT_FALSE(archives.empty());

  std::string target_name;
  std::set<std::string> official_names;
  std::size_t official_winners = 0;
  {
    const auto resolver = fo4::archive::AssetResolver::open({}, archives);
    const auto winners = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(resolver));
    ASSERT_FALSE(winners.empty());
    target_name = winners.front().winner.name;
    official_winners = winners.size();
    for (const auto& winner : winners) official_names.insert(winner.winner.name);
  }

  std::string added_name = "radicle_c_api_new_script";
  while (official_names.contains(added_name)) added_name += "_x";
  const auto mod_payload = marker_bytes(std::string_view("c api shadow"));
  const auto added_payload = marker_bytes(std::string_view("c api added"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), {
      {archive_entry_for(target_name), mod_payload},
      {archive_entry_for(added_name), added_payload},
  });

  std::vector<std::string> texts;
  for (const auto& archive : archives) texts.push_back(archive.string());
  texts.push_back(mod_archive.string());
  std::vector<const char*> paths;
  for (const auto& text : texts) paths.push_back(text.c_str());

  radicle_assets* raw_assets = nullptr;
  ASSERT_EQ(radicle_assets_open_roots(nullptr, 0, paths.data(), paths.size(), &raw_assets), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_assets, void (*)(radicle_assets*)> assets(raw_assets, radicle_assets_destroy);
  radicle_script_set* raw_set = nullptr;
  ASSERT_EQ(radicle_script_set_resolve(assets.get(), nullptr, 0, &raw_set), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_script_set, void (*)(radicle_script_set*)> set(raw_set, radicle_script_set_destroy);
  const auto count = radicle_script_set_count(set.get());
  ASSERT_EQ(count, official_winners + 1);

  std::optional<std::size_t> shadow_index;
  std::optional<std::size_t> added_index;
  for (std::size_t index = 0; index < count; ++index) {
    radicle_script_set_winner winner{};
    ASSERT_EQ(radicle_script_set_get(set.get(), index, &winner), RADICLE_STATUS_OK) << radicle_last_error();
    const std::string name = winner.name == nullptr ? std::string() : std::string(winner.name);
    if (name == target_name) shadow_index = index;
    if (name == added_name) added_index = index;
  }
  ASSERT_TRUE(shadow_index.has_value());
  ASSERT_TRUE(added_index.has_value());

  const auto read_winner = [&](std::size_t index) {
    uint8_t* bytes = nullptr;
    std::size_t size = 0;
    EXPECT_EQ(radicle_script_set_read(set.get(), index, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
    std::vector<std::byte> out;
    for (std::size_t i = 0; i < size; ++i) out.push_back(std::byte{bytes[i]});
    radicle_free_bytes(bytes);
    return out;
  };

  // Invariant: the ABI must report the same shadowing winner the C++ resolver does, not only the same name set.
  radicle_script_set_winner shadowed{};
  ASSERT_EQ(radicle_script_set_get(set.get(), *shadow_index, &shadowed), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(shadowed.winner.path), "scripts/" + target_name + ".pex");
  EXPECT_EQ(std::filesystem::path(shadowed.winner.container).filename(), mod_archive.filename());
  EXPECT_EQ(shadowed.winner.origin, RADICLE_ASSET_ARCHIVE);
  EXPECT_EQ(shadowed.replaced, 1u);
  EXPECT_EQ(read_winner(*shadow_index), mod_payload);

  radicle_script_set_winner added{};
  ASSERT_EQ(radicle_script_set_get(set.get(), *added_index, &added), RADICLE_STATUS_OK);
  EXPECT_EQ(std::filesystem::path(added.winner.container).filename(), mod_archive.filename());
  EXPECT_EQ(added.replaced, 0u);
  EXPECT_EQ(read_winner(*added_index), added_payload);
}
