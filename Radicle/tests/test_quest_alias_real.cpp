// Real-data exercise of the QUST alias decoder.
//
// #73's remaining acceptance is "save/reload round-trip". The synthetic tests
// cover the shapes we author; this one runs the decoder over every QUST
// Bethesda ships, which is where unmodelled fills, cardinality violations and
// marker-order surprises actually live.
//
// Needs the game's Data folder, so it is gated on FO4_DATA_DIR and skips
// without it. CI has no game files and will skip.

#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr auto k_qust = fo4::Signature::from_string("QUST");
constexpr auto k_alst = fo4::Signature::from_string("ALST");
constexpr auto k_alls = fo4::Signature::from_string("ALLS");
constexpr auto k_alcs = fo4::Signature::from_string("ALCS");
constexpr auto k_almi = fo4::Signature::from_string("ALMI");

std::size_t marker_count(const fo4::Record& record) {
  std::size_t markers = 0;
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size();) {
    if (subs[i].signature == k_alst || subs[i].signature == k_alls) {
      ++markers;
      ++i;
    } else if (subs[i].signature == k_alcs) {
      ++markers;
      while (i < subs.size() && subs[i].signature == k_alcs) {
        ++i;
        if (i < subs.size() && subs[i].signature == k_almi) ++i;
      }
    } else {
      ++i;
    }
  }
  return markers;
}

}  // namespace

TEST(QuestAliasRealData, DecodesEveryShippedQuest) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::size_t quests = 0;
  std::size_t aliases = 0;
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
      ++quests;

      const auto expected = marker_count(*record);
      try {
        const auto decoded = fo4::forms::decode_quest_aliases(index, record->form_key);
        aliases += decoded.aliases.size();
        // Every start marker has to surface as exactly one modelled alias, or
        // the decoder is silently dropping or merging entries.
        if (decoded.aliases.size() != expected) {
          failures.push_back(std::string{name} + " " + record->form_key.to_string() +
                             ": decoded " + std::to_string(decoded.aliases.size()) +
                             " aliases, file has " + std::to_string(expected) + " markers");
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

  // Guard against a vacuous pass if the walk finds nothing.
  EXPECT_GT(quests, 1000u);
  EXPECT_GT(aliases, 10000u);
}
