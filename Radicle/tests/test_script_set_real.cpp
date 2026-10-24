#include "fo4/archive/assets.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_pex.h"
#include "fo4/c_api_script_set.h"
#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
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

TEST(ScriptSetReal, ModArchiveEntrySpellingFoldsToTheOfficialScriptName) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  ASSERT_FALSE(archives.empty());

  std::string target_name;
  std::string official_spelling;
  std::vector<std::byte> official_bytes;
  std::size_t official_winners = 0;
  {
    const auto official_resolver = fo4::archive::AssetResolver::open({}, archives);
    const auto official = fo4::pex_analysis::resolve_script_set(
        fo4::pex_analysis::enumerate_script_candidates(official_resolver));
    ASSERT_FALSE(official.empty());
    const auto target = std::find_if(official.begin(), official.end(), [](const auto& candidate) {
      return candidate.winner.kind == fo4::pex_analysis::ScriptSourceKind::archive &&
             candidate.winner.name.find('/') != std::string::npos;
    });
    ASSERT_NE(target, official.end());
    target_name = target->winner.name;
    official_winners = official.size();
    official_bytes = official_resolver.read(target->winner.location);
    const auto& entry = official_resolver.archives()
                            .at(target->winner.location.archive)
                            .entries()
                            .at(target->winner.location.entry);
    official_spelling = entry.path;
  }
  ASSERT_FALSE(official_bytes.empty());
  ASSERT_EQ(fo4::pex_analysis::script_name_from_entry(official_spelling),
            std::optional<std::string>(target_name));

  // The mod entry must name the same script with only its case and separators spelled differently.
  std::string mod_spelling;
  for (const char ch : official_spelling) {
    if (ch == '/' || ch == '\\') {
      mod_spelling.push_back(ch == '/' ? '\\' : '/');
      continue;
    }
    mod_spelling.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
  }
  ASSERT_NE(mod_spelling, official_spelling);
  ASSERT_EQ(fo4::archive::normalize_asset_path(mod_spelling), "scripts/" + target_name + ".pex");
  ASSERT_EQ(fo4::pex_analysis::script_name_from_entry(mod_spelling),
            std::optional<std::string>(target_name));

  const auto mod_payload = marker_bytes(std::string_view("shadowed by a respelled entry"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), {{mod_spelling, mod_payload}});

  std::vector<std::filesystem::path> mod_last = archives;
  mod_last.push_back(mod_archive);
  const auto resolver = fo4::archive::AssetResolver::open({}, mod_last);
  const auto candidates = fo4::pex_analysis::enumerate_script_candidates(resolver);
  const auto winners = fo4::pex_analysis::resolve_script_set(candidates);

  // Invariant: respelling a name adds a candidate but no name, so it can only shadow, never extend the set.
  EXPECT_EQ(candidates.size(), official_winners + 1);
  EXPECT_EQ(winners.size(), official_winners);
  const auto winner = std::find_if(winners.begin(), winners.end(), [&](const auto& candidate) {
    return candidate.winner.name == target_name;
  });
  ASSERT_NE(winner, winners.end());
  EXPECT_EQ(std::filesystem::path(winner->winner.container).filename(), mod_archive.filename());
  EXPECT_EQ(winner->replaced, 1u);
  EXPECT_EQ(winner->winner.path, "scripts/" + target_name + ".pex");
  EXPECT_EQ(resolver.read(winner->winner.location), mod_payload);
  std::size_t replaced = 0;
  for (const auto& candidate : winners) replaced += candidate.replaced;
  EXPECT_EQ(replaced, 1u);
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
  std::string respelled_name;
  std::string official_spelling;
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
    const auto respelled = std::find_if(winners.begin(), winners.end(), [&](const auto& winner) {
      return winner.winner.name != target_name &&
             winner.winner.kind == fo4::pex_analysis::ScriptSourceKind::archive &&
             winner.winner.name.find('/') != std::string::npos;
    });
    ASSERT_NE(respelled, winners.end());
    respelled_name = respelled->winner.name;
    const auto& entry = resolver.archives()
                            .at(respelled->winner.location.archive)
                            .entries()
                            .at(respelled->winner.location.entry);
    official_spelling = entry.path;
  }

  std::string added_name = "radicle_c_api_new_script";
  while (official_names.contains(added_name)) added_name += "_x";
  // The third entry must name an official script with only its case and separators spelled differently.
  std::string respelled_spelling;
  for (const char ch : official_spelling) {
    if (ch == '/' || ch == '\\') {
      respelled_spelling.push_back(ch == '/' ? '\\' : '/');
      continue;
    }
    respelled_spelling.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
  }
  ASSERT_NE(respelled_spelling, official_spelling);
  const auto mod_payload = marker_bytes(std::string_view("c api shadow"));
  const auto added_payload = marker_bytes(std::string_view("c api added"));
  const auto respelled_payload = marker_bytes(std::string_view("c api respelled"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), {
      {archive_entry_for(target_name), mod_payload},
      {archive_entry_for(added_name), added_payload},
      {respelled_spelling, respelled_payload},
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
  std::optional<std::size_t> respelled_index;
  std::size_t replaced_total = 0;
  for (std::size_t index = 0; index < count; ++index) {
    radicle_script_set_winner winner{};
    ASSERT_EQ(radicle_script_set_get(set.get(), index, &winner), RADICLE_STATUS_OK) << radicle_last_error();
    const std::string name = winner.name == nullptr ? std::string() : std::string(winner.name);
    replaced_total += winner.replaced;
    if (name == target_name) shadow_index = index;
    if (name == added_name) added_index = index;
    if (name == respelled_name) respelled_index = index;
  }
  ASSERT_TRUE(shadow_index.has_value());
  ASSERT_TRUE(added_index.has_value());
  ASSERT_TRUE(respelled_index.has_value());

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

  // Invariant: a respelled entry must report the canonical script path, not the spelling its archive stores.
  radicle_script_set_winner respelled{};
  ASSERT_EQ(radicle_script_set_get(set.get(), *respelled_index, &respelled), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(respelled.winner.path), "scripts/" + respelled_name + ".pex");
  EXPECT_NE(std::string(respelled.winner.path), respelled_spelling);
  EXPECT_EQ(std::filesystem::path(respelled.winner.container).filename(), mod_archive.filename());
  EXPECT_EQ(respelled.winner.origin, RADICLE_ASSET_ARCHIVE);
  EXPECT_EQ(respelled.replaced, 1u);
  EXPECT_EQ(read_winner(*respelled_index), respelled_payload);
  EXPECT_EQ(replaced_total, 2u);
}

TEST(ScriptSetReal, CApiScriptResolveFoldsACallerBuiltNameThatIsSpelledDifferently) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  ASSERT_FALSE(archives.empty());

  std::vector<fo4::pex_analysis::ScriptCandidate> retail;
  std::string target_name;
  std::string official_spelling;
  std::size_t retail_winners = 0;
  std::int32_t highest_index = 0;
  {
    const auto resolver = fo4::archive::AssetResolver::open({}, archives);
    retail = fo4::pex_analysis::enumerate_script_candidates(resolver);
    ASSERT_FALSE(retail.empty());
    for (const auto& candidate : retail) highest_index = std::max(highest_index, candidate.load_index);
    const auto winners = fo4::pex_analysis::resolve_script_set(retail);
    retail_winners = winners.size();
    const auto target = std::find_if(winners.begin(), winners.end(), [](const auto& winner) {
      return winner.winner.kind == fo4::pex_analysis::ScriptSourceKind::archive &&
             winner.winner.name.find('/') != std::string::npos;
    });
    ASSERT_NE(target, winners.end());
    target_name = target->winner.name;
    const auto& entry = resolver.archives()
                            .at(target->winner.location.archive)
                            .entries()
                            .at(target->winner.location.entry);
    official_spelling = entry.path;
  }

  // The caller holds this spelling of the script and must fold it before naming a candidate with it.
  std::string caller_spelling;
  for (const char ch : official_spelling) {
    if (ch == '/' || ch == '\\') {
      caller_spelling.push_back(ch == '/' ? '\\' : '/');
      continue;
    }
    caller_spelling.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
  }
  ASSERT_NE(caller_spelling, official_spelling);
  uint8_t* folded = nullptr;
  std::size_t folded_size = 0;
  ASSERT_EQ(radicle_script_normalize_name(reinterpret_cast<const uint8_t*>(caller_spelling.data()),
                                          caller_spelling.size(), &folded, &folded_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string folded_name(reinterpret_cast<const char*>(folded), folded_size);
  radicle_free_bytes(folded);
  ASSERT_EQ(folded_name, target_name);

  const auto payload = marker_bytes(std::string_view("resolved as the same name"));
  fo4::test::TempDataDir mod_dir;
  const auto mod_archive = write_mod_archive(mod_dir.path(), {{caller_spelling, payload}});
  const auto container = mod_archive.string();

  std::vector<radicle_script_candidate> rows;
  rows.reserve(retail.size() + 1);
  for (const auto& candidate : retail) {
    const auto kind = static_cast<uint8_t>(candidate.kind == fo4::pex_analysis::ScriptSourceKind::loose
                                               ? RADICLE_PEX_SOURCE_LOOSE
                                               : RADICLE_PEX_SOURCE_ARCHIVE);
    rows.push_back({reinterpret_cast<const uint8_t*>(candidate.name.data()), candidate.name.size(), kind,
                    reinterpret_cast<const uint8_t*>(candidate.container.data()), candidate.container.size(),
                    candidate.load_index, candidate.size});
  }
  const auto mod_row = rows.size();
  rows.push_back({reinterpret_cast<const uint8_t*>(folded_name.data()), folded_name.size(), RADICLE_PEX_SOURCE_ARCHIVE,
                  reinterpret_cast<const uint8_t*>(container.data()), container.size(), highest_index + 1,
                  payload.size()});

  radicle_script_winner* winners = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_script_resolve(rows.data(), rows.size(), &winners, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_script_winner, void (*)(radicle_script_winner*)> guard(winners,
                                                                                      radicle_script_free_winners);
  // Invariant: a caller name folded from another spelling adds no name, so the winner count cannot grow.
  EXPECT_EQ(count, retail_winners);
  const auto winner = std::find_if(winners, winners + count, [&](const auto& row) {
    return row.candidate_index == mod_row;
  });
  ASSERT_NE(winner, winners + count);
  EXPECT_EQ(winner->replaced, 1u);
  std::size_t replaced = 0;
  for (std::size_t i = 0; i < count; ++i) replaced += winners[i].replaced;
  EXPECT_EQ(replaced, 1u);

  // The resolver must group by the name bytes it is handed, so an unfolded caller spelling is its own script.
  auto unfolded = rows;
  unfolded.back().name = reinterpret_cast<const uint8_t*>(caller_spelling.data());
  unfolded.back().name_size = caller_spelling.size();
  radicle_script_winner* unfolded_winners = nullptr;
  std::size_t unfolded_count = 0;
  ASSERT_EQ(radicle_script_resolve(unfolded.data(), unfolded.size(), &unfolded_winners, &unfolded_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_script_winner, void (*)(radicle_script_winner*)> unfolded_guard(
      unfolded_winners, radicle_script_free_winners);
  EXPECT_EQ(unfolded_count, retail_winners + 1);
  const auto unfolded_winner = std::find_if(unfolded_winners, unfolded_winners + unfolded_count,
                                            [&](const auto& row) { return row.candidate_index == mod_row; });
  ASSERT_NE(unfolded_winner, unfolded_winners + unfolded_count);
  EXPECT_EQ(unfolded_winner->replaced, 0u);
}
