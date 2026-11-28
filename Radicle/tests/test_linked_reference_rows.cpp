#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/semantic_fields.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::uint32_t k_achr_id = 0x810u;
constexpr std::uint32_t k_refr_id = 0x811u;
constexpr std::uint32_t k_quest_id = 0x820u;
constexpr std::uint32_t k_pairs_id = 0x821u;

std::string label_of(std::uint32_t value) {
  std::string out;
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
  return out;
}

Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

void write_placed(ByteWriter& out, std::string_view signature, std::uint32_t local, const Bytes& odd_link) {
  const auto at = fo4::test::begin_record(out, signature, 0, 0x01000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Placed\0", 7});
  fo4::test::write_subrecord(out, "XLKR", words({0x00000A50u, 0x00000A51u}));
  fo4::test::write_subrecord(out, "XLKR", odd_link);
  fo4::test::write_subrecord(out, "XLKR", words({0x00000A53u, 0x00000A54u}));
  fo4::test::end_record(out, at);
}

void write_alias(ByteWriter& out, std::string_view start, std::uint32_t id, std::string_view name,
                 std::initializer_list<std::pair<std::string_view, std::uint32_t>> extra) {
  fo4::test::write_subrecord(out, start, words({id}));
  fo4::test::write_subrecord(out, "ALID", name);
  fo4::test::write_subrecord(out, "FNAM", words({0}));
  for (const auto& [signature, value] : extra) fo4::test::write_subrecord(out, signature, words({value}));
  fo4::test::write_subrecord(out, "ALED", Bytes{});
}

Bytes make_plugin(const Bytes& odd_link) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000000u | 0x800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::end_record(out, cell);
  const auto label = label_of(0x01000000u | 0x800u);
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto persistent = fo4::test::begin_group(out, label, 8);
  write_placed(out, "ACHR", k_achr_id, odd_link);
  write_placed(out, "REFR", k_refr_id, odd_link);
  fo4::test::end_group(out, persistent);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cell_group);

  const auto quest_group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000000u | k_quest_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
  fo4::test::write_subrecord(out, "ANAM", words({3}));
  write_alias(out, "ALST", 0, std::string_view{"One\0", 4},
              {{"ALFR", 0x00000A70u}, {"ALDN", 0x00000A71u}});
  write_alias(out, "ALLS", 1, std::string_view{"Two\0", 4}, {});
  write_alias(out, "ALST", 2, std::string_view{"Three\0", 6}, {});
  fo4::test::end_record(out, quest);

  const auto pairs = fo4::test::begin_record(out, "QUST", 0, 0x01000000u | k_pairs_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"PairsQuest\0", 11});
  fo4::test::write_subrecord(out, "ANAM", words({5}));
  write_alias(out, "ALST", 0, std::string_view{"DisplayOnly\0", 12}, {{"ALFI", 0u}, {"ALDN", 0x00000B01u}});
  write_alias(out, "ALST", 1, std::string_view{"ForcedOnly\0", 11}, {{"ALFI", 0u}, {"ALFR", 0x00000B10u}});
  write_alias(out, "ALST", 2, std::string_view{"Both\0", 5},
              {{"ALFI", 0u}, {"ALFR", 0x00000B20u}, {"ALFA", 0u}, {"ALDN", 0x00000B21u}});
  write_alias(out, "ALLS", 3, std::string_view{"Neither\0", 8}, {});
  fo4::test::write_subrecord(out, "ALCS", words({4}));
  fo4::test::write_subrecord(out, "ALMI", Bytes{std::byte{1}});
  fo4::test::end_record(out, pairs);
  fo4::test::end_group(out, quest_group);
  return out.bytes;
}

fo4::FormKey patch_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

std::string master_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{local}}.to_string();
}

struct Read {
  fo4::FieldStatus status;
  std::string text;
};

Read read_row(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path, std::size_t index) {
  const auto* record = fo4::find_record(plugin, patch_key(id));
  if (record == nullptr) throw std::runtime_error("missing record");
  Read out{fo4::FieldStatus::unknown_path, {}};
  out.status = fo4::read_record_field(plugin, *record, path, index, out.text);
  return out;
}

std::vector<std::size_t> xlkr_sizes(const fo4::Plugin& plugin, std::uint32_t id) {
  std::vector<std::size_t> out;
  for (const auto& sub : fo4::find_record(plugin, patch_key(id))->subrecords) {
    if (sub.signature == fo4::Signature::from_string("XLKR")) out.push_back(sub.data.size());
  }
  return out;
}

struct PlacedRows {
  const char* signature;
  std::uint32_t id;
  const char* keyword;
  const char* reference;
};

constexpr PlacedRows k_placed[] = {
    {"ACHR", k_achr_id, "ACHR\\LinkedReferenceKeyword", "ACHR\\LinkedReference"},
    {"REFR", k_refr_id, "REFR\\LinkedKeyword", "REFR\\LinkedReference"},
};

TEST(LinkedReferenceRows, ReferenceRowAdmitsTheFourByteEntryAndKeepsTheSizeOrder) {
  for (const auto& rows : k_placed) {
    SCOPED_TRACE(rows.reference);
    const auto signature = fo4::Signature::from_string(rows.signature);
    const auto reference = fo4::schema::formlink_field_descriptor(signature, rows.reference);
    ASSERT_TRUE(reference.has_value());
    EXPECT_EQ(reference->offset, 4u);
    EXPECT_EQ(reference->payload_sizes[0], 8u);
    EXPECT_EQ(reference->payload_sizes[1], 4u);
  }
}

TEST(LinkedReferenceRows, MixedEntrySizesKeepKeywordAndReferenceIndexesPaired) {
  const auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  for (const auto& rows : k_placed) {
    SCOPED_TRACE(rows.signature);
    ASSERT_EQ(xlkr_sizes(plugin, rows.id), (std::vector<std::size_t>{8, 4, 8}));
    EXPECT_EQ(fo4::record_field_count(plugin, patch_key(rows.id), rows.keyword), 3u);
    EXPECT_EQ(fo4::record_field_count(plugin, patch_key(rows.id), rows.reference), 3u);

    const auto keyword = std::vector<std::string>{master_key(0xA50), master_key(0xA52), master_key(0xA53)};
    const auto reference = std::vector<std::string>{master_key(0xA51), "", master_key(0xA54)};
    for (std::size_t index = 0; index < 3; ++index) {
      SCOPED_TRACE(index);
      const auto keyword_read = read_row(plugin, rows.id, rows.keyword, index);
      EXPECT_EQ(keyword_read.status, fo4::FieldStatus::ok);
      EXPECT_EQ(keyword_read.text, keyword[index]);
      const auto reference_read = read_row(plugin, rows.id, rows.reference, index);
      EXPECT_EQ(reference_read.status, fo4::FieldStatus::ok);
      EXPECT_EQ(reference_read.text, reference[index]);
    }
    EXPECT_EQ(read_row(plugin, rows.id, rows.keyword, 3).status, fo4::FieldStatus::occurrence_missing);
    EXPECT_EQ(read_row(plugin, rows.id, rows.reference, 3).status, fo4::FieldStatus::occurrence_missing);
  }
}

TEST(LinkedReferenceRows, ShortEntryRefusesAReferenceWriteAndTakesAKeywordWriteInPlace) {
  auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  for (const auto& rows : k_placed) {
    SCOPED_TRACE(rows.signature);
    const auto signature = fo4::Signature::from_string(rows.signature);
    const auto keyword = fo4::schema::formlink_field_descriptor(signature, rows.keyword);
    const auto reference = fo4::schema::formlink_field_descriptor(signature, rows.reference);
    ASSERT_TRUE(keyword.has_value());
    ASSERT_TRUE(reference.has_value());
    EXPECT_THROW(fo4::set_record_formlink_field(plugin, patch_key(rows.id), *reference, 1,
                                                fo4::FormKey::parse("00000A99:Fallout4.esm")),
                 fo4::schema::FormLinkFieldError);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(rows.id), *keyword, 1,
                                             fo4::FormKey::parse("00000A99:Fallout4.esm")),
              fo4::FormLinkFieldMutationStatus::updated);
    EXPECT_EQ(xlkr_sizes(plugin, rows.id), (std::vector<std::size_t>{8, 4, 8}));
    EXPECT_EQ(read_row(plugin, rows.id, rows.keyword, 1).text, master_key(0xA99));
    EXPECT_EQ(read_row(plugin, rows.id, rows.reference, 1).text, "");
  }
}

TEST(LinkedReferenceRows, SemanticCellsEmitAnEmptyLinkForAShortXlkrEntry) {
  const auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  for (const auto& rows : k_placed) {
    SCOPED_TRACE(rows.signature);
    const auto* record = fo4::find_record(plugin, patch_key(rows.id));
    ASSERT_NE(record, nullptr);
    const auto cells = fo4::semantic_field_cells(plugin, *record);
    ASSERT_TRUE(cells.has_value());
    const auto cell = [&](const std::string& path) -> const fo4::SemanticFieldCell* {
      const auto found = std::find_if(cells->begin(), cells->end(),
                                      [&](const auto& candidate) { return candidate.path == path; });
      return found == cells->end() ? nullptr : &*found;
    };
    const auto* paired = cell(std::string(rows.reference) + "[0]");
    const auto* short_entry = cell(std::string(rows.reference) + "[1]");
    const auto* keyword = cell(std::string(rows.keyword) + "[1]");
    ASSERT_NE(paired, nullptr);
    ASSERT_NE(short_entry, nullptr);
    ASSERT_NE(keyword, nullptr);
    EXPECT_TRUE(paired->available);
    EXPECT_EQ(paired->value, master_key(0xA51));
    EXPECT_TRUE(short_entry->available);
    EXPECT_EQ(short_entry->value, fo4::FormKey{}.to_string());
    EXPECT_TRUE(keyword->available);
    EXPECT_EQ(keyword->value, master_key(0xA52));
  }
}

TEST(LinkedReferenceRows, AliasEndRefusesANonEmptyWrite) {
  auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  const auto end = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("QUST"), "QUST\\AliasEnd");
  ASSERT_TRUE(end.has_value());
  EXPECT_THROW(fo4::set_record_scalar(plugin, patch_key(k_quest_id), *end, 0, "00"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(k_quest_id), *end, 0, ""),
            fo4::ScalarMutationStatus::unchanged);
}

TEST(LinkedReferenceRows, AnUnsupportedEntrySizeStillRefusesTheReferenceRow) {
  const auto plugin = fo4::read_plugin(make_plugin(Bytes(6, std::byte{0})), "Patch.esp");
  for (const auto& rows : k_placed) {
    SCOPED_TRACE(rows.signature);
    EXPECT_THROW(read_row(plugin, rows.id, rows.reference, 0), fo4::schema::FormLinkFieldError);
  }
}

TEST(LinkedReferenceRows, QuestAliasRowsReadTheForcedReferenceDisplayNameAndEndMarker) {
  const auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  const auto forced = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("QUST"),
                                                             "QUST\\AliasForcedReference");
  const auto display = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("QUST"),
                                                              "QUST\\AliasDisplayName");
  ASSERT_TRUE(forced.has_value());
  ASSERT_TRUE(display.has_value());
  EXPECT_EQ(forced->subrecord, fo4::Signature::from_string("ALFR"));
  EXPECT_TRUE(forced->address.repeated);
  EXPECT_FALSE(forced->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*forced, fo4::Signature::from_string("REFR")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*forced, fo4::Signature::from_string("PLYR")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*forced, fo4::Signature::from_string("PBEA")));
  EXPECT_EQ(display->subrecord, fo4::Signature::from_string("ALDN"));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*display, fo4::Signature::from_string("MESG")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*display, fo4::Signature::from_string("KYWD")));

  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_quest_id), "QUST\\AliasForcedReference"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_quest_id), "QUST\\AliasDisplayName"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_quest_id), "QUST\\AliasEnd"), 3u);

  const auto forced_read = read_row(plugin, k_quest_id, "QUST\\AliasForcedReference", 0);
  EXPECT_EQ(forced_read.status, fo4::FieldStatus::ok);
  EXPECT_EQ(forced_read.text, master_key(0xA70));
  const auto display_read = read_row(plugin, k_quest_id, "QUST\\AliasDisplayName", 0);
  EXPECT_EQ(display_read.status, fo4::FieldStatus::ok);
  EXPECT_EQ(display_read.text, master_key(0xA71));
  EXPECT_EQ(read_row(plugin, k_quest_id, "QUST\\AliasForcedReference", 1).status,
            fo4::FieldStatus::occurrence_missing);
  for (std::size_t index = 0; index < 3; ++index) {
    const auto end = read_row(plugin, k_quest_id, "QUST\\AliasEnd", index);
    EXPECT_EQ(end.status, fo4::FieldStatus::ok);
    EXPECT_EQ(end.text, "");
  }
  EXPECT_EQ(read_row(plugin, k_quest_id, "QUST\\AliasEnd", 3).status, fo4::FieldStatus::occurrence_missing);
}

std::size_t list_size_of(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path) {
  std::size_t size = 0;
  if (fo4::record_list_size(plugin, patch_key(id), path, size) != fo4::FieldStatus::ok) {
    throw std::runtime_error("list size refused");
  }
  return size;
}

fo4::FieldStatus list_size_status(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path) {
  std::size_t size = 0;
  return fo4::record_list_size(plugin, patch_key(id), path, size);
}

struct AliasPair {
  std::string forced;
  std::string display;
};

std::vector<AliasPair> alias_pairs(const fo4::Plugin& plugin, std::uint32_t id) {
  std::vector<AliasPair> out;
  std::size_t forced_at = 0;
  std::size_t display_at = 0;
  const auto aliases = list_size_of(plugin, id, "QUST\\Aliases");
  for (std::size_t alias = 0; alias < aliases; ++alias) {
    const auto scope = "QUST\\Aliases[" + std::to_string(alias) + "]\\";
    AliasPair pair;
    if (list_size_of(plugin, id, scope + "AliasForcedReferences") != 0) {
      pair.forced = read_row(plugin, id, "QUST\\AliasForcedReference", forced_at++).text;
    }
    if (list_size_of(plugin, id, scope + "AliasDisplayNames") != 0) {
      pair.display = read_row(plugin, id, "QUST\\AliasDisplayName", display_at++).text;
    }
    out.push_back(std::move(pair));
  }
  return out;
}

// Constraint: detects an alias reader that pairs the flat ALFR and ALDN rows by index, which gives an alias the forced reference or display name of a neighbouring alias as soon as one alias has only one of them, and scoped per-alias counts that count a collection alias or lose an alias.
TEST(LinkedReferenceRows, PerAliasRowsKeepForcedReferenceAndDisplayNamePairedWhenOneIsMissing) {
  const auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  EXPECT_EQ(list_size_of(plugin, k_pairs_id, "QUST\\Aliases"), 4u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_pairs_id), "QUST\\AliasEnd"), 4u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_pairs_id), "QUST\\AliasForcedReference"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(k_pairs_id), "QUST\\AliasDisplayName"), 2u);
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\AliasForcedReference", 0).text, master_key(0xB10));
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\AliasDisplayName", 0).text, master_key(0xB01));

  const auto pairs = alias_pairs(plugin, k_pairs_id);
  ASSERT_EQ(pairs.size(), 4u);
  EXPECT_EQ(pairs[0].forced, "");
  EXPECT_EQ(pairs[0].display, master_key(0xB01));
  EXPECT_EQ(pairs[1].forced, master_key(0xB10));
  EXPECT_EQ(pairs[1].display, "");
  EXPECT_EQ(pairs[2].forced, master_key(0xB20));
  EXPECT_EQ(pairs[2].display, master_key(0xB21));
  EXPECT_EQ(pairs[3].forced, "");
  EXPECT_EQ(pairs[3].display, "");

  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\Aliases[1]\\AliasForcedReferences", 0).text, "100b0000");
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\Aliases[2]\\AliasForcedReferences", 0).text, "200b0000");
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\Aliases[2]\\AliasDisplayNames", 0).text, "210b0000");
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\Aliases[0]\\AliasForcedReferences", 0).status,
            fo4::FieldStatus::occurrence_missing);
  EXPECT_EQ(read_row(plugin, k_pairs_id, "QUST\\Aliases[2]\\AliasForcedReferences", 1).status,
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(list_size_status(plugin, k_pairs_id, "QUST\\Aliases[4]\\AliasForcedReferences"),
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(list_size_status(plugin, k_pairs_id, "QUST\\Stages[0]\\AliasForcedReferences"),
            fo4::FieldStatus::unknown_path);
}

// Constraint: detects an insert into an alias that has no ALFR or ALDN writing the new subrecord after the alias ALED, outside the alias, or an unscoped append that lands at the record end, which the strict alias decode then refuses.
TEST(LinkedReferenceRows, EmptyPerAliasChildRefusesAnInsertThatWouldLandPastAliasEnd) {
  auto plugin = fo4::read_plugin(make_plugin(words({0x00000A52u})), "Patch.esp");
  const auto signatures = [&] {
    std::vector<std::string> out;
    for (const auto& sub : fo4::find_record(plugin, patch_key(k_pairs_id))->subrecords) {
      out.push_back(sub.signature.to_string());
    }
    return out;
  };
  const auto before = signatures();
  EXPECT_THROW(fo4::insert_record_list_element(plugin, patch_key(k_pairs_id),
                                               "QUST\\Aliases[0]\\AliasForcedReferences", 0, nullptr),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::insert_record_list_element(plugin, patch_key(k_pairs_id),
                                               "QUST\\Aliases[3]\\AliasDisplayNames", 0, nullptr),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::insert_record_list_element(plugin, patch_key(k_pairs_id), "QUST\\AliasForcedReferences", 2, nullptr),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(signatures(), before);
}

}  // namespace
