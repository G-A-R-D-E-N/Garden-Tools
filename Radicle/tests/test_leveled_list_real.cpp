#include "fo4/environment/environment.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/patch/list_merge.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class LeveledListReal : public testing::TestWithParam<const char*> {};

TEST_P(LeveledListReal, EveryLeveledItemAndNpcDecodesWithoutFailure) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  if (!std::filesystem::is_regular_file(path)) {
    GTEST_SKIP() << GetParam() << " is not installed";
  }
  const auto plugin = fo4::read_plugin_file(path);
  const auto item = fo4::Signature::from_string("LVLI");
  const auto npc = fo4::Signature::from_string("LVLN");
  std::vector<fo4::FormKey> lists;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == item || record.header.signature == npc) {
      lists.push_back(record.form_key);
    }
  });

  std::size_t failures = 0;
  for (const auto& key : lists) {
    try {
      static_cast<void>(fo4::forms::decode_leveled_list(plugin, key));
    } catch (const std::exception& error) {
      ++failures;
      ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ": " << error.what();
    }
  }
  EXPECT_EQ(failures, 0u) << GetParam() << " has " << failures << " of " << lists.size()
                          << " leveled lists that failed to decode";
  if (std::string(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(lists.size(), 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, LeveledListReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

TEST(LeveledListMergeReal, Lvli210B67MergesToTheThreeRowsOfItsNukaWorldWinner) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto data = std::filesystem::path(directory);
  const std::filesystem::path paths[] = {data / "Fallout4.esm", data / "DLCNukaWorld.esm"};
  const auto env = fo4::env::Environment::load(paths);
  const auto key = fo4::FormKey::parse("210B67:Fallout4.esm");

  const auto winner = env.find_plugin("DLCNukaWorld.esm");
  ASSERT_TRUE(winner.has_value());
  EXPECT_EQ(fo4::forms::decode_leveled_list(env.plugin(*winner), key).entries.size(), 3u);

  const auto result =
      fo4::patch::build_list_merge_patch(env, fo4::Signature::from_string("LVLI"), "Merged.esp");
  std::size_t written = 0;
  fo4::visit_records(result.patch, [&](const fo4::Record& record) {
    if (record.form_key == key) ++written;
  });
  EXPECT_EQ(written, 0u) << "the merged rows differ from the winner's three rows";
}

}  // namespace
