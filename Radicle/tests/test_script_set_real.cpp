#include "fo4/archive/assets.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

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
