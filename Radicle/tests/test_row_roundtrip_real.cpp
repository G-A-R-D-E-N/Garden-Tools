#include "row_roundtrip_harness.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>

namespace {

using fo4::Signature;

}  // namespace

TEST(RowRoundtripReal, CellRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("CELL", Signature::from_string("CELL"));
}

TEST(RowRoundtripReal, RegionRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("REGN", Signature::from_string("REGN"));
}

TEST(RowRoundtripReal, WorldRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("WRLD", Signature::from_string("WRLD"));
}

TEST(RowRoundtripReal, TerminalRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("TERM", Signature::from_string("TERM"));
}

TEST(RowRoundtripReal, QuestRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("QUST", Signature::from_string("QUST"));
}

TEST(RowRoundtripReal, ObjectModRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("OMOD", Signature::from_string("OMOD"));
}

TEST(RowRoundtripReal, PerkRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("PERK", Signature::from_string("PERK"));
}

TEST(RowRoundtripReal, InfoRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("INFO", Signature::from_string("INFO"));
}

TEST(RowRoundtripReal, FootstepSetRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("FSTS", Signature::from_string("FSTS"));
}

TEST(RowRoundtripReal, FootstepSetEditorIdSurvivesALengthChange) {
  row_roundtrip::roundtrip_strings("FSTS", Signature::from_string("FSTS"));
}

TEST(RowRoundtripReal, WeaponEditorIdSurvivesALengthChange) {
  row_roundtrip::roundtrip_strings("WEAP", Signature::from_string("WEAP"));
}

TEST(RowRoundtripReal, FootstepSetCategoryListsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_lists("FSTS", Signature::from_string("FSTS"),
                                 {"FSTS\\WalkingFootsteps", "FSTS\\RunningFootsteps",
                                  "FSTS\\SprintingFootsteps", "FSTS\\SneakingFootsteps",
                                  "FSTS\\SwimmingFootsteps"});
}

TEST(RowRoundtripReal, LensRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("LENS", Signature::from_string("LENS"));
}

TEST(RowRoundtripReal, MaterialSwapRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("MSWP", Signature::from_string("MSWP"));
}

TEST(RowRoundtripReal, ArmorRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("ARMO", Signature::from_string("ARMO"));
}

TEST(RowRoundtripReal, ArmorAddonRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("ARMA", Signature::from_string("ARMA"));
}

TEST(RowRoundtripReal, NpcRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("NPC_", Signature::from_string("NPC_"));
}

TEST(RowRoundtripReal, RaceRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("RACE", Signature::from_string("RACE"));
}

TEST(RowRoundtripReal, RaceLinkRowsResolveOnEveryRecord) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);
  const auto race = Signature::from_string("RACE");
  const auto rows = fo4::schema::formlink_fields(race);
  ASSERT_FALSE(rows.empty()) << "RACE has no FormLink rows";
  const auto severable = fo4::schema::formlink_field_descriptor("RACE\\SeverableExplosion");
  ASSERT_TRUE(severable.has_value()) << "RACE has no severable explosion row";

  std::size_t records = 0;
  std::size_t resolved = 0;
  std::size_t present = 0;
  std::size_t absent = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != race) return;
    ++records;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      ASSERT_TRUE(count.has_value()) << field.path << " on " << record.form_key.to_string();
      if (*count > 0) ++resolved;
    }
    const auto count = fo4::record_field_count(plugin, record.form_key, *severable);
    ASSERT_TRUE(count.has_value()) << record.form_key.to_string();
    if (*count == 0) {
      ++absent;
    } else {
      ++present;
    }
  });
  EXPECT_EQ(records, 45u);
  EXPECT_EQ(present + absent, records);
  EXPECT_GT(present, 0u) << "no RACE carries the severable explosion link";
  EXPECT_GT(absent, 0u) << "every RACE carrying the link would leave the absence path unproven";
  EXPECT_GT(resolved, 0u) << "no RACE FormLink row resolved a value";
}

TEST(RowRoundtripReal, ArmorLinkRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_links("ARMO", Signature::from_string("ARMO"));
}

TEST(RowRoundtripReal, ArmorAddonLinkRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_links("ARMA", Signature::from_string("ARMA"));
}

TEST(RowRoundtripReal, RaceLinkRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_links("RACE", Signature::from_string("RACE"));
}

TEST(RowRoundtripReal, WeaponRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("WEAP", Signature::from_string("WEAP"));
}

TEST(RowRoundtripReal, SpellRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("SPEL", Signature::from_string("SPEL"));
}

TEST(RowRoundtripReal, PotionRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("ALCH", Signature::from_string("ALCH"));
}

TEST(RowRoundtripReal, IngredientRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("INGR", Signature::from_string("INGR"));
}

TEST(RowRoundtripReal, KeyRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("KEYM", Signature::from_string("KEYM"));
}

TEST(RowRoundtripReal, LightRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("LIGH", Signature::from_string("LIGH"));
}

TEST(RowRoundtripReal, MiscItemRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("MISC", Signature::from_string("MISC"));
}

TEST(RowRoundtripReal, ContainerRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("CONT", Signature::from_string("CONT"));
}

TEST(RowRoundtripReal, BookRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("BOOK", Signature::from_string("BOOK"));
}

TEST(RowRoundtripReal, AmmoRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("AMMO", Signature::from_string("AMMO"));
}

TEST(RowRoundtripReal, ConstructibleObjectRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("COBJ", Signature::from_string("COBJ"));
}

TEST(RowRoundtripReal, FloraRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("FLOR", Signature::from_string("FLOR"));
}

TEST(RowRoundtripReal, FurnitureRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("FURN", Signature::from_string("FURN"));
}

TEST(RowRoundtripReal, ActivatorRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("ACTI", Signature::from_string("ACTI"));
}

TEST(RowRoundtripReal, StaticRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("STAT", Signature::from_string("STAT"));
}
