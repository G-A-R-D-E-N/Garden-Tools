#include "fo4/plugin/destructible_stage.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

fo4::FormKey bare_key() { return fo4::FormKey::parse("000C01:Patch.esp"); }

Bytes dest_payload() {
  ByteWriter out;
  out.i32(100);
  out.u8(0);
  out.u8(0);
  out.u16(0);
  return out.bytes;
}

Bytes make_plugin() {
  ByteWriter out;
  out.raw(fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"}));

  const auto acti_group = fo4::test::begin_group(out, "ACTI", 0);
  const auto bare = fo4::test::begin_record(out, "ACTI", 0, 0x01000C01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Bare\0", 5});
  fo4::test::end_record(out, bare);

  const auto modelled = fo4::test::begin_record(out, "ACTI", 0, 0x01000C02);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Modelled\0", 9});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"a.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "KSIZ", Bytes{std::byte{1}, std::byte{0}, std::byte{0},
                                                std::byte{0}});
  fo4::test::write_subrecord(out, "KWDA", Bytes(4));
  fo4::test::end_record(out, modelled);

  const auto navmeshed = fo4::test::begin_record(out, "ACTI", 0, 0x01000C03);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Navmeshed\0", 10});
  fo4::test::write_subrecord(out, "NVNM", Bytes(4));
  fo4::test::end_record(out, navmeshed);

  const auto staged = fo4::test::begin_record(out, "ACTI", 0, 0x01000C07);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Staged\0", 7});
  fo4::test::write_subrecord(out, "DSTD", Bytes(20));
  fo4::test::write_subrecord(out, "DSTD", Bytes(20));
  fo4::test::end_record(out, staged);

  const auto damaged = fo4::test::begin_record(out, "ACTI", 0, 0x01000C08);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Damaged\0", 8});
  fo4::test::write_subrecord(out, "DAMC", Bytes(8));
  fo4::test::end_record(out, damaged);

  const auto crowded = fo4::test::begin_record(out, "ACTI", 0, 0x01000C09);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Crowded\0", 8});
  for (int i = 0; i < 256; ++i) fo4::test::write_subrecord(out, "DSTD", Bytes(20));
  fo4::test::end_record(out, crowded);

  const auto headerless = fo4::test::begin_record(out, "ACTI", 0, 0x01000C0B);
  fo4::test::write_subrecord(out, "VMAD", Bytes(6));
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"h.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "KSIZ", Bytes{std::byte{1}, std::byte{0}, std::byte{0},
                                                std::byte{0}});
  fo4::test::end_record(out, headerless);

  const auto crossed = fo4::test::begin_record(out, "ACTI", 0, 0x01000C0C);
  fo4::test::write_subrecord(out, "KSIZ", Bytes{std::byte{1}, std::byte{0}, std::byte{0},
                                                std::byte{0}});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::end_record(out, crossed);
  fo4::test::end_group(out, acti_group);

  const auto door_group = fo4::test::begin_group(out, "DOOR", 0);
  const auto door = fo4::test::begin_record(out, "DOOR", 0, 0x01000C04);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
  fo4::test::end_record(out, door);
  fo4::test::end_group(out, door_group);

  const auto stat_group = fo4::test::begin_group(out, "STAT", 0);
  const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x01000C05);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Prop\0", 5});
  fo4::test::end_record(out, stat);
  fo4::test::end_group(out, stat_group);

  const auto armo_group = fo4::test::begin_group(out, "ARMO", 0);
  const auto twice = fo4::test::begin_record(out, "ARMO", 0, 0x01000C06);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Twice\0", 6});
  fo4::test::write_subrecord(out, "DEST", dest_payload());
  fo4::test::write_subrecord(out, "DEST", dest_payload());
  fo4::test::write_subrecord(out, "DATA", Bytes(12));
  fo4::test::end_record(out, twice);
  fo4::test::end_group(out, armo_group);

  const auto furn_group = fo4::test::begin_group(out, "FURN", 0);
  const auto marker = fo4::test::begin_record(out, "FURN", 0, 0x01000C0A);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"MarkerFurn\0", 11});
  fo4::test::write_subrecord(out, "ENAM", Bytes(4));
  fo4::test::write_subrecord(out, "NAM0", Bytes(4));
  fo4::test::write_subrecord(out, "FNPR", Bytes(4));
  fo4::test::write_subrecord(out, "XMRK", std::string_view{"Marker.nif\0", 11});
  fo4::test::write_subrecord(out, "SNAM", Bytes(24));
  fo4::test::write_subrecord(out, "NVNM", Bytes(4));
  fo4::test::end_record(out, marker);
  fo4::test::end_group(out, furn_group);
  return out.bytes;
}

std::vector<std::string> signatures_at(const fo4::Plugin& plugin, std::string_view key) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

const fo4::Subrecord* subrecord_at(const fo4::Plugin& plugin, std::string_view key,
                                   std::string_view signature, std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return nullptr;
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature.to_string() != signature) continue;
    if (seen == ordinal) return &subrecord;
    ++seen;
  }
  return nullptr;
}

std::uint8_t byte_at(const Bytes& data, std::size_t offset) {
  return std::to_integer<std::uint8_t>(data[offset]);
}

std::uint32_t u32_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(byte_at(data, offset + i)) << (8 * i);
  }
  return value;
}

std::int32_t i32_at(const Bytes& data, std::size_t offset) {
  return static_cast<std::int32_t>(u32_at(data, offset));
}

fo4::schema::ScalarFieldDescriptor scalar_row(std::string_view signature, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string(signature), path);
  if (!field.has_value()) throw std::runtime_error(std::string(path));
  return *field;
}

std::string read_row(const fo4::Plugin& plugin, const fo4::FormKey& key,
                     const fo4::schema::ScalarFieldDescriptor& field, std::size_t index) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size()) return {};
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(DestructibleHeader, InsertAndRemoveCreateAndDropTheWholeGroup) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto bare = bare_key();
  const auto modelled = fo4::FormKey::parse("000C02:Patch.esp");
  const auto door = fo4::FormKey::parse("000C04:Patch.esp");
  const auto stat = fo4::FormKey::parse("000C05:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::insert_destructible_header(plugin, fo4::FormKey::parse("000C99:Patch.esp")),
            fo4::DestructibleHeaderStatus::record_missing);
  EXPECT_EQ(fo4::insert_destructible_header(plugin, stat),
            fo4::DestructibleHeaderStatus::record_mismatch);
  EXPECT_EQ(fo4::insert_destructible_header(plugin, bare), fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::insert_destructible_header(plugin, bare),
            fo4::DestructibleHeaderStatus::header_present);
  EXPECT_EQ(fo4::insert_destructible_header(plugin, modelled),
            fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::insert_destructible_header(plugin, door), fo4::DestructibleHeaderStatus::updated);

  const auto health = scalar_row("ACTI", "ACTI\\DestructionHealth");
  const auto value = scalar_row("ACTI", "ACTI\\DestructionResistanceValue");
  const auto resistances = fo4::schema::counted_array_descriptor(
                               fo4::Signature::from_string("ACTI"), "ACTI\\DestructionResistances")
                               .value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, bare, health, 0, "800"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, bare, resistances, 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::insert_destructible_stage(plugin, bare, 0),
            fo4::DestructibleStageMutationStatus::updated);

  const auto built = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(built, "000C01:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DSTD", "DSTF"}));
  EXPECT_EQ(signatures_at(built, "000C02:Patch.esp"),
            (std::vector<std::string>{"EDID", "OBND", "MODL", "MODT", "DEST", "KSIZ", "KWDA"}));
  EXPECT_EQ(signatures_at(built, "000C04:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST"}));

  const auto* dest = subrecord_at(built, "000C01:Patch.esp", "DEST", 0);
  ASSERT_NE(dest, nullptr);
  ASSERT_EQ(dest->data.size(), 8u);
  EXPECT_EQ(i32_at(dest->data, 0), 800);
  EXPECT_EQ(byte_at(dest->data, 4), 1u);
  EXPECT_EQ(byte_at(dest->data, 5), 0u);
  EXPECT_EQ(byte_at(dest->data, 6), 0u);
  EXPECT_EQ(byte_at(dest->data, 7), 0u);
  const auto* dstd = subrecord_at(built, "000C01:Patch.esp", "DSTD", 0);
  ASSERT_NE(dstd, nullptr);
  EXPECT_EQ(dstd->data.size(), 20u);
  EXPECT_EQ(u32_at(dstd->data, 0), 0u);
  const auto* dstf = subrecord_at(built, "000C01:Patch.esp", "DSTF", 0);
  ASSERT_NE(dstf, nullptr);
  EXPECT_EQ(dstf->data.size(), 0u);
  const auto* modelled_dest = subrecord_at(built, "000C02:Patch.esp", "DEST", 0);
  ASSERT_NE(modelled_dest, nullptr);
  EXPECT_EQ(i32_at(modelled_dest->data, 0), 0);
  EXPECT_EQ(byte_at(modelled_dest->data, 4), 0u);
  EXPECT_EQ(read_row(built, bare, health, 0), "800");
  EXPECT_EQ(read_row(built, bare, value, 0), "");

  EXPECT_EQ(fo4::remove_destructible_header(plugin, bare), fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, bare),
            fo4::DestructibleHeaderStatus::header_missing);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, stat),
            fo4::DestructibleHeaderStatus::record_mismatch);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, fo4::FormKey::parse("000C99:Patch.esp")),
            fo4::DestructibleHeaderStatus::record_missing);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, modelled),
            fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, door), fo4::DestructibleHeaderStatus::updated);

  const auto cleared = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(cleared, "000C01:Patch.esp"), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(signatures_at(cleared, "000C02:Patch.esp"),
            (std::vector<std::string>{"EDID", "OBND", "MODL", "MODT", "KSIZ", "KWDA"}));
  EXPECT_EQ(signatures_at(cleared, "000C04:Patch.esp"), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

TEST(DestructibleHeader, InsertOrdersTheHeaderWithoutEdidAndRefusesACrossedRecord) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto headerless = fo4::FormKey::parse("000C0B:Patch.esp");
  const auto crossed = fo4::FormKey::parse("000C0C:Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_header(plugin, crossed),
            fo4::DestructibleHeaderStatus::unknown_order);
  EXPECT_EQ(fo4::write_plugin(plugin), original);

  EXPECT_EQ(fo4::insert_destructible_header(plugin, headerless),
            fo4::DestructibleHeaderStatus::updated);

  const auto built = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(built, "000C0B:Patch.esp"),
            (std::vector<std::string>{"VMAD", "OBND", "MODL", "MODT", "DEST", "KSIZ"}));
  EXPECT_EQ(signatures_at(built, "000C0C:Patch.esp"),
            (std::vector<std::string>{"KSIZ", "OBND"}));

  EXPECT_EQ(fo4::remove_destructible_header(plugin, headerless),
            fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}

TEST(DestructibleHeader, InsertRefusesARecordWithASubrecordOutsideItsRules) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::insert_destructible_header(plugin, fo4::FormKey::parse("000C03:Patch.esp")),
            fo4::DestructibleHeaderStatus::unknown_order);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(DestructibleHeader, InsertRefusesAStageCountThatDoesNotFitTheByte) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::insert_destructible_header(plugin, fo4::FormKey::parse("000C09:Patch.esp")),
            fo4::DestructibleHeaderStatus::too_many_stages);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(DestructibleHeader, DuplicatedHeaderIsPresentToInsertAndRemovable) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto twice = fo4::FormKey::parse("000C06:Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_header(plugin, twice),
            fo4::DestructibleHeaderStatus::header_present);
  EXPECT_EQ(fo4::remove_destructible_header(plugin, twice),
            fo4::DestructibleHeaderStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(reread, "000C06:Patch.esp"),
            (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(fo4::remove_destructible_header(plugin, twice),
            fo4::DestructibleHeaderStatus::header_missing);
}

TEST(DestructibleHeader, InsertSeedsTheStageCountAndRemoveDropsStagesWithoutADestHeader) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto staged = fo4::FormKey::parse("000C07:Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_header(plugin, staged),
            fo4::DestructibleHeaderStatus::updated);

  const auto built = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(built, "000C07:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DSTD", "DSTD"}));
  const auto* dest = subrecord_at(built, "000C07:Patch.esp", "DEST", 0);
  ASSERT_NE(dest, nullptr);
  ASSERT_EQ(dest->data.size(), 8u);
  EXPECT_EQ(byte_at(dest->data, 4), 2u);

  EXPECT_EQ(fo4::remove_destructible_header(plugin, staged),
            fo4::DestructibleHeaderStatus::updated);
  const auto cleared = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(cleared, "000C07:Patch.esp"), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(fo4::remove_destructible_header(plugin, staged),
            fo4::DestructibleHeaderStatus::header_missing);
}

TEST(DestructibleHeader, DamcWithoutDestIsRefusedOnInsertAndCleanedByRemove) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto damaged = fo4::FormKey::parse("000C08:Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_header(plugin, damaged),
            fo4::DestructibleHeaderStatus::header_present);
  EXPECT_EQ(signatures_at(plugin, "000C08:Patch.esp"),
            (std::vector<std::string>{"EDID", "DAMC"}));

  EXPECT_EQ(fo4::remove_destructible_header(plugin, damaged),
            fo4::DestructibleHeaderStatus::updated);
  const auto cleared = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(cleared, "000C08:Patch.esp"), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(fo4::remove_destructible_header(plugin, damaged),
            fo4::DestructibleHeaderStatus::header_missing);
}

TEST(DestructibleHeader, InsertAcceptsAFurnRecordThatCarriesMarkerData) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C0A:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::insert_destructible_header(plugin, key), fo4::DestructibleHeaderStatus::updated);
  const auto built = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(built, "000C0A:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "ENAM", "NAM0", "FNPR", "XMRK", "SNAM",
                                      "NVNM"}));

  EXPECT_EQ(fo4::remove_destructible_header(plugin, key), fo4::DestructibleHeaderStatus::updated);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}
