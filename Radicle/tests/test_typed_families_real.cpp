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
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace {

struct Family {
  fo4::Signature signature;
  const char* label;
  bool zero_allowed;
  std::function<void(const fo4::Plugin&, const fo4::FormKey&)> decode;
  std::size_t seen{0};
  std::vector<std::string> failures;
};

std::vector<std::filesystem::path> masters(const char* dir) {
  std::vector<std::filesystem::path> out;
  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm"}) {
    auto path = std::filesystem::path{dir} / name;
    if (std::filesystem::exists(path)) out.push_back(std::move(path));
  }
  return out;
}

// Every shipped master sets TES4 localized (0x80), so RACE FULL/DESC arrive as
// four-byte string IDs. Decoding must surface them as IDs, not reject them.
void expect_localized_ids(const fo4::Plugin& plugin) {
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

  // ARMO DESC is exactly one per record across all 688 shipped ARMO, and every
  // master is localized, so it must decode as a string ID.
  const auto armo = fo4::Signature::from_string("ARMO");
  seen = 0;
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

}  // namespace

TEST(TypedFamiliesRealData, DecodesEveryShippedRecordFamily) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto fallout = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(fallout)) {
    GTEST_SKIP() << "Fallout4.esm not found under " << dir;
  }

  std::vector<Family> families{
      {fo4::Signature::from_string("RACE"), "RACE", false,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_race(p, k));
       },
       0,
       {}},
      {fo4::Signature::from_string("RACE"), "RACE BodyData", false,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_race_body_data(p, k));
       },
       0,
       {}},
      {fo4::Signature::from_string("ARMO"), "ARMO", false,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_armor(p, k));
       },
       0,
       {}},
      {fo4::Signature::from_string("ARMA"), "ARMA", false,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_armor_addon(p, k));
       },
       0,
       {}},
      {fo4::Signature::from_string("EYES"), "EYES", true,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_eyes(p, k));
       },
       0,
       {}},
      {fo4::Signature::from_string("LVSP"), "LVSP", true,
       [](const fo4::Plugin& p, const fo4::FormKey& k) {
         static_cast<void>(fo4::forms::decode_leveled_spell(p, k));
       },
       0,
       {}},
  };

  for (const auto& path : masters(dir)) {
    auto plugin = fo4::read_plugin_file(path);
    if (path == fallout) expect_localized_ids(plugin);
    for (const auto* record : fo4::collect_records(plugin)) {
      for (auto& family : families) {
        if (record->header.signature != family.signature) continue;
        ++family.seen;
        try {
          family.decode(plugin, record->form_key);
        } catch (const std::exception& error) {
          family.failures.push_back(path.filename().string() + " " + record->form_key.to_string() +
                                    ": " + error.what());
        }
      }
    }
  }

  for (const auto& family : families) {
    for (std::size_t i = 0; i < family.failures.size() && i < 15; ++i) {
      ADD_FAILURE() << family.failures[i];
    }
    EXPECT_TRUE(family.failures.empty())
        << family.label << ": " << family.failures.size() << " of " << family.seen
        << " failed to decode";
  }

  // Vanilla Fallout 4 ships no records of some supported types -- EYES and
  // LVSP are both valid FO4 signatures with zero instances in the masters --
  // so an empty sweep is not a failure.
  for (const auto& family : families) {
    if (family.seen == 0 && !family.zero_allowed) {
      GTEST_SKIP() << "no " << family.label << " records in the shipped masters";
    }
  }
}

// Invariant: the SPCT count must equal the number of SPLO subrecords on the record.
TEST(TypedFamiliesRealData, RaceSpellCountAgreesWithTheActorEffectList) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto shipped = masters(dir);
  if (shipped.size() != 7) {
    GTEST_SKIP() << "the measured totals need all seven official masters, found " << shipped.size();
  }
  const auto field = fo4::schema::scalar_field_descriptor("RACE\\SpellCount");
  ASSERT_TRUE(field.has_value());
  const auto race = fo4::Signature::from_string("RACE");
  const auto spct = fo4::Signature::from_string("SPCT");
  const auto splo = fo4::Signature::from_string("SPLO");
  std::size_t records = 0;
  std::size_t with_count = 0;
  std::size_t one = 0;
  std::size_t two = 0;
  std::size_t odd_size = 0;
  std::size_t mismatched = 0;
  std::size_t effects_without_count = 0;
  for (const auto& path : shipped) {
    auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != race) continue;
      ++records;
      std::size_t effects = 0;
      std::span<const std::byte> counts;
      for (const auto& sub : record->subrecords) {
        if (sub.signature == splo) ++effects;
        if (sub.signature == spct) counts = sub.data;
      }
      if (counts.empty()) {
        if (effects != 0) ++effects_without_count;
        continue;
      }
      ++with_count;
      if (counts.size() != 4) {
        ++odd_size;
        continue;
      }
      const auto text = fo4::schema::decode_scalar(
          fo4::schema::scalar_field_bytes(counts, field->scalar), field->scalar);
      if (text == "1") ++one;
      if (text == "2") ++two;
      if (text != std::to_string(effects)) ++mismatched;
    }
  }
  EXPECT_EQ(records, 110u);
  EXPECT_EQ(with_count, 91u);
  EXPECT_EQ(one, 56u);
  EXPECT_EQ(two, 35u);
  EXPECT_EQ(odd_size, 0u);
  EXPECT_EQ(mismatched, 0u);
  EXPECT_EQ(effects_without_count, 0u);
}
