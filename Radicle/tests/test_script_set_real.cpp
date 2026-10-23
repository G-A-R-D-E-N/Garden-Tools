// TODO-verify: the 10331 candidate and winner pins are not yet measured against the Fallout 4 Data folder.
#include "fo4/archive/assets.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

TEST(ScriptSetReal, VanillaAndDlcArchivesResolveToTheScannedWinnerCount) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path data_dir = dir;
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  const auto resolver = fo4::archive::AssetResolver::open({}, archives);
  const auto candidates = fo4::pex_analysis::enumerate_script_candidates(resolver);
  const auto winners = fo4::pex_analysis::resolve_script_set(candidates);
  EXPECT_EQ(candidates.size(), 10331u);
  EXPECT_EQ(winners.size(), 10331u);
}
