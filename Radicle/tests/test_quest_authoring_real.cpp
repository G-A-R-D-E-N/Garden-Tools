// Byte-exact round-trip of the QUST stage and objective writers over every
// shipped quest. Decoding then writing back unchanged must reproduce the
// record's exact subrecord sequence -- if it does not, authoring silently
// rewrites Bethesda's data.
//
// Gated on FO4_DATA_DIR; CI has no game files and will skip.

#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"
#include "real_sample_plugin.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr auto k_qust = fo4::Signature::from_string("QUST");

struct Snapshot {
  std::vector<std::pair<std::string, std::vector<std::byte>>> members;
};

Snapshot snapshot(const fo4::Record& record) {
  Snapshot out;
  for (const auto& sub : record.subrecords) {
    out.members.emplace_back(sub.signature.to_string(), sub.data);
  }
  return out;
}

std::string describe(const Snapshot& before, const Snapshot& after) {
  if (before.members.size() != after.members.size()) {
    return "subrecord count " + std::to_string(before.members.size()) + " -> " +
           std::to_string(after.members.size());
  }
  for (std::size_t i = 0; i < before.members.size(); ++i) {
    if (before.members[i].first != after.members[i].first) {
      return "at " + std::to_string(i) + ": " + before.members[i].first + " -> " +
             after.members[i].first;
    }
    if (before.members[i].second != after.members[i].second) {
      return "payload changed at " + std::to_string(i) + " (" + before.members[i].first + ")";
    }
  }
  return {};
}

}  // namespace

TEST(QuestAuthoringRealData, PartialFormMasterFragmentsSurviveSerialization) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder";
  record_game_build(detect_game_build(dir));

  const std::pair<const char*, const char*> cases[] = {
      {"DLCCoast.esm", "0AE51C:Fallout4.esm"},
      {"DLCNukaWorld.esm", "05DEE4:Fallout4.esm"},
  };
  for (const auto& [name, form_key] : cases) {
    SCOPED_TRACE(name);
    const auto path = std::filesystem::path{dir} / name;
    ASSERT_TRUE(std::filesystem::exists(path));
    auto plugin = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse(form_key);
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->header.flags.test(0x4000));
    const auto before = snapshot(*record);
    ASSERT_EQ(before.members.size(), 2u);
    ASSERT_EQ(before.members[1].first, "VMAD");
    const auto flags = record->header.flags;
    const auto stages = fo4::forms::decode_quest_stages(plugin, key);
    ASSERT_TRUE(stages.stages.empty());
    fo4::forms::write_quest_stages(plugin, key, stages);
    fo4::forms::write_quest_objectives(
        plugin, key, fo4::forms::decode_quest_objectives(plugin, key));

    const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), name);
    const auto* after = fo4::find_record(reloaded, key);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->header.flags, flags);
    EXPECT_EQ(snapshot(*after).members, before.members);
    EXPECT_TRUE(fo4::forms::decode_quest_stages(reloaded, key).stages.empty());
  }
}

TEST(QuestAuthoringRealData, RewritingUnchangedStagesAndObjectivesIsByteExact) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::size_t quests = 0;
  std::vector<std::string> failures;
  int files = 0;

  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;

    const auto reduced = fo4::test::make_sample(fo4::read_plugin_file(path), [](const fo4::Record& record) {
      return record.header.signature == k_qust;
    }, 0);
    std::vector<fo4::FormKey> keys;
    for (const auto* record : fo4::collect_records(reduced)) {
      if (record->header.signature == k_qust) keys.push_back(record->form_key);
    }

    for (const auto& key : keys) {
      ++quests;
      const auto* before_record = fo4::find_record(reduced, key);
      ASSERT_NE(before_record, nullptr);
      const auto before = snapshot(*before_record);

      auto working = reduced;
      try {
        const auto stages = fo4::forms::decode_quest_stages(working, key);
        fo4::forms::write_quest_stages(working, key, stages);
        const auto objectives = fo4::forms::decode_quest_objectives(working, key);
        fo4::forms::write_quest_objectives(working, key, objectives);
      } catch (const std::exception& error) {
        failures.push_back(std::string{name} + " " + key.to_string() + ": " + error.what());
        continue;
      }

      const auto* after_record = fo4::find_record(working, key);
      ASSERT_NE(after_record, nullptr);
      const auto diff = describe(before, snapshot(*after_record));
      if (!diff.empty()) {
        failures.push_back(std::string{name} + " " + key.to_string() + ": " + diff);
      }
    }
  }

  if (files == 0) GTEST_SKIP() << "no official masters found under " << dir;
  for (std::size_t i = 0; i < failures.size() && i < 20; ++i) ADD_FAILURE() << failures[i];
  EXPECT_TRUE(failures.empty()) << failures.size() << " quest(s) did not round-trip";
  EXPECT_GT(quests, 1000u);
}
