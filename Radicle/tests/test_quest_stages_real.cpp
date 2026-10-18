// Real-data exercise of the QUST stage decoder, and the measurements the
// grammar was derived from. Gated on FO4_DATA_DIR; CI has no game files.

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
constexpr auto k_indx = fo4::Signature::from_string("INDX");
constexpr auto k_qobj = fo4::Signature::from_string("QOBJ");
constexpr auto k_anam = fo4::Signature::from_string("ANAM");

// Counts INDX markers in the stage array only, so the expectation is derived
// from the file rather than from the decoder under test.
std::size_t stage_markers(const fo4::Record& record) {
  std::size_t markers = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == k_qobj || sub.signature == k_anam) break;
    if (sub.signature == k_indx) ++markers;
  }
  return markers;
}

}  // namespace

TEST(QuestStagesRealData, DecodesEveryShippedQuest) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::size_t quests = 0;
  std::size_t stages = 0;
  std::size_t entries = 0;
  std::size_t localized_text = 0;
  std::size_t empty_stages = 0;
  std::size_t multi_entry = 0;
  std::vector<std::string> failures;
  int files = 0;

  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop03.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;

    const auto plugin = fo4::read_plugin_file(path);
    const fo4::RecordIndex index{plugin};
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != k_qust) continue;
      ++quests;
      const auto expected = stage_markers(*record);
      try {
        const auto decoded = fo4::forms::decode_quest_stages(index, record->form_key);
        if (decoded.stages.size() != expected) {
          failures.push_back(std::string{name} + " " + record->form_key.to_string() +
                             ": decoded " + std::to_string(decoded.stages.size()) +
                             " stages, file has " + std::to_string(expected) + " INDX markers");
          continue;
        }
        for (const auto& stage : decoded.stages) {
          ++stages;
          if (stage.log_entries.empty()) ++empty_stages;
          if (stage.log_entries.size() > 1) ++multi_entry;
          for (const auto& entry : stage.log_entries) {
            ++entries;
            if (entry.log_text.has_value() && entry.log_text->localized()) ++localized_text;
          }
        }
      } catch (const std::exception& error) {
        failures.push_back(std::string{name} + " " + record->form_key.to_string() + ": " +
                           error.what());
      }
    }
  }

  if (files == 0) {
    GTEST_SKIP() << "no official masters found under " << dir;
  }
  for (std::size_t i = 0; i < failures.size() && i < 25; ++i) {
    ADD_FAILURE() << failures[i];
  }
  EXPECT_TRUE(failures.empty()) << failures.size() << " quest(s) failed to decode";

  EXPECT_GT(quests, 1000u);
  EXPECT_GT(stages, 8000u);
  EXPECT_GT(entries, 8000u);
  // Every CNAM in a shipped quest is a string ID, never inline text.
  EXPECT_GT(localized_text, 900u);
  // Both edge shapes the grammar has to allow really occur.
  EXPECT_GT(empty_stages, 50u);
  EXPECT_GT(multi_entry, 200u);
}

namespace {

class QuestConditionCorpus : public testing::TestWithParam<const char*> {};

TEST_P(QuestConditionCorpus, EveryStoredRunIsLossless) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto cis1 = fo4::Signature::from_string("CIS1");
  const auto cis2 = fo4::Signature::from_string("CIS2");
  const auto condition_member = [&](const auto& member) {
    return member.signature == fo4::sig_ctda || member.signature == cis1 || member.signature == cis2;
  };
  std::size_t quests = 0, conditions = 0, padding = 0, runs = 0;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature != k_qust) continue;
    ++quests;
    SCOPED_TRACE(record->form_key.to_string());
    for (std::size_t i = 0; i < record->subrecords.size();) {
      if (!condition_member(record->subrecords[i])) { ++i; continue; }
      const auto begin = i;
      std::size_t raw_count = 0;
      while (i < record->subrecords.size() && condition_member(record->subrecords[i])) {
        const auto& member = record->subrecords[i++];
        if (member.signature != fo4::sig_ctda) continue;
        ++raw_count;
        ASSERT_GE(member.data.size(), 4u);
        padding += member.data[1] != std::byte{0} || member.data[2] != std::byte{0} || member.data[3] != std::byte{0};
      }
      SCOPED_TRACE("condition run at member " + std::to_string(begin));
      const auto run = std::span<const fo4::Subrecord>(record->subrecords).subspan(begin, i - begin);
      std::vector<fo4::CtdaCondition> parsed;
      std::size_t consumed = 0;
      ASSERT_TRUE(fo4::forms::lossless_quest_log_entry_conditions(run, parsed, consumed));
      EXPECT_EQ(consumed, run.size());
      EXPECT_EQ(parsed.size(), raw_count);
      const auto editable = fo4::read_ctda_conditions_in(*record, begin, i);
      EXPECT_EQ(editable.size(), raw_count);
      const auto rewritten = fo4::serialize_ctda_run(editable);
      ASSERT_EQ(rewritten.size(), run.size());
      for (std::size_t member = 0; member < run.size(); ++member) {
        EXPECT_EQ(rewritten[member].signature, run[member].signature);
        EXPECT_EQ(rewritten[member].data, run[member].data);
      }
      conditions += raw_count;
      ++runs;
    }
  }
  if (std::string{GetParam()} == "Fallout4.esm") {
    const auto* mq101 = fo4::find_record(plugin, fo4::FormKey::parse("01ED86:Fallout4.esm"));
    ASSERT_NE(mq101, nullptr);
    std::size_t mq101_conditions = 0;
    std::size_t mq101_padding = 0;
    for (const auto& member : mq101->subrecords) {
      if (member.signature != fo4::sig_ctda) continue;
      ++mq101_conditions;
      ASSERT_GE(member.data.size(), 4u);
      if (member.data[1] != std::byte{0} || member.data[2] != std::byte{0} ||
          member.data[3] != std::byte{0}) ++mq101_padding;
    }
    EXPECT_GT(mq101_conditions, 0u);
    EXPECT_GT(mq101_padding, 0u);
  }
  EXPECT_GT(quests, 0u);
  RecordProperty("QuestCount", quests);
  RecordProperty("ConditionCount", conditions);
  RecordProperty("ConditionRunCount", runs);
  RecordProperty("PaddingCount", padding);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, QuestConditionCorpus, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
