#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr auto k_cont = fo4::Signature::from_string("CONT");
constexpr auto k_coct = fo4::Signature::from_string("COCT");
constexpr auto k_cnto = fo4::Signature::from_string("CNTO");
constexpr auto k_coed = fo4::Signature::from_string("COED");
constexpr auto k_xtra = fo4::Signature::from_string("XTRA");

constexpr std::array<fo4::schema::SubrecordGroupMemberDescriptor, 3> k_custom_members{{
    {k_cnto, true, false, 4, {}, false, false},
    {k_coed, false, true, 4, {}, false, false},
    {k_xtra, true, false, 4, {}, false, false},
}};

struct Piece {
  std::string_view signature;
  std::vector<std::byte> data;
};

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey misc_key() { return fo4::FormKey::parse("000910:Patch.esp"); }

fo4::schema::SubrecordGroupDescriptor items_group() {
  return fo4::schema::subrecord_group_descriptor(k_cont, "CONT\\Items").value();
}

fo4::schema::SubrecordGroupDescriptor custom_group() {
  return {.record = k_cont,
          .path = "CONT\\Custom",
          .label = "Custom",
          .count_subrecord = k_coct,
          .members = std::span<const fo4::schema::SubrecordGroupMemberDescriptor>(k_custom_members),
          .successors = {}};
}

std::vector<std::byte> zeros(std::size_t size) { return std::vector<std::byte>(size); }

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> cnto_bytes(std::uint32_t item, std::int32_t count) {
  fo4::test::ByteWriter out;
  out.u32(item);
  out.i32(count);
  return out.bytes;
}

std::vector<Piece> base_items() {
  return {
      {"COCT", u32_bytes(2)},
      {"CNTO", cnto_bytes(0x900, 5)},
      {"COED", zeros(12)},
      {"CNTO", cnto_bytes(0x901, 1)},
      {"DATA", zeros(4)},
  };
}

std::vector<Piece> three_runs() {
  return {
      {"COCT", u32_bytes(3)},
      {"CNTO", cnto_bytes(0x900, 5)},
      {"COED", zeros(12)},
      {"CNTO", cnto_bytes(0x901, 1)},
      {"CNTO", cnto_bytes(0x902, 2)},
      {"DATA", zeros(4)},
  };
}

void write_container(fo4::test::ByteWriter& out, std::uint32_t form_id, const std::vector<Piece>& container) {
  const auto group = fo4::test::begin_group(out, "CONT", 0);
  const auto record = fo4::test::begin_record(out, "CONT", 0, form_id);
  for (const auto& piece : container) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
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
  write_container(out, 0x00000800, container);
  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000910);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);
  return out.bytes;
}

std::vector<std::byte> masters_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"RadiationOverhaul.esp\0", 22});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  write_container(out, 0x03000800,
                  {{"COCT", u32_bytes(2)}, {"CNTO", cnto_bytes(0x0200009A, 5)}, {"CNTO", cnto_bytes(0x0200009B, 1)}});
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
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

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = fo4::schema::scalar_field_descriptor(path).value();
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

TEST(SubrecordGroup, DescriptorNamesItsRecordCountAndMembers) {
  const auto group = items_group();
  EXPECT_EQ(group.record, k_cont);
  EXPECT_EQ(group.count_subrecord, k_coct);
  ASSERT_EQ(group.members.size(), 2u);
  EXPECT_EQ(group.members[0].subrecord, k_cnto);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 8u);
  EXPECT_EQ(group.members[1].subrecord, k_coed);
  EXPECT_FALSE(group.members[1].required);
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(k_cont, k_coed)->path, "CONT\\Items");
  EXPECT_FALSE(fo4::schema::subrecord_group_for_member(k_cont, fo4::Signature::from_string("DATA")).has_value());
  EXPECT_TRUE(fo4::schema::is_subrecord_group_count_subrecord(k_cont, k_coct));
  EXPECT_FALSE(fo4::schema::is_subrecord_group_count_subrecord(k_cont, k_cnto));
  EXPECT_FALSE(fo4::schema::subrecord_group_descriptor(fo4::Signature::from_string("CELL"), "CONT\\Items")
                   .has_value());
}

TEST(SubrecordGroup, EnumeratesEachRunFromItsLeaderThroughItsMembers) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, record_key(), items_group()).value();
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0].first, 1u);
  EXPECT_EQ(spans[0].end, 3u);
  EXPECT_EQ(spans[1].first, 3u);
  EXPECT_EQ(spans[1].end, 4u);
  EXPECT_FALSE(fo4::subrecord_group_spans(plugin, fo4::FormKey::parse("000999:Patch.esp"), items_group())
                   .has_value());
}

TEST(SubrecordGroup, ReadsEachRunsCountAndItemLink) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "CONT\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(plugin, "CONT\\ItemCount", 1), "1");
  EXPECT_EQ(cnto_item(plugin, 0), 0x900u);
  EXPECT_EQ(cnto_item(plugin, 1), 0x901u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "CONT\\ItemCount"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), fo4::schema::formlink_field_descriptor("CONT\\ItemObject").value()),
            2u);
}

TEST(SubrecordGroup, WritesOneRunsCountAndLinkWithoutTouchingTheCount) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  const auto count = fo4::schema::scalar_field_descriptor("CONT\\ItemCount").value();
  const auto item = fo4::schema::formlink_field_descriptor("CONT\\ItemObject").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), count, 1, "7"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), item, 1, misc_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 1), "7");
  EXPECT_EQ(cnto_item(after, 0), 0x900u);
  EXPECT_EQ(cnto_item(after, 1), 0x910u);
  EXPECT_EQ(stored_count(after), 2u);
}

TEST(SubrecordGroup, InsertsARunWithItsDefaultsAtTheFrontAndSyncsTheCount) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"COCT", "CNTO", "CNTO", "COED", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(after), 3u);
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 0), "1");
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 1), "5");
  EXPECT_EQ(cnto_item(after, 0), 0u);
  EXPECT_EQ(cnto_item(after, 1), 0x900u);
  const auto spans = fo4::subrecord_group_spans(after, record_key(), items_group()).value();
  ASSERT_EQ(spans.size(), 3u);
  EXPECT_EQ(spans[2].first, 4u);
  EXPECT_EQ(spans[2].end, 5u);
}

TEST(SubrecordGroup, InsertsInTheMiddleAndAtTheEnd) {
  auto middle = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(middle, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto middle_after = reparse(middle);
  EXPECT_EQ(signatures(middle_after), (std::vector<std::string>{"COCT", "CNTO", "COED", "CNTO", "CNTO", "DATA"}));
  EXPECT_EQ(read_member(middle_after, "CONT\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(middle_after, "CONT\\ItemCount", 1), "1");
  EXPECT_EQ(cnto_item(middle_after, 1), 0u);
  EXPECT_EQ(cnto_item(middle_after, 2), 0x901u);

  auto end = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(end, record_key(), items_group(), 2),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto end_after = reparse(end);
  EXPECT_EQ(signatures(end_after), (std::vector<std::string>{"COCT", "CNTO", "COED", "CNTO", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(end_after), 3u);
  EXPECT_EQ(cnto_item(end_after, 1), 0x901u);
  EXPECT_EQ(cnto_item(end_after, 2), 0u);
}

TEST(SubrecordGroup, InsertsIntoAnEmptyGroupAfterTheCount) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(signatures(reparse(plugin)), (std::vector<std::string>{"COCT", "DATA"}));

  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"COCT", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(after), 1u);
  EXPECT_EQ(cnto_item(after, 0), 0u);
}

TEST(SubrecordGroup, RemovesFirstMiddleAndLastRunsWithTheirOptionalMembers) {
  auto first = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(first, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto first_after = reparse(first);
  EXPECT_EQ(signatures(first_after), (std::vector<std::string>{"COCT", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(first_after), 1u);
  EXPECT_EQ(cnto_item(first_after, 0), 0x901u);

  auto middle = fo4::read_plugin(make_plugin(three_runs()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(middle, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto middle_after = reparse(middle);
  EXPECT_EQ(signatures(middle_after), (std::vector<std::string>{"COCT", "CNTO", "COED", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(middle_after), 2u);
  EXPECT_EQ(cnto_item(middle_after, 0), 0x900u);
  EXPECT_EQ(cnto_item(middle_after, 1), 0x902u);

  auto last = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(last, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto last_after = reparse(last);
  EXPECT_EQ(signatures(last_after), (std::vector<std::string>{"COCT", "CNTO", "COED", "DATA"}));
  EXPECT_EQ(stored_count(last_after), 1u);
  EXPECT_EQ(cnto_item(last_after, 0), 0x900u);
}

TEST(SubrecordGroup, RemovingEveryRunKeepsTheCountAtZero) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);

  const auto emptied = reparse(plugin);
  EXPECT_EQ(signatures(emptied), (std::vector<std::string>{"COCT", "DATA"}));
  EXPECT_EQ(stored_count(emptied), 0u);
}

TEST(SubrecordGroup, CountSubrecordIsRefusedAsAPlainScalarWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto total = fo4::schema::scalar_field_descriptor("CONT\\ItemTotal").value();
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), total, "5"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, MismatchedCountAndOrphanMembersFailClosed) {
  auto mismatch = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(3)},
                                                {"CNTO", cnto_bytes(0x900, 5)},
                                                {"COED", zeros(12)},
                                                {"CNTO", cnto_bytes(0x901, 1)},
                                                {"DATA", zeros(4)}}),
                                   "Patch.esp");
  const auto mismatch_before = fo4::write_plugin(mismatch);
  EXPECT_THROW(fo4::insert_record_subrecord_group(mismatch, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(mismatch, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(mismatch), mismatch_before);

  auto orphan = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)},
                                              {"COED", zeros(12)},
                                              {"CNTO", cnto_bytes(0x900, 5)},
                                              {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto orphan_before = fo4::write_plugin(orphan);
  const auto count = fo4::schema::scalar_field_descriptor("CONT\\ItemCount").value();
  EXPECT_THROW(fo4::set_record_scalar(orphan, record_key(), count, 0, "1"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::insert_record_subrecord_group(orphan, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(orphan, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(orphan), orphan_before);
}

TEST(SubrecordGroup, OutOfRangeMissingRecordAndMissingCountRefuseWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 3),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, fo4::FormKey::parse("000999:Patch.esp"), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, MissingCountWithRunsIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"CNTO", cnto_bytes(0x900, 5)}, {"DATA", zeros(4)}}), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, InsertsIntoAnItemlessContainerWithANewCountBeforeItsFirstSuccessor) {
  auto plugin = fo4::read_plugin(make_plugin({{"EDID", zeros(4)}, {"DATA", zeros(4)}}), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"EDID", "COCT", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(after), 1u);
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 0), "1");
  EXPECT_EQ(cnto_item(after, 0), 0u);
}

TEST(SubrecordGroup, ItemlessContainerWithNoSuccessorGetsItsNewCountAndRunAtTheEnd) {
  auto plugin = fo4::read_plugin(make_plugin({{"EDID", zeros(4)}}), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"EDID", "COCT", "CNTO"}));
  EXPECT_EQ(stored_count(after), 1u);
}

TEST(SubrecordGroup, TwoCountSubrecordsFailClosedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)},
                                              {"COCT", u32_bytes(1)},
                                              {"CNTO", cnto_bytes(0x900, 5)},
                                              {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, CountWithTheWrongSizeFailsClosedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", zeros(2)}, {"CNTO", cnto_bytes(0x900, 5)}, {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(plugin, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, DuplicateCoedInOneRunFailsClosedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)},
                                              {"CNTO", cnto_bytes(0x900, 5)},
                                              {"COED", zeros(12)},
                                              {"COED", zeros(12)},
                                              {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, FormLinkWriteOnABrokenRunFailsClosedWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)}, {"CNTO", zeros(4)}, {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto item = fo4::schema::formlink_field_descriptor("CONT\\ItemObject").value();
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, record_key(), item, 0, misc_key()),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordGroup, OptionalCoedIsNotCreatedByInsert) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)}, {"CNTO", cnto_bytes(0x900, 5)}, {"DATA", zeros(4)}}),
                                 "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, record_key(), items_group()).value();
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].end, 2u);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(signatures(reparse(plugin)), (std::vector<std::string>{"COCT", "CNTO", "CNTO", "DATA"}));
}

TEST(SubrecordGroup, CustomDescriptorEnforcesLeaderRequiredOrderRepeatAndPayloadRules) {
  const auto group = custom_group();
  auto valid = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)},
                                             {"CNTO", zeros(4)},
                                             {"COED", zeros(4)},
                                             {"COED", zeros(4)},
                                             {"XTRA", zeros(4)}}),
                                "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(valid, record_key(), group).value();
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].first, 1u);
  EXPECT_EQ(spans[0].end, 5u);

  const auto rejects = [&](const std::vector<Piece>& container) {
    auto plugin = fo4::read_plugin(make_plugin(container), "Patch.esp");
    EXPECT_THROW(fo4::subrecord_group_spans(plugin, record_key(), group), fo4::schema::SubrecordGroupError);
  };
  rejects({{"COCT", u32_bytes(1)}, {"CNTO", zeros(4)}});
  rejects({{"COCT", u32_bytes(1)}, {"CNTO", zeros(4)}, {"XTRA", zeros(4)}, {"COED", zeros(4)}});
  rejects({{"COCT", u32_bytes(1)}, {"CNTO", zeros(4)}, {"XTRA", zeros(4)}, {"XTRA", zeros(4)}});
  rejects({{"COCT", u32_bytes(1)}, {"CNTO", zeros(8)}, {"XTRA", zeros(4)}});
}

TEST(SubrecordGroup, TwoCntoLinksAreRemappedOncePerItem) {
  auto plugin = fo4::read_plugin(masters_plugin(), "Patch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto after = reparse(plugin);
  EXPECT_EQ(cnto_item(after, 0), 0x0100009Au);
  EXPECT_EQ(cnto_item(after, 1), 0x0100009Bu);
  EXPECT_EQ(stored_count(after), 2u);
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 1), "1");
}

TEST(SubrecordGroup, ItemLinkAcceptsNull) {
  auto plugin = fo4::read_plugin(make_plugin(base_items()), "Patch.esp");
  const auto item = fo4::schema::formlink_field_descriptor("CONT\\ItemObject").value();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), item, 1, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(cnto_item(reparse(plugin), 1), 0u);
}

TEST(SubrecordGroup, InsertsAtTheEndOfTheRunsAfterTheCount) {
  auto plugin = fo4::read_plugin(make_plugin({{"COCT", u32_bytes(1)}, {"CNTO", cnto_bytes(0x900, 5)}, {"DATA", zeros(4)}}),
                                 "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"COCT", "CNTO", "CNTO", "DATA"}));
  EXPECT_EQ(stored_count(after), 2u);
  EXPECT_EQ(cnto_item(after, 0), 0x900u);
  EXPECT_EQ(cnto_item(after, 1), 0u);
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(after, "CONT\\ItemCount", 1), "1");
  EXPECT_EQ(record_of(after).subrecords[2].data, cnto_bytes(0, 1));
  EXPECT_EQ(record_of(after).subrecords[3].data, zeros(4));
}

TEST(SubrecordGroup, InsertPastTheEndOfAnItemlessContainerFailsWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin({{"EDID", zeros(4)}, {"DATA", zeros(4)}}), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), items_group(), 1),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(signatures(plugin), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_FALSE(record_of(plugin).original_range.empty());
}
