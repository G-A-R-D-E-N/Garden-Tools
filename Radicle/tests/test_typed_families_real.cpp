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
#include "real_data_build.hpp"

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
  record_game_build(detect_game_build(dir));
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

TEST(TypedFamiliesRealData, DecodesEveryShippedRace) {
  run_family({"RACE", "RACE", [](const fo4::Plugin& p, const fo4::FormKey& k) {
                static_cast<void>(fo4::forms::decode_race(p, k));
              }});
}

// Every shipped master sets TES4 localized (0x80), so RACE FULL/DESC arrive as
// four-byte string IDs. Decoding must surface them as IDs, not reject them.
TEST(TypedFamiliesRealData, ShippedRaceStringsAreLocalizedIds) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Fallout4.esm not found under " << dir;
  }

  const auto plugin = fo4::read_plugin_file(path);
  ASSERT_TRUE(plugin.header.tes4_flags.is_localized());

  const auto race = fo4::Signature::from_string("RACE");
  std::size_t seen = 0;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature != race) continue;
    const auto decoded = fo4::forms::decode_race(plugin, record->form_key);
    EXPECT_TRUE(decoded.description.localized())
        << decoded.key.to_string() << " DESC should be a string ID";
    EXPECT_FALSE(decoded.description.text.has_value());
    if (decoded.full_name.has_value()) {
      EXPECT_TRUE(decoded.full_name->localized());
    }
    // EDID is never localized and must still be inline text.
    EXPECT_TRUE(decoded.editor_id.has_value());
    ++seen;
  }
  EXPECT_GT(seen, 40u);
}

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

// ARMO DESC is exactly one per record across all 688 shipped ARMO, and every
// master is localized, so it must decode as a string ID.
TEST(TypedFamiliesRealData, ShippedArmorDescriptionsAreLocalizedIds) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Fallout4.esm not found under " << dir;
  }

  const auto plugin = fo4::read_plugin_file(path);
  ASSERT_TRUE(plugin.header.tes4_flags.is_localized());

  const auto armo = fo4::Signature::from_string("ARMO");
  std::size_t seen = 0;
  std::size_t described = 0;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature != armo) continue;
    const auto decoded = fo4::forms::decode_armor(plugin, record->form_key);
    ++seen;
    if (!decoded.description.has_value()) continue;
    ++described;
    EXPECT_TRUE(decoded.description->localized())
        << decoded.key.to_string() << " DESC should be a string ID";
    EXPECT_FALSE(decoded.description->text.has_value());
  }
  EXPECT_GT(seen, 600u);
  // Every shipped ARMO carries exactly one DESC.
  EXPECT_EQ(described, seen);
}
