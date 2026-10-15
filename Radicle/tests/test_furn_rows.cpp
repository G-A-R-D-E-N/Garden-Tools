#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_furn = fo4::Signature::from_string("FURN");
constexpr auto k_coct = fo4::Signature::from_string("COCT");
constexpr auto k_cnto = fo4::Signature::from_string("CNTO");

struct Piece {
  std::string_view signature;
  Bytes data;
};

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey misc_key() { return fo4::FormKey::parse("000910:Patch.esp"); }

fo4::schema::SubrecordGroupDescriptor items_group() {
  return fo4::schema::subrecord_group_descriptor(k_furn, "FURN\\Items").value();
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

Bytes zeros(std::size_t size) { return Bytes(size); }

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes i32_bytes(std::int32_t value) {
  fo4::test::ByteWriter out;
  out.i32(value);
  return out.bytes;
}

Bytes cnto_bytes(std::uint32_t item, std::int32_t count) {
  fo4::test::ByteWriter out;
  out.u32(item);
  out.i32(count);
  return out.bytes;
}

Bytes nam0_bytes(std::uint16_t points) {
  fo4::test::ByteWriter out;
  out.u8(0);
  out.u8(0);
  out.u16(points);
  return out.bytes;
}

Bytes fnpr_bytes(std::uint16_t type, std::uint16_t points) {
  fo4::test::ByteWriter out;
  out.u16(type);
  out.u16(points);
  return out.bytes;
}

std::vector<Piece> items_base() {
  return {
      {"COCT", u32_bytes(2)},
      {"CNTO", cnto_bytes(0x900, 5)},
      {"CNTO", cnto_bytes(0x901, 1)},
      {"MNAM", u32_bytes(0)},
      {"WBDT", zeros(1)},
  };
}

std::vector<Piece> markers_base() {
  return {
      {"MNAM", u32_bytes(0)},
      {"WBDT", zeros(1)},
      {"ENAM", i32_bytes(-1)},
      {"NAM0", nam0_bytes(12)},
      {"ENAM", i32_bytes(4)},
      {"NAM0", nam0_bytes(3)},
  };
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& container) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FURN", 0);
  const auto record = fo4::test::begin_record(out, "FURN", 0, 0x00000800);
  for (const auto& piece : container) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000910);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    names.push_back(subrecord.signature.to_string());
  }
  return names;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view signature) {
  std::vector<Bytes> out;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
  }
  return out;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::optional<std::uint32_t> cnto_item(const fo4::Plugin& plugin, std::size_t run) {
  std::size_t seen = 0;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature != k_cnto) continue;
    if (seen++ == run) return read_u32(subrecord.data, 0);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> stored_count(const fo4::Plugin& plugin) {
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature == k_coct && subrecord.data.size() == 4) return read_u32(subrecord.data, 0);
  }
  return std::nullopt;
}

}  // namespace

TEST(FurnRows, ItemsGroupNamesItsCountAndItemMember) {
  const auto group = items_group();
  EXPECT_EQ(group.record, k_furn);
  EXPECT_EQ(group.count_subrecord, k_coct);
  ASSERT_EQ(group.members.size(), 1u);
  EXPECT_EQ(group.members[0].subrecord, k_cnto);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 8u);
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(k_furn, k_cnto)->path, "FURN\\Items");
  EXPECT_TRUE(fo4::schema::is_subrecord_group_count_subrecord(k_furn, k_coct));
  EXPECT_FALSE(fo4::schema::is_subrecord_group_count_subrecord(k_furn, k_cnto));
}

TEST(FurnRows, ReadsEachRunsCountAndItemLink) {
  auto plugin = fo4::read_plugin(make_plugin(items_base()), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "FURN\\ItemTotal", 0), "2");
  EXPECT_EQ(read_member(plugin, "FURN\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(plugin, "FURN\\ItemCount", 1), "1");
  EXPECT_EQ(cnto_item(plugin, 0), 0x900u);
  EXPECT_EQ(cnto_item(plugin, 1), 0x901u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "FURN\\ItemCount"), 2u);
}

TEST(FurnRows, WritesOneRunsCountAndLinkWithoutTouchingTheTotal) {
  auto plugin = fo4::read_plugin(make_plugin(items_base()), "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\ItemCount"), 1, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(),
                                           fo4::schema::formlink_field_descriptor("FURN\\ItemObject").value(), 1,
                                           misc_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(read_member(after, "FURN\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(after, "FURN\\ItemCount", 1), "7");
  EXPECT_EQ(cnto_item(after, 0), 0x900u);
  EXPECT_EQ(cnto_item(after, 1), 0x910u);
  EXPECT_EQ(stored_count(after), 2u);
}

TEST(FurnRows, InsertsAndRemovesRunsAndSyncsTheTotal) {
  auto inserted = fo4::read_plugin(make_plugin(items_base()), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(inserted, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto inserted_after = reparse(inserted);
  EXPECT_EQ(signatures(inserted_after), (std::vector<std::string>{"COCT", "CNTO", "CNTO", "CNTO", "MNAM", "WBDT"}));
  EXPECT_EQ(stored_count(inserted_after), 3u);
  EXPECT_EQ(read_member(inserted_after, "FURN\\ItemCount", 1), "1");
  EXPECT_EQ(cnto_item(inserted_after, 1), 0u);
  EXPECT_EQ(cnto_item(inserted_after, 2), 0x901u);

  auto removed = fo4::read_plugin(make_plugin(items_base()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(removed, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto removed_after = reparse(removed);
  EXPECT_EQ(signatures(removed_after), (std::vector<std::string>{"COCT", "CNTO", "MNAM", "WBDT"}));
  EXPECT_EQ(stored_count(removed_after), 1u);
  EXPECT_EQ(cnto_item(removed_after, 0), 0x901u);
}

TEST(FurnRows, InsertsItemsBeforeTheFirstSuccessorOfAnItemlessRecord) {
  auto plugin = fo4::read_plugin(make_plugin({{"EDID", zeros(4)}, {"MNAM", u32_bytes(0)}, {"WBDT", zeros(1)}}),
                                 "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"EDID", "COCT", "CNTO", "MNAM", "WBDT"}));
  EXPECT_EQ(stored_count(after), 1u);
  EXPECT_EQ(read_member(after, "FURN\\ItemCount", 0), "1");
  EXPECT_EQ(cnto_item(after, 0), 0u);
}

TEST(FurnRows, ItemTotalIsRefusedAsAPlainScalarWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(items_base()), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\ItemTotal"), "5"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(FurnRows, MarkerIndexAndDisabledPointsEditByIndex) {
  auto plugin = fo4::read_plugin(make_plugin(markers_base()), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "FURN\\MarkerIndex"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "FURN\\MarkerDisabledPoints"), 2u);
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerIndex", 0), "-1");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerIndex", 1), "4");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerDisabledPoints", 0), "12");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerDisabledPoints", 1), "3");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\MarkerDisabledPoints"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\MarkerIndex"), 1, "-2"),
            fo4::ScalarMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(payloads(after, "NAM0"), (std::vector<Bytes>{nam0_bytes(5), nam0_bytes(3)}));
  EXPECT_EQ(payloads(after, "ENAM"), (std::vector<Bytes>{i32_bytes(-1), i32_bytes(-2)}));
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"MNAM", "WBDT", "ENAM", "NAM0", "ENAM", "NAM0"}));
}

TEST(FurnRows, MarkerEntryTypeAndPointsEditTheirOwnHalfOfEachEntry) {
  auto plugin = fo4::read_plugin(make_plugin({{"ENAM", i32_bytes(-1)},
                                              {"NAM0", nam0_bytes(12)},
                                              {"FNPR", fnpr_bytes(1, 13)},
                                              {"FNPR", fnpr_bytes(2, 14)}}),
                                 "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "FURN\\MarkerEntryType"), 2u);
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryType", 0), "1");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryType", 1), "2");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryPoints", 0), "13");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryPoints", 1), "14");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\MarkerEntryType"), 0, "4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("FURN\\MarkerEntryPoints"), 1, "1"),
            fo4::ScalarMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(payloads(after, "FNPR"), (std::vector<Bytes>{fnpr_bytes(4, 13), fnpr_bytes(2, 1)}));
}
