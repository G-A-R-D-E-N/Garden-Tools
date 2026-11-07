#include "fo4/loadorder/load_order_files.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace loadorder = fo4::loadorder;

const std::vector<std::string> k_official_table{
    "DLCRobot.esm",         "DLCworkshop01.esm",          "DLCCoast.esm",
    "DLCworkshop02.esm",    "DLCworkshop03.esm",          "DLCNukaWorld.esm",
    "DLCUltraHighResolution.esm", "DLCDummy10thAnniversary.esm"};

}  // namespace

TEST(LoadOrderReal, InstalledDataFollowsFallout4OfficialThenCccOrder) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the installed load order check";
  }
  const std::filesystem::path data{dir};
  const auto build = detect_game_build(data);
  record_game_build(build);
  const auto table = build.release == GameRelease::AE ? loadorder::OfficialTable::ae : loadorder::OfficialTable::og_ng;
  const auto official_count = table == loadorder::OfficialTable::ae ? k_official_table.size() : k_official_table.size() - 1;

  const auto ccc_path = data / ".." / "Fallout4.ccc";
  const auto ccc = std::filesystem::is_regular_file(ccc_path) ? loadorder::read_ccc(ccc_path) : std::vector<std::string>{};
  const auto order = loadorder::assemble_load_order(data, loadorder::PluginList{}, ccc, table);
  const bool has_missing_master = std::any_of(order.findings.begin(), order.findings.end(),
                                              [](const auto& finding) { return finding.code == "missing_master"; });
  if (!has_missing_master) {
    EXPECT_FALSE(order.failed);
  }

  std::vector<std::string> names;
  for (const auto& entry : order.entries) names.push_back(lowered_name(entry.name));
  const auto index_of = [&](const std::string& name) -> std::ptrdiff_t {
    const auto found = std::find(names.begin(), names.end(), lowered_name(name));
    return found == names.end() ? -1 : found - names.begin();
  };

  ASSERT_GE(index_of("Fallout4.esm"), 0);
  EXPECT_EQ(index_of("Fallout4.esm"), 0);
  std::ptrdiff_t last = index_of("Fallout4.esm");
  for (std::size_t i = 0; i < official_count; ++i) {
    const auto at = index_of(k_official_table[i]);
    if (at < 0) continue;
    EXPECT_GT(at, last) << k_official_table[i];
    last = at;
  }
  for (const auto& name : ccc) {
    const auto at = index_of(name);
    if (at < 0) continue;
    EXPECT_GT(at, last) << name;
    last = at;
  }
}
