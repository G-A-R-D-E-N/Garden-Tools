// Real-data exercise of the QUST objective decoder. Gated on FO4_DATA_DIR.
#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr auto k_qust = fo4::Signature::from_string("QUST");
constexpr auto k_qobj = fo4::Signature::from_string("QOBJ");
constexpr auto k_anam = fo4::Signature::from_string("ANAM");

std::size_t objective_markers(const fo4::Record& record) {
  std::size_t markers = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == k_anam) break;
    if (sub.signature == k_qobj) ++markers;
  }
  return markers;
}
}  // namespace

TEST(QuestObjectivesRealData, DecodesEveryShippedQuest) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::size_t objectives = 0, targets = 0, localized = 0, empty = 0, multi = 0, keywords = 0;
  std::vector<std::string> failures;
  int files = 0;

  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop03.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;
    auto plugin = fo4::read_plugin_file(path);
    const fo4::RecordIndex index{plugin};
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != k_qust) continue;
      const auto expected = objective_markers(*record);
      try {
        const auto decoded = fo4::forms::decode_quest_objectives(index, record->form_key);
        if (decoded.objectives.size() != expected) {
          failures.push_back(std::string{name} + " " + record->form_key.to_string() +
                             ": decoded " + std::to_string(decoded.objectives.size()) +
                             " objectives, file has " + std::to_string(expected) + " QOBJ");
          continue;
        }
        for (const auto& objective : decoded.objectives) {
          ++objectives;
          if (objective.targets.empty()) ++empty;
          if (objective.targets.size() > 1) ++multi;
          if (objective.display_text.has_value() && objective.display_text->localized()) {
            ++localized;
          }
          for (const auto& target : objective.targets) {
            ++targets;
            if (target.keyword.has_value()) ++keywords;
          }
        }
      } catch (const std::exception& error) {
        failures.push_back(std::string{name} + " " + record->form_key.to_string() + ": " +
                           error.what());
      }
    }
  }

  if (files == 0) GTEST_SKIP() << "no official masters found under " << dir;
  for (std::size_t i = 0; i < failures.size() && i < 25; ++i) ADD_FAILURE() << failures[i];
  EXPECT_TRUE(failures.empty()) << failures.size() << " quest(s) failed to decode";

  EXPECT_GT(objectives, 1000u);
  EXPECT_GT(targets, 1400u);
  // NNAM is a string ID in every shipped objective.
  EXPECT_EQ(localized, objectives);
  // Both edge shapes the grammar must allow really occur.
  EXPECT_GT(empty, 50u);
  EXPECT_GT(multi, 100u);
  // The optional keyword is rare but present -- the case a missing FormLink
  // remap slot would silently corrupt.
  EXPECT_GT(keywords, 0u);
}
