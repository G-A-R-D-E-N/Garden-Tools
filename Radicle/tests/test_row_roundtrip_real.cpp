#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

using fo4::Signature;
using fo4::schema::ScalarFieldDescriptor;
using fo4::schema::ScalarStorage;

struct SubrecordBytes {
  Signature signature{};
  std::vector<std::byte> data;
};

std::vector<SubrecordBytes> snapshot(const fo4::Record& record) {
  std::vector<SubrecordBytes> out;
  out.reserve(record.subrecords.size());
  for (const auto& subrecord : record.subrecords) {
    out.push_back({subrecord.signature, subrecord.data});
  }
  return out;
}

bool same_record(const std::vector<SubrecordBytes>& before, const fo4::Record& after) {
  if (before.size() != after.subrecords.size()) return false;
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].signature != after.subrecords[index].signature) return false;
    const auto& data = after.subrecords[index].data;
    if (before[index].data.size() != data.size()) return false;
    for (std::size_t byte = 0; byte < data.size(); ++byte) {
      if (before[index].data[byte] != data[byte]) return false;
    }
  }
  return true;
}

std::string decode_row(const fo4::Record& record, const ScalarFieldDescriptor& field) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != field.scalar.subrecord) continue;
    const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
    if (bytes.empty()) return {};
    return fo4::schema::decode_scalar(bytes, field.scalar);
  }
  return {};
}

std::vector<std::string> candidates(const ScalarFieldDescriptor& field) {
  std::vector<std::string> out;
  if (!field.scalar.names.empty()) {
    for (const auto& name : field.scalar.names) out.push_back(std::to_string(name.value));
    return out;
  }
  if (field.scalar.storage == ScalarStorage::f32) {
    out = {"0.5", "-0.5", "2.5", "1.5"};
    return out;
  }
  out = {"1", "2", "3", "0", "7", "42"};
  return out;
}

bool inside_bounds(const ScalarFieldDescriptor& field, const std::string& value) {
  try {
    const auto number = std::stod(value);
    return number >= field.scalar.minimum && number <= field.scalar.maximum;
  } catch (const std::exception&) {
    return false;
  }
}

struct Tally {
  std::size_t rows = 0;
  std::size_t exercised = 0;
  std::size_t skipped = 0;
};

void roundtrip_family(const char* family, Signature signature) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);
  const auto rows = fo4::schema::scalar_fields(signature);
  ASSERT_FALSE(rows.empty()) << family;

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      if (count && *count > 0) {
        chosen = record.form_key;
        return;
      }
    }
  });
  ASSERT_TRUE(chosen.has_value()) << family << " has no record carrying one of its rows";
  const auto key = *chosen;
  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);

  Tally tally;
  struct EditedRow {
    ScalarFieldDescriptor field;
    std::string expected;
  };
  std::vector<EditedRow> edited_rows;
  for (const auto& field : rows) {
    ++tally.rows;
    if (field.scalar.storage == ScalarStorage::zstring ||
        field.scalar.storage == ScalarStorage::fixed_string) {
      ++tally.skipped;
      continue;
    }
    const auto count = fo4::record_field_count(edited, key, field);
    if (!count || *count == 0) {
      ++tally.skipped;
      continue;
    }
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto original = decode_row(*current, field);
    std::string applied;
    std::string decoded;
    for (const auto& candidate : candidates(field)) {
      if (!inside_bounds(field, candidate) || candidate == original) continue;
      if (fo4::set_record_scalar(edited, key, field, 0, candidate) !=
          fo4::ScalarMutationStatus::updated) {
        continue;
      }
      const auto* probe = fo4::find_record(edited, key);
      ASSERT_NE(probe, nullptr);
      decoded = decode_row(*probe, field);
      if (!decoded.empty() && decoded != original) {
        applied = candidate;
        break;
      }
    }
    if (applied.empty()) {
      ++tally.skipped;
      continue;
    }
    edited_rows.push_back({field, decoded});
    ++tally.exercised;
  }

  ASSERT_GT(tally.exercised, 0u) << family << " exercised no row";
  const auto* written = fo4::find_record(edited, key);
  ASSERT_NE(written, nullptr);
  const auto after_edit = snapshot(*written);
  EXPECT_FALSE(same_record(before, *written))
      << family << " wrote nothing to the record";

  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, "RowRoundtrip.esm");
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(after_edit, *restored))
      << family << " did not re-read the edited record byte for byte";

  for (const auto& row : edited_rows) {
    EXPECT_EQ(decode_row(*restored, row.field), row.expected)
        << family << " lost " << row.field.path << " across the write";
  }
  EXPECT_EQ(edited_rows.size(), tally.exercised) << family;

  std::size_t changed = 0;
  for (std::size_t index = 0; index < before.size(); ++index) {
    bool differs = false;
    if (before[index].data.size() != after_edit[index].data.size()) {
      differs = true;
    } else {
      for (std::size_t byte = 0; byte < before[index].data.size(); ++byte) {
        if (before[index].data[byte] != after_edit[index].data[byte]) differs = true;
      }
    }
    if (!differs) continue;
    ++changed;
    bool named = false;
    for (const auto& row : edited_rows) {
      if (row.field.scalar.subrecord == before[index].signature) named = true;
    }
    EXPECT_TRUE(named) << family << " changed " << before[index].signature.to_string()
                       << " which no edited row names";
  }
  std::vector<Signature> distinct;
  for (const auto& row : edited_rows) {
    if (std::find(distinct.begin(), distinct.end(), row.field.scalar.subrecord) == distinct.end()) {
      distinct.push_back(row.field.scalar.subrecord);
    }
  }
  EXPECT_GE(changed, 1u) << family;
  EXPECT_LE(changed, distinct.size())
      << family << " changed more subrecords than the distinct subrecords its rows name";
}


void roundtrip_links(const char* family, Signature signature) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);
  const auto rows = fo4::schema::formlink_fields(signature);
  ASSERT_FALSE(rows.empty()) << family << " has no FormLink rows";

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      if (count && *count > 0) {
        chosen = record.form_key;
        return;
      }
    }
  });
  ASSERT_TRUE(chosen.has_value()) << family << " has no record carrying one of its links";
  const auto key = *chosen;

  std::optional<fo4::FormKey> target;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (target.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto allowed = fo4::schema::formlink_field_targets(field);
      if (allowed.empty() || allowed.front() != record.header.signature) continue;
      if (record.form_key == key) continue;
      target = record.form_key;
      return;
    }
  });
  ASSERT_TRUE(target.has_value()) << family << " has no second record to link to";

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);

  std::size_t exercised = 0;
  std::vector<Signature> distinct;
  for (const auto& field : rows) {
    if (!fo4::schema::formlink_field_allowed(field, target->local())) continue;
    const auto count = fo4::record_field_count(edited, key, field);
    if (!count || *count == 0) continue;
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto prior = snapshot(*current);
    if (fo4::set_record_formlink_field(edited, key, field, 0, *target) !=
        fo4::FormLinkFieldMutationStatus::updated) {
      continue;
    }
    const auto* probe = fo4::find_record(edited, key);
    ASSERT_NE(probe, nullptr);
    if (same_record(prior, *probe)) continue;
    if (std::find(distinct.begin(), distinct.end(), field.subrecord) == distinct.end()) {
      distinct.push_back(field.subrecord);
    }
    ++exercised;
  }
  ASSERT_GT(exercised, 0u) << family << " wrote no link";
  const auto* written = fo4::find_record(edited, key);
  ASSERT_NE(written, nullptr);
  const auto after_edit = snapshot(*written);
  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, "RowRoundtrip.esm");
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(after_edit, *restored))
      << family << " did not re-read the edited link byte for byte";
  std::size_t changed = 0;
  for (std::size_t index = 0; index < before.size(); ++index) {
    const bool differs = before[index].data.size() != after_edit[index].data.size() ||
        !std::equal(before[index].data.begin(), before[index].data.end(),
                    after_edit[index].data.begin());
    if (!differs) continue;
    ++changed;
    EXPECT_TRUE(std::find(distinct.begin(), distinct.end(), before[index].signature) != distinct.end())
        << family << " changed " << before[index].signature.to_string()
        << " which no edited link names";
  }
  EXPECT_GE(changed, 1u) << family;
  EXPECT_LE(changed, distinct.size()) << family;
}

}  // namespace

TEST(RowRoundtripReal, CellRowsSurviveWriteAndReread) {
  roundtrip_family("CELL", fo4::Signature::from_string("CELL"));
}

TEST(RowRoundtripReal, RegionRowsSurviveWriteAndReread) {
  roundtrip_family("REGN", fo4::Signature::from_string("REGN"));
}

TEST(RowRoundtripReal, WorldRowsSurviveWriteAndReread) {
  roundtrip_family("WRLD", fo4::Signature::from_string("WRLD"));
}

TEST(RowRoundtripReal, TerminalRowsSurviveWriteAndReread) {
  roundtrip_family("TERM", fo4::Signature::from_string("TERM"));
}

TEST(RowRoundtripReal, QuestRowsSurviveWriteAndReread) {
  roundtrip_family("QUST", fo4::Signature::from_string("QUST"));
}

TEST(RowRoundtripReal, ObjectModRowsSurviveWriteAndReread) {
  roundtrip_family("OMOD", fo4::Signature::from_string("OMOD"));
}

TEST(RowRoundtripReal, PerkRowsSurviveWriteAndReread) {
  roundtrip_family("PERK", fo4::Signature::from_string("PERK"));
}

TEST(RowRoundtripReal, InfoRowsSurviveWriteAndReread) {
  roundtrip_family("INFO", fo4::Signature::from_string("INFO"));
}

TEST(RowRoundtripReal, FootstepSetRowsSurviveWriteAndReread) {
  roundtrip_family("FSTS", fo4::Signature::from_string("FSTS"));
}

TEST(RowRoundtripReal, LensRowsSurviveWriteAndReread) {
  roundtrip_family("LENS", fo4::Signature::from_string("LENS"));
}

TEST(RowRoundtripReal, MaterialSwapRowsSurviveWriteAndReread) {
  roundtrip_family("MSWP", fo4::Signature::from_string("MSWP"));
}

TEST(RowRoundtripReal, ArmorRowsSurviveWriteAndReread) {
  roundtrip_family("ARMO", fo4::Signature::from_string("ARMO"));
}

TEST(RowRoundtripReal, ArmorAddonRowsSurviveWriteAndReread) {
  roundtrip_family("ARMA", fo4::Signature::from_string("ARMA"));
}

TEST(RowRoundtripReal, NpcRowsSurviveWriteAndReread) {
  roundtrip_family("NPC_", fo4::Signature::from_string("NPC_"));
}

TEST(RowRoundtripReal, RaceRowsSurviveWriteAndReread) {
  roundtrip_family("RACE", fo4::Signature::from_string("RACE"));
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
  const auto race = fo4::Signature::from_string("RACE");
  const auto rows = fo4::schema::formlink_fields(race);
  ASSERT_FALSE(rows.empty()) << "RACE has no FormLink rows";
  std::size_t records = 0;
  std::size_t resolved = 0;
  std::size_t absent_links = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != race) return;
    ++records;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      ASSERT_TRUE(count.has_value()) << field.path << " on " << record.form_key.to_string();
      if (*count > 0) ++resolved;
    }
    const auto severable = fo4::schema::scalar_field_descriptor("RACE\SeverableExplosion");
    if (severable.has_value()) {
      const auto count = fo4::record_field_count(plugin, record.form_key, *severable);
      ASSERT_TRUE(count.has_value());
      if (*count == 0) ++absent_links;
    }
  });
  EXPECT_EQ(records, 45u);
  EXPECT_EQ(absent_links, 39u) << "the severable explosion link is present on 6 of 45 RACE records";
  EXPECT_GT(resolved, 0u) << "no RACE FormLink row resolved a value";
}

TEST(RowRoundtripReal, ArmorLinkRowsSurviveWriteAndReread) {
  roundtrip_links("ARMO", fo4::Signature::from_string("ARMO"));
}

TEST(RowRoundtripReal, ArmorAddonLinkRowsSurviveWriteAndReread) {
  roundtrip_links("ARMA", fo4::Signature::from_string("ARMA"));
}

TEST(RowRoundtripReal, RaceLinkRowsSurviveWriteAndReread) {
  roundtrip_links("RACE", fo4::Signature::from_string("RACE"));
}
