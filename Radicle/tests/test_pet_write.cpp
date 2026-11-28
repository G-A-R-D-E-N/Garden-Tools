#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Spec {
  std::uint32_t form_id;
  std::uint32_t flags;
  std::optional<std::uint32_t> name_link;
};

std::vector<std::byte> make_plugin(std::initializer_list<Spec> records, std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FLST", 0);
  for (const auto& spec : records) {
    const auto record = fo4::test::begin_record(out, "FLST", spec.flags, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Item\0", 5});
    if (spec.name_link) {
      fo4::test::ByteWriter link;
      link.u32(*spec.name_link);
      fo4::test::write_subrecord(out, "LNAM", link.bytes);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::FormKey key(std::uint32_t local) { return fo4::FormKey{fo4::PluginKey::from_filename("Pet.esp"), fo4::LocalFormId{local}}; }

std::optional<fo4::FormKey> link_target(const fo4::Plugin& plugin, const fo4::FormKey& record) {
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord == fo4::Signature::from_string("LNAM")) return hit.key;
  }
  return std::nullopt;
}

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

void add_record(fo4::test::ByteWriter& out,
                std::string_view signature,
                std::uint32_t form_id,
                const std::vector<std::pair<std::string, std::vector<std::byte>>>& subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_field_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  add_record(out, "FLST", 0x800, {{"LNAM", words({0x801})}, {"LNAM", words({0x802})}});
  fo4::test::ByteWriter floats;
  for (int i = 1; i <= 12; ++i) floats.f32(static_cast<float>(i));
  add_record(out, "REFR", 0x801, {{"XHLT", words({50})}, {"XWCN", words({3})}, {"XWCU", floats.bytes}});
  return out.bytes;
}

std::vector<std::byte> make_nested_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x800);
  fo4::test::end_record(out, cell);
  const std::string label{char(0), char(8), char(0), char(0)};
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x801);
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

std::optional<std::uint32_t> first_word(const fo4::Plugin& plugin, std::uint32_t local, std::string_view sig) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return std::nullopt;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != sig || sub.data.size() < 4) continue;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sub.data[i])) << (8 * i);
    return value;
  }
  return std::nullopt;
}

std::optional<std::size_t> sub_size(const fo4::Plugin& plugin, std::uint32_t local, std::string_view sig) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return std::nullopt;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == sig) return sub.data.size();
  }
  return std::nullopt;
}

std::string read_field(const fo4::Plugin& plugin, std::uint32_t local, std::string_view path, std::size_t index) {
  std::string text;
  EXPECT_EQ(fo4::read_record_field(plugin, *fo4::find_record(plugin, key(local)), path, index, text), fo4::FieldStatus::ok);
  return text;
}

fo4::Plugin round_trip(const fo4::Plugin& plugin) { return fo4::read_plugin(fo4::write_plugin(plugin), "Pet.esp"); }

}  // namespace

TEST(PetWrite, CleanDeletedUndeletesAndDisablesOnlyDeletedRecords) {
  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0x20, std::nullopt}, {0x801, 0, std::nullopt}}, 0x802), "Pet.esp");
  const auto result = fo4::clean_deleted_records(plugin);
  EXPECT_EQ(result.undeleted, 1u);
  EXPECT_EQ(result.skipped, 0u);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Pet.esp");
  EXPECT_EQ(fo4::find_record(reloaded, key(0x800))->header.flags.value(), fo4::RecordFlags::initially_disabled);
  EXPECT_EQ(fo4::find_record(reloaded, key(0x801))->header.flags.value(), 0u);
  EXPECT_EQ(fo4::clean_deleted_records(plugin).undeleted, 0u);
}

TEST(PetWrite, RenumberAllShiftsUpWithoutCollidingAndRepointsLinks) {
  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0, 0x801}, {0x801, 0, std::nullopt}}, 0x802), "Pet.esp");
  const auto result = fo4::renumber_all_local_records(plugin, 0x801);
  EXPECT_EQ(result.status, fo4::RenumberAllStatus::renumbered);
  EXPECT_EQ(result.renumbered, 2u);
  EXPECT_EQ(fo4::find_record(plugin, key(0x800)), nullptr);
  ASSERT_NE(fo4::find_record(plugin, key(0x801)), nullptr);
  ASSERT_NE(fo4::find_record(plugin, key(0x802)), nullptr);
  EXPECT_EQ(link_target(plugin, key(0x801)), key(0x802));
}

TEST(PetWrite, RenumberAllShiftsDownAndLeavesSequentialPluginUnchanged) {
  auto plugin = fo4::read_plugin(make_plugin({{0x900, 0, std::nullopt}, {0x905, 0, 0x900}, {0x910, 0, 0x905}}, 0x911), "Pet.esp");
  EXPECT_EQ(fo4::renumber_all_local_records(plugin, 0x800).renumbered, 3u);
  EXPECT_NE(fo4::find_record(plugin, key(0x802)), nullptr);
  EXPECT_EQ(link_target(plugin, key(0x802)), key(0x801));
  EXPECT_EQ(link_target(plugin, key(0x801)), key(0x800));
  EXPECT_EQ(fo4::renumber_all_local_records(plugin, 0x800).status, fo4::RenumberAllStatus::unchanged);
}

TEST(PetWrite, RenumberAllRefusesARunThatLeavesTheLocalRangeWithoutChanges) {
  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0, std::nullopt}, {0x801, 0, std::nullopt}}, 0x802), "Pet.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::renumber_all_local_records(plugin, 0x7FF).status, fo4::RenumberAllStatus::out_of_range);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PetWrite, RenumberWithoutRepointKeepsLinksToTheOldKey) {
  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0, std::nullopt}, {0x801, 0, 0x800}}, 0x802), "Pet.esp");
  EXPECT_EQ(fo4::renumber_local_record(plugin, key(0x800), fo4::LocalFormId{0x900}, false), fo4::RecordRenumberStatus::renumbered);
  EXPECT_EQ(link_target(plugin, key(0x801)), key(0x800));
}

TEST(PetWrite, RetargetRewritesOnlyTheNamedRecord) {
  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0, std::nullopt}, {0x801, 0, 0x800}, {0x802, 0, 0x800}}, 0x803), "Pet.esp");
  fo4::assign_record_form_keys(plugin);
  EXPECT_EQ(fo4::rekey_record_form_links(plugin, key(0x801), key(0x800), key(0x802)), 1u);
  EXPECT_EQ(link_target(plugin, key(0x801)), key(0x802));
  EXPECT_EQ(link_target(plugin, key(0x802)), key(0x800));
}

TEST(PetWrite, FieldSchemaIsDerivedFromTheTablesAndAccessorsRefuseWrongKinds) {
  const auto rows = fo4::record_field_schema_rows(fo4::Signature::from_string("REFR"));
  const auto kind_of = [&](std::string_view path) -> std::optional<fo4::FieldKind> {
    for (const auto& row : rows) {
      if (row.path == path) return row.kind;
    }
    return std::nullopt;
  };
  EXPECT_EQ(kind_of("REFR\\HealthPercent"), fo4::FieldKind::scalar);
  EXPECT_EQ(kind_of("REFR\\WaterCurrentVelocities"), fo4::FieldKind::counted_array);

  auto plugin = fo4::read_plugin(make_plugin({{0x800, 0, std::nullopt}}, 0x801), "Pet.esp");
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x800), "MISC\\Nope", 0, "1"), fo4::FieldStatus::unknown_path);
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x999), "REFR\\HealthPercent", 0, "1"), fo4::FieldStatus::record_missing);
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x800), "REFR\\HealthPercent", 0, "1"), fo4::FieldStatus::record_mismatch);
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x800), "REFR\\WaterCurrentVelocities", 0, "1"),
            fo4::FieldStatus::record_mismatch);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key(0x800), "REFR\\HealthPercent", 0), fo4::FieldStatus::wrong_kind);
}

TEST(PetWrite, FieldWriteAndReadRoundTripThroughSaveAndReload) {
  auto plugin = fo4::read_plugin(make_field_plugin(), "Pet.esp");
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x801), "REFR\\HealthPercent", 0, "75"), fo4::FieldStatus::ok);
  EXPECT_EQ(read_field(plugin, 0x801, "REFR\\HealthPercent", 0), "75");
  EXPECT_EQ(read_field(round_trip(plugin), 0x801, "REFR\\HealthPercent", 0), "75");
  EXPECT_EQ(read_field(plugin, 0x800, "FLST\\Items", 1), key(0x802).to_string());
  EXPECT_EQ(fo4::write_record_field(plugin, key(0x801), "REFR\\HealthPercent", 5, "75"), fo4::FieldStatus::occurrence_missing);
}

TEST(PetWrite, ListEditsOfALinkListAndACountedArrayKeepTheCounter) {
  auto plugin = fo4::read_plugin(make_field_plugin(), "Pet.esp");
  std::size_t size = 0;
  const auto target = key(0x801);
  ASSERT_EQ(fo4::record_list_size(plugin, key(0x800), "FLST\\Items", size), fo4::FieldStatus::ok);
  EXPECT_EQ(size, 2u);
  EXPECT_EQ(fo4::insert_record_list_element(plugin, key(0x800), "FLST\\Items", 1, &target), fo4::FieldStatus::ok);
  EXPECT_EQ(read_field(plugin, 0x800, "FLST\\Items", 1), target.to_string());
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key(0x800), "FLST\\Items", 0), fo4::FieldStatus::ok);
  ASSERT_EQ(fo4::record_list_size(round_trip(plugin), key(0x800), "FLST\\Items", size), fo4::FieldStatus::ok);
  EXPECT_EQ(size, 2u);
  EXPECT_NE(fo4::insert_record_list_element(plugin, key(0x800), "FLST\\Items", 9, &target), fo4::FieldStatus::ok);

  const std::string_view path = "REFR\\WaterCurrentVelocities";
  ASSERT_EQ(fo4::record_list_size(plugin, key(0x801), path, size), fo4::FieldStatus::ok);
  EXPECT_EQ(size, 3u);
  EXPECT_EQ(fo4::insert_record_list_element(plugin, key(0x801), path, 1, nullptr), fo4::FieldStatus::ok);
  auto reread = round_trip(plugin);
  EXPECT_EQ(first_word(reread, 0x801, "XWCN"), 4u);
  EXPECT_EQ(sub_size(reread, 0x801, "XWCU"), 64u);
  ASSERT_EQ(fo4::record_list_size(reread, key(0x801), path, size), fo4::FieldStatus::ok);
  EXPECT_EQ(size, 4u);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key(0x801), path, 0), fo4::FieldStatus::ok);
  reread = round_trip(plugin);
  EXPECT_EQ(first_word(reread, 0x801, "XWCN"), 3u);
  EXPECT_EQ(sub_size(reread, 0x801, "XWCU"), 48u);
}

TEST(PetWrite, RenumberAllBlockedByNestedRecordsLeavesTheBytesUnchanged) {
  auto plugin = fo4::read_plugin(make_nested_plugin(), "Pet.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto result = fo4::renumber_all_local_records(plugin, 0x900);
  EXPECT_EQ(result.status, fo4::RenumberAllStatus::blocked_nested_records);
  ASSERT_EQ(result.blockers.size(), 1u);
  EXPECT_EQ(result.blockers.front(), key(0x801));
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_THROW(fo4::renumber_local_record(plugin, key(0x801), fo4::LocalFormId{0x900}), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
