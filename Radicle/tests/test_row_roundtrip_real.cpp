#include "row_roundtrip_harness.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

TEST(RowRoundtripReal, PackageRowsSurviveWriteAndReread) {
  row_roundtrip::roundtrip_family("PACK", Signature::from_string("PACK"));
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
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
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
  std::size_t absent_rows = 0;
  std::size_t present = 0;
  std::size_t absent = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != race) return;
    ++records;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      ASSERT_TRUE(count.has_value()) << field.path << " on " << record.form_key.to_string();
      if (*count > 0) ++resolved;
      else ++absent_rows;
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
  EXPECT_GT(absent_rows, 0u) << "no RACE FormLink row is absent on any record";
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
  const auto* directory = ::fo4::test::fo4_data_dir_cstr();
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

// Detects: a STAT MNAM row whose offset or minimum_payload_size drifted, so shipped statics
// would decode shifted, truncated or missing LOD mesh paths instead of matching the four
// 260-byte slots xEdit declares. The unit suite proves the shape on authored bytes; this one
// proves the loader reads every shipped STAT the same way.
TEST(RowRoundtripReal, StatDistantLodLevelsDecodeFromShippedRecords) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);

  constexpr std::size_t k_slot = 260;
  const std::array<std::string_view, 4> row_paths{
      "STAT\\DistantLodLevel0", "STAT\\DistantLodLevel1",
      "STAT\\DistantLodLevel2", "STAT\\DistantLodLevel3"};
  std::array<fo4::schema::ScalarFieldDescriptor, 4> rows;
  for (std::size_t level = 0; level < rows.size(); ++level) {
    const auto field = fo4::schema::scalar_field_descriptor(row_paths[level]);
    ASSERT_TRUE(field.has_value()) << row_paths[level];
    EXPECT_EQ(field->scalar.subrecord, Signature::from_string("MNAM")) << row_paths[level];
    EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::zstring) << row_paths[level];
    EXPECT_EQ(field->scalar.offset, level * k_slot) << row_paths[level];
    EXPECT_EQ(field->scalar.minimum_payload_size, (level + 1) * k_slot) << row_paths[level];
    rows[level] = *field;
  }

  const auto stat = Signature::from_string("STAT");
  const auto mnam = Signature::from_string("MNAM");
  std::size_t records = 0;
  std::size_t four_level = 0;
  std::size_t shorter = 0;
  std::size_t decoded_levels = 0;
  std::size_t expected_levels = 0;
  std::size_t non_empty = 0;
  const auto count = [&](const fo4::Record& record) {
    if (record.header.signature != stat) return;
    const fo4::Subrecord* payload = nullptr;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == mnam) {
        payload = &subrecord;
        break;
      }
    }
    if (payload == nullptr) return;
    ++records;
    const auto size = payload->data.size();
    EXPECT_EQ(size % k_slot, std::size_t{0})
        << record.form_key.to_string() << " MNAM is " << size << " bytes";
    EXPECT_LE(size, k_slot * rows.size())
        << record.form_key.to_string() << " MNAM is " << size << " bytes";
    if (size == k_slot * rows.size()) {
      ++four_level;
    } else {
      ++shorter;
    }
    expected_levels += size / k_slot;
    const auto* slot = reinterpret_cast<const char*>(payload->data.data());
    for (std::size_t level = 0; level < rows.size(); ++level) {
      const auto bytes = fo4::schema::scalar_field_bytes(payload->data, rows[level].scalar);
      const bool present = size >= (level + 1) * k_slot;
      EXPECT_EQ(!bytes.empty(), present)
          << record.form_key.to_string() << ' ' << row_paths[level] << " MNAM is " << size
          << " bytes";
      if (!present) continue;
      const auto decoded = fo4::schema::decode_scalar(bytes, rows[level].scalar);
      ++decoded_levels;
      // The slot's own bytes up to the terminator are the independent answer the row must give.
      const std::string raw{slot + level * k_slot};
      EXPECT_EQ(decoded, raw)
          << record.form_key.to_string() << ' ' << row_paths[level];
      EXPECT_EQ(decoded.find('\0'), std::string::npos)
          << record.form_key.to_string() << ' ' << row_paths[level] << " kept its terminator";
      if (!decoded.empty()) ++non_empty;
    }
  };
  fo4::visit_records(plugin, count);

  EXPECT_GT(records, std::size_t{100}) << "no shipped STAT carries MNAM";
  EXPECT_GT(four_level, std::size_t{0}) << "no shipped STAT carries four MNAM levels";
  // Every shipped STAT uses all four slots; shorter payloads are authored and covered by the
  // synthetic suite, so the real-data check proves the four-slot shape rather than inventing one.
  EXPECT_EQ(records, four_level) << "a shipped STAT carried a MNAM payload that is not four slots";
  EXPECT_GT(non_empty, std::size_t{0}) << "every shipped MNAM slot decoded empty";
  EXPECT_EQ(decoded_levels, expected_levels)
      << "a payload decoded a different number of levels than its slot count";
  std::clog << "STAT MNAM: records=" << records << " four_level=" << four_level
            << " shorter=" << shorter << " levels=" << decoded_levels
            << " non_empty=" << non_empty << "\n";
}

// Constraint: detects a property-row writer that zeroes the Unused bytes (1-3, 5-7, 10-11); every official OMOD DATA and OBTS must re-set byte for byte.
TEST(RowRoundtripReal, ObjectModDataAndObjectTemplatesRewriteByteIdentically) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  auto plugin = fo4::read_plugin_file(path);
  const auto omod = Signature::from_string("OMOD");
  const auto data = Signature::from_string("DATA");
  const auto obts = Signature::from_string("OBTS");

  struct Target {
    fo4::FormKey key;
    bool is_omod{false};
    std::size_t templates{0};
  };
  std::vector<Target> targets;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    Target target{record.form_key, record.header.signature == omod, 0};
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == obts) ++target.templates;
    }
    if (target.is_omod || target.templates != 0) targets.push_back(target);
  });

  const auto payload = [&](const fo4::FormKey& key, Signature signature, std::size_t index) {
    const auto* record = fo4::find_record(plugin, key);
    std::size_t seen = 0;
    if (record != nullptr) {
      for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != signature) continue;
        if (seen++ == index) return subrecord.data;
      }
    }
    ADD_FAILURE() << key.to_string() << " lost its " << signature.to_string() << " subrecord";
    return std::vector<std::byte>{};
  };

  const auto rewrite_rows = [&](const fo4::FormKey& key, const std::string& prefix,
                                const std::vector<const char*>& leaves) {
    for (const auto* leaf : leaves) {
      const auto field = fo4::schema::scalar_field_descriptor(prefix + leaf);
      ASSERT_TRUE(field.has_value()) << prefix << leaf;
      const auto count = fo4::record_field_count(plugin, key, *field).value_or(0);
      for (std::size_t row = 0; row < count; ++row) {
        const auto text = fo4::record_scalar_text(plugin, *fo4::find_record(plugin, key), *field, row);
        ASSERT_TRUE(text.has_value()) << key.to_string() << " " << prefix << leaf << " " << row;
        const auto status = fo4::set_record_scalar(plugin, key, *field, row, *text);
        ASSERT_TRUE(status == fo4::ScalarMutationStatus::updated || status == fo4::ScalarMutationStatus::unchanged)
            << key.to_string() << " " << prefix << leaf << " " << row;
      }
    }
  };

  std::size_t mods = 0;
  std::size_t templates = 0;
  std::size_t mismatched = 0;
  for (const auto& target : targets) {
    if (target.is_omod) {
      const auto before = payload(target.key, data, 0);
      const auto* record = fo4::find_record(plugin, target.key);
      ASSERT_NE(record, nullptr) << target.key.to_string();
      rewrite_rows(target.key, std::string("OMOD\\"),
                   {"IncludeMod", "IncludeMinimumLevel", "IncludeOptional", "IncludeDontUseAll",
                    "PropertyValueType", "PropertyFunctionType", "PropertyProperty",
                    "PropertyValue1", "PropertyValue2", "PropertyStep", "AttachParentSlot"});
      ++mods;
      if (payload(target.key, data, 0) != before) {
        ++mismatched;
        ADD_FAILURE() << "OMOD DATA changed on " << target.key.to_string();
      }
    }
    if (target.templates != 0) {
      std::vector<std::vector<std::byte>> before;
      for (std::size_t index = 0; index < target.templates; ++index) {
        before.push_back(payload(target.key, obts, index));
      }
      const auto* host = fo4::find_record(plugin, target.key);
      ASSERT_NE(host, nullptr) << target.key.to_string();
      rewrite_rows(target.key, host->header.signature.to_string() + "\\ObjectTemplate",
                   {"LevelMin", "LevelMax", "ParentCombinationIndex", "Keyword", "MinLevelForRanks",
                    "AltLevelsPerTier", "IncludeMod", "IncludeAttachPointIndex", "IncludeOptional",
                    "IncludeDontUseAll", "PropertyValueType", "PropertyFunctionType",
                    "PropertyProperty", "PropertyValue1", "PropertyValue2", "PropertyStep"});
      for (std::size_t index = 0; index < target.templates; ++index) {
        ++templates;
        if (payload(target.key, obts, index) != before[index]) {
          ++mismatched;
          ADD_FAILURE() << "OBTS " << index << " changed on " << target.key.to_string();
        }
      }
      ASSERT_LT(mismatched, 20u) << "stopping after 20 changed payloads";
    }
    ASSERT_LT(mismatched, 20u) << "stopping after 20 changed payloads";
  }
  EXPECT_GT(mods, 0u) << "Fallout4.esm yielded no OMOD record";
  EXPECT_GT(templates, 0u) << "Fallout4.esm yielded no object template";
}

// Regression: the bounds setter once counted the OFST and CLSZ grids from the stored NAM0/NAM9 floats, which are
// world units, so it refused every shipped worldspace that carries a grid. xEdit counts cells, 4096 units each.
TEST(RowRoundtripReal, WorldBoundsEditResizesEveryShippedCellGrid) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  auto plugin = fo4::read_plugin_file(path);
  const auto wrld = Signature::from_string("WRLD");
  const auto max_x = fo4::schema::scalar_field_descriptor(wrld, "WRLD\\BoundsMaxX");
  ASSERT_TRUE(max_x.has_value());

  struct World {
    fo4::FormKey key;
    float min_x{0.0f};
    float max_x{0.0f};
    std::size_t offsets{0};
    std::size_t cell_sizes{0};
  };
  const auto payload_size = [](const fo4::Record& record, const char* signature) {
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == Signature::from_string(signature)) return subrecord.data.size();
    }
    return std::size_t{0};
  };
  const auto stored_float = [](const fo4::Record& record, const char* signature) {
    float value = 0.0f;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature != Signature::from_string(signature)) continue;
      if (subrecord.data.size() >= sizeof(value)) {
        std::memcpy(&value, subrecord.data.data(), sizeof(value));
      }
      break;
    }
    return value;
  };
  const auto read_world = [&](const fo4::Record& record) {
    return World{record.form_key, stored_float(record, "NAM0"), stored_float(record, "NAM9"),
                 payload_size(record, "OFST"), payload_size(record, "CLSZ")};
  };
  std::vector<World> worlds;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != wrld) return;
    const auto world = read_world(record);
    if (world.offsets != 0 || world.cell_sizes != 0) worlds.push_back(world);
  });
  ASSERT_FALSE(worlds.empty()) << "Fallout4.esm yielded no worldspace with an OFST or CLSZ grid";

  constexpr float k_cell = 4096.0f;
  constexpr float k_sentinel = std::numeric_limits<float>::max();
  const auto cells = [&](float stored) {
    if (stored == k_sentinel || stored == -k_sentinel) return 0.0f;
    return std::nearbyint(stored / k_cell);
  };
  std::size_t resized = 0;
  for (const auto& world : worlds) {
    SCOPED_TRACE(world.key.to_string());
    const auto columns = cells(world.max_x) - cells(world.min_x) + 1.0f;
    std::clog << "WRLD grid: " << world.key.to_string() << " min_x=" << world.min_x
              << " max_x=" << world.max_x << " columns=" << columns << " OFST=" << world.offsets
              << " CLSZ=" << world.cell_sizes << "\n";
    ASSERT_GE(columns, 1.0f);
    const auto wide = static_cast<std::size_t>(columns);
    const auto wider = std::to_string((cells(world.max_x) + 1.0f) * k_cell);
    try {
      ASSERT_EQ(fo4::set_record_scalar(plugin, world.key, *max_x, 0, wider),
                fo4::ScalarMutationStatus::updated);
    } catch (const fo4::schema::ScalarError& error) {
      ADD_FAILURE() << "the bounds setter refused a shipped worldspace: " << error.what();
      continue;
    }
    const auto* record = fo4::find_record(plugin, world.key);
    ASSERT_NE(record, nullptr);
    const auto after = read_world(*record);
    EXPECT_EQ(after.offsets, world.offsets / wide * (wide + 1)) << "OFST did not gain one column";
    EXPECT_EQ(after.cell_sizes, world.cell_sizes / wide * (wide + 1)) << "CLSZ did not gain one column";
    ++resized;
  }
  EXPECT_EQ(resized, worlds.size());
  std::clog << "WRLD grid: worlds=" << worlds.size() << " resized=" << resized << "\n";
}
