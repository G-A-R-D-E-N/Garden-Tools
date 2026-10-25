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
#include <string>

namespace {

using fo4::Signature;

std::optional<std::string> response_bytes(const fo4::Plugin& plugin,
                                          const fo4::FormKey& key,
                                          const fo4::schema::ScalarDescriptor& scalar) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return std::nullopt;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != Signature::from_string("NAM9")) continue;
    return fo4::schema::decode_scalar(subrecord.data, scalar);
  }
  return std::nullopt;
}

std::size_t response_byte_payload(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == Signature::from_string("NAM9")) return subrecord.data.size();
  }
  return 0;
}

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

TEST(RowRoundtripReal, GrasRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("GRAS", Signature::from_string("GRAS"));
}

TEST(RowRoundtripReal, TreeRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("TREE", Signature::from_string("TREE"));
}

TEST(RowRoundtripReal, FlorRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("FLOR", Signature::from_string("FLOR"));
}

TEST(RowRoundtripReal, LtexRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("LTEX", Signature::from_string("LTEX"));
}

TEST(RowRoundtripReal, WaterRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("WATR", Signature::from_string("WATR"));
}

TEST(RowRoundtripReal, ClimateRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("CLMT", Signature::from_string("CLMT"));
}

TEST(RowRoundtripReal, SoundDescriptorRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("SNDR", Signature::from_string("SNDR"));
}

TEST(RowRoundtripReal, SoundMarkerRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("SOUN", Signature::from_string("SOUN"));
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
  auto plugin = fo4::read_plugin_file(path);
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

TEST(RowRoundtripReal, InfoResponseBytesSurviveALengthChange) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  auto plugin = fo4::read_plugin_file(path);

  const auto field = fo4::schema::scalar_field_descriptor(
      Signature::from_string("INFO"), "INFO\\ResponseBytes");
  ASSERT_TRUE(field.has_value()) << "INFO has no NAM9 bytes row";
  ASSERT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::bytes);

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != Signature::from_string("INFO")) return;
    if (fo4::record_field_count(plugin, record.form_key, *field).value_or(0) == std::size_t{0}) {
      return;
    }
    if (response_byte_payload(plugin, record.form_key) < 2) return;
    chosen = record.form_key;
  });
  // Constraint: an official master must carry an INFO response of at least two bytes, so a missing one fails instead of skipping.
  ASSERT_TRUE(chosen.has_value()) << "no INFO record carries a NAM9 of at least two bytes";
  const auto key = *chosen;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = row_roundtrip::snapshot(*live);
  const auto original = response_bytes(edited, key, field->scalar);
  ASSERT_TRUE(original.has_value()) << key.to_string();
  ASSERT_GE(original->size(), std::size_t{4})
      << "a two-byte payload encodes to four hex characters";
  const auto original_size = response_byte_payload(edited, key);
  const auto longer = *original + "deadbeef";
  const auto shorter = original->substr(0, 2);
  ASSERT_LT(shorter.size(), original->size());
  ASSERT_GT(longer.size(), original->size());

  ASSERT_EQ(fo4::set_record_scalar(edited, key, *field, 0, longer),
            fo4::ScalarMutationStatus::updated);
  const auto* grown = fo4::find_record(edited, key);
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(response_bytes(edited, key, field->scalar), std::optional<std::string>{longer});
  EXPECT_EQ(response_byte_payload(edited, key), original_size + 4)
      << "the longer value did not add exactly four bytes";
  const auto grown_snapshot = row_roundtrip::snapshot(*grown);

  auto bytes = fo4::write_plugin(edited);
  auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(row_roundtrip::same_record(grown_snapshot, *restored))
      << "the longer NAM9 payload did not re-read byte for byte";
  EXPECT_EQ(response_bytes(reread, key, field->scalar), std::optional<std::string>{longer});

  ASSERT_EQ(fo4::set_record_scalar(reread, key, *field, 0, shorter),
            fo4::ScalarMutationStatus::updated);
  const auto* shrunk = fo4::find_record(reread, key);
  ASSERT_NE(shrunk, nullptr);
  EXPECT_EQ(response_bytes(reread, key, field->scalar), std::optional<std::string>{shorter});
  EXPECT_EQ(response_byte_payload(reread, key), shorter.size() / 2)
      << "the shorter value did not replace the payload at its own length";
  const auto shrunk_snapshot = row_roundtrip::snapshot(*shrunk);

  bytes = fo4::write_plugin(reread);
  const auto second = fo4::read_plugin(bytes, path);
  const auto* shortened = fo4::find_record(second, key);
  ASSERT_NE(shortened, nullptr) << key.to_string();
  EXPECT_TRUE(row_roundtrip::same_record(shrunk_snapshot, *shortened))
      << "the shorter NAM9 payload did not re-read byte for byte";

  ASSERT_EQ(fo4::set_record_scalar(edited, key, *field, 0, *original),
            fo4::ScalarMutationStatus::updated);
  const auto* reverted = fo4::find_record(edited, key);
  ASSERT_NE(reverted, nullptr);
  EXPECT_TRUE(row_roundtrip::same_snapshot(before, row_roundtrip::snapshot(*reverted)))
      << "the record was not restored after a longer and a shorter NAM9 write";
}
