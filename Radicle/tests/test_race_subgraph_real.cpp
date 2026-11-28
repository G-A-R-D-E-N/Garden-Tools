#include "fo4/environment/furniture.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

TEST(RaceSubgraphRealData, EveryOfficialRaceAndFurnitureDecodesAndAChairKeywordNamesAChairDirectory) {
  const auto* dir = ::fo4::test::fo4_data_dir_cstr();
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  std::size_t furniture_rows = 0, chair_rows = 0, furniture_with_chair_keyword = 0;
  std::vector<fo4::FormKey> chair_targets;
  for (const auto* ref : env.plugin_records(0)) {
    const auto type = ref->record->header.signature.view();
    if (type == "RACE") {
      std::vector<fo4::env::RaceSubgraphRow> rows;
      ASSERT_NO_THROW(rows = fo4::env::decode_race_subgraphs(env, *ref)) << ref->record->form_key.to_string();
      for (const auto& row : rows) {
        if (row.role != fo4::env::k_subgraph_role_furniture) continue;
        ++furniture_rows;
        const bool chair = std::any_of(row.path_prefixes.begin(), row.path_prefixes.end(), [](std::string_view prefix) {
          std::string normalised(prefix);
          std::replace(normalised.begin(), normalised.end(), '/', '\\');
          return std::string_view{normalised}.ends_with("Furniture\\Chair\\Neutral");
        });
        if (!chair) continue;
        ++chair_rows;
        for (const auto& key : row.target_keywords) chair_targets.push_back(key);
      }
    }
  }
  for (const auto* ref : env.plugin_records(0)) {
    if (ref->record->header.signature.view() != "FURN") continue;
    std::vector<fo4::FormKey> keywords;
    ASSERT_NO_THROW(keywords = fo4::env::decode_furniture_keywords(env, *ref)) << ref->record->form_key.to_string();
    if (std::any_of(keywords.begin(), keywords.end(), [&](const fo4::FormKey& key) {
          return std::find(chair_targets.begin(), chair_targets.end(), key) != chair_targets.end();
        })) {
      ++furniture_with_chair_keyword;
    }
  }
  EXPECT_GT(furniture_rows, 0u);
  EXPECT_GT(chair_rows, 0u);
  EXPECT_GT(furniture_with_chair_keyword, 0u);
  RecordProperty("furniture_subgraph_rows", static_cast<int>(furniture_rows));
  RecordProperty("furniture_with_chair_directory_keyword", static_cast<int>(furniture_with_chair_keyword));
}
