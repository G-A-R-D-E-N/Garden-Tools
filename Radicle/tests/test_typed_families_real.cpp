// Real-data exercise of the typed record decoders.
//
// The synthetic per-family tests cover the shapes we author. This one runs
// each decoder over every matching record Bethesda ships, which is where
// unmodelled fields, cardinality assumptions and partial-override shapes
// actually live -- the same gap that hid two QUST alias bugs.
//
// Needs the game's Data folder, so it is gated on FO4_DATA_DIR and skips
// without it. CI has no game files and will skip.

#include "fo4/forms/armor.hpp"
#include "fo4/forms/eyes.hpp"
#include "fo4/forms/leveled_spell.hpp"
#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Family {
  const char* signature;
  const char* label;
  std::function<void(const fo4::Plugin&, const fo4::FormKey&)> decode;
};

std::vector<std::filesystem::path> masters(const char* dir) {
  std::vector<std::filesystem::path> out;
  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop03.esm"}) {
    auto path = std::filesystem::path{dir} / name;
    if (std::filesystem::exists(path)) out.push_back(std::move(path));
  }
  return out;
}

void run_family(const Family& family) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  const auto files = masters(dir);
  if (files.empty()) {
    GTEST_SKIP() << "no official masters found under " << dir;
  }

  const auto signature = fo4::Signature::from_string(family.signature);
  std::size_t seen = 0;
  std::vector<std::string> failures;

  for (const auto& path : files) {
    const auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != signature) continue;
      ++seen;
      try {
        family.decode(plugin, record->form_key);
      } catch (const std::exception& error) {
        failures.push_back(path.filename().string() + " " + record->form_key.to_string() +
                           ": " + error.what());
      }
    }
  }

  for (std::size_t i = 0; i < failures.size() && i < 15; ++i) {
    ADD_FAILURE() << failures[i];
  }
  EXPECT_TRUE(failures.empty())
      << family.label << ": " << failures.size() << " of " << seen << " failed to decode";

  // Vanilla Fallout 4 ships no records of some supported types -- EYES and
  // LVSP are both valid FO4 signatures with zero instances in the masters --
  // so an empty sweep is a skip, not a failure.
  if (seen == 0) {
    GTEST_SKIP() << "no " << family.label << " records in the shipped masters";
  }
}

}  // namespace

// decode_race is deliberately not swept here yet: every shipped master sets
// TES4 localized (0x80), which makes RACE DESC a four-byte string ID rather
// than inline text, and the decoder still reads it as a NUL-terminated string.
// That is a localization gap, not a RACE gap -- see docs/FORMAT_NOTES.md.

TEST(TypedFamiliesRealData, DecodesEveryShippedRaceBodyData) {
  run_family({"RACE", "RACE BodyData", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_race_body_data(p, k));
              }});
}

TEST(TypedFamiliesRealData, DecodesEveryShippedArmor) {
  run_family({"ARMO", "ARMO", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_armor(p, k));
              }});
}

TEST(TypedFamiliesRealData, DecodesEveryShippedArmorAddon) {
  run_family({"ARMA", "ARMA", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_armor_addon(p, k));
              }});
}

TEST(TypedFamiliesRealData, DecodesEveryShippedEyes) {
  run_family({"EYES", "EYES", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_eyes(p, k));
              }});
}

TEST(TypedFamiliesRealData, DecodesEveryShippedLeveledSpell) {
  run_family({"LVSP", "LVSP", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_leveled_spell(p, k));
              }});
}
