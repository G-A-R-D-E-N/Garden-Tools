// Real-data exercise of the QUST stage decoder, and the measurements the
// grammar was derived from. Gated on FO4_DATA_DIR; CI has no game files.

#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"

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
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != k_qust) continue;
      ++quests;
      const auto expected = stage_markers(*record);
      try {
        const auto decoded = fo4::forms::decode_quest_stages(plugin, record->form_key);
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

TEST(QuestStagesRealData, MQ101ConditionsRoundTripOpaqueBytes) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run this";
  }
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("01ED86:Fallout4.esm"));
  ASSERT_NE(record, nullptr);
  std::size_t conditions = 0;
  std::size_t opaque = 0;
  for (const auto& member : record->subrecords) {
    if (member.signature != fo4::sig_ctda) continue;
    SCOPED_TRACE("MQ101 CTDA ordinal " + std::to_string(conditions));
    ++conditions;
    const auto condition = fo4::parse_ctda_condition(member.data);
    EXPECT_EQ(fo4::serialize_ctda_condition(condition), member.data);
    ASSERT_GE(member.data.size(), 4u);
    if (member.data[1] != std::byte{0} || member.data[2] != std::byte{0} ||
        member.data[3] != std::byte{0}) ++opaque;
  }
  EXPECT_GT(conditions, 0u);
  EXPECT_GT(opaque, 0u);
  RecordProperty("ConditionCount", conditions);
  RecordProperty("OpaquePrefixCount", opaque);
}
