#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_race = fo4::Signature::from_string("RACE");
constexpr auto k_hdpt = fo4::Signature::from_string("HDPT");
constexpr auto k_head = fo4::Signature::from_string("HEAD");
constexpr auto k_indx = fo4::Signature::from_string("INDX");

fo4::FormKey local_key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:Legacy.esp", local);
  return fo4::FormKey::parse(text);
}

fo4::FormKey race_key() { return local_key(0x0800); }

std::uint32_t raw_local(std::uint32_t local) { return 0x01000000u | local; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x0E00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

void add_group(ByteWriter& out, std::string_view signature,
               std::initializer_list<std::uint32_t> locals) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  for (const auto local : locals) {
    const auto record = fo4::test::begin_record(out, signature, 0, raw_local(local));
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

Bytes head_part_plugin() {
  ByteWriter out;
  write_header(out);
  add_group(out, "HDPT", {0x0A20, 0x0A21});
  add_group(out, "TXST", {0x0D02});

  const auto group = fo4::test::begin_group(out, "RACE", 0);
  const auto record = fo4::test::begin_record(out, "RACE", 0, raw_local(0x0800), 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, "INDX", u32_bytes(0));
  fo4::test::write_subrecord(out, "HEAD", u32_bytes(raw_local(0x0A20)));
  fo4::test::write_subrecord(out, "INDX", u32_bytes(1));
  fo4::test::write_subrecord(out, "HEAD", u32_bytes(raw_local(0x0A21)));
  fo4::test::write_subrecord(out, "INDX", u32_bytes(0));
  fo4::test::write_subrecord(out, "HEAD", u32_bytes(raw_local(0x0A21)));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::FormLinkListDescriptor head_list() {
  const auto list = fo4::schema::formlink_list_descriptor(k_race, "RACE\\HeadParts");
  EXPECT_TRUE(list.has_value());
  return list.value_or(fo4::schema::FormLinkListDescriptor{});
}

const fo4::Record* race_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, race_key());
}

Bytes payload_of(const fo4::Plugin& plugin, fo4::Signature signature, std::size_t index) {
  const auto* record = race_record(plugin);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return {};
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != signature) continue;
    if (seen == index) return subrecord.data;
    ++seen;
  }
  return {};
}

std::string read_index(const fo4::Plugin& plugin, std::size_t index) {
  const auto field = fo4::schema::scalar_field_descriptor("RACE\\HeadPartNumber");
  EXPECT_TRUE(field.has_value());
  if (!field.has_value()) return "missing";
  const auto* record = race_record(plugin);
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field->scalar), field->scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field->scalar),
      field->scalar);
}

std::size_t count_hits(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto hits = fo4::collect_form_links(plugin);
  return static_cast<std::size_t>(std::count_if(hits.begin(), hits.end(), [&](const auto& hit) {
    return hit.record == race_key() && hit.subrecord == signature;
  }));
}

std::vector<fo4::FormKey> head_values(const fo4::Plugin& plugin) {
  const auto values = fo4::schema::record_formlink_list_values(plugin, race_key(), head_list());
  EXPECT_TRUE(values.has_value());
  return values.value_or(std::vector<fo4::FormKey>{});
}

}  // namespace

TEST(RaceHeadParts, RowsMatchXEditHeadPartPairs) {
  const auto number = fo4::schema::scalar_field_descriptor("RACE\\HeadPartNumber");
  ASSERT_TRUE(number.has_value());
  EXPECT_EQ(number->scalar.subrecord, k_indx);
  EXPECT_EQ(number->scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(number->scalar.offset, 0u);
  EXPECT_EQ(number->scalar.payload_size, 4u);
  EXPECT_TRUE(number->scalar.address.repeated);
  EXPECT_FALSE(number->can_insert);

  const auto list = head_list();
  EXPECT_EQ(list.subrecord, k_head);
  EXPECT_EQ(list.storage, fo4::schema::FormLinkListStorage::per_subrecord);
  EXPECT_TRUE(list.nullable);
  EXPECT_FALSE(list.can_insert);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(list, k_hdpt));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(list, fo4::Signature::from_string("TXST")));
}

TEST(RaceHeadParts, ReadsIndexValuesAndHeadLinksInRecordOrder) {
  auto plugin = fo4::read_plugin(head_part_plugin(), "Legacy.esp");
  EXPECT_EQ(read_index(plugin, 0), "0");
  EXPECT_EQ(read_index(plugin, 1), "1");
  EXPECT_EQ(read_index(plugin, 2), "0");
  EXPECT_EQ(read_index(plugin, 3), "absent");

  EXPECT_EQ(head_values(plugin),
            (std::vector<fo4::FormKey>{local_key(0x0A20), local_key(0x0A21), local_key(0x0A21)}));
  EXPECT_EQ(count_hits(plugin, k_head), 3u);
}

TEST(RaceHeadParts, SetsHdptLinkAndRefusesNonHdptTarget) {
  auto plugin = fo4::read_plugin(head_part_plugin(), "Legacy.esp");
  const auto list = head_list();

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, race_key(), list, 2,
                                                          local_key(0x0A20)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, race_key(), list, 1,
                                                          local_key(0x0D02)),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(head_values(plugin),
            (std::vector<fo4::FormKey>{local_key(0x0A20), local_key(0x0A21), local_key(0x0A20)}));
  EXPECT_EQ(payload_of(plugin, k_head, 1), u32_bytes(raw_local(0x0A21)));
}

TEST(RaceHeadParts, SaveAndReparseKeepsIndexPairsAndLinks) {
  auto plugin = fo4::read_plugin(head_part_plugin(), "Legacy.esp");
  const auto list = head_list();
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, race_key(), list, 0,
                                                          local_key(0x0A21)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, race_key(), list, 2,
                                                          local_key(0x0A20)),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  EXPECT_EQ(head_values(reread),
            (std::vector<fo4::FormKey>{local_key(0x0A21), local_key(0x0A21), local_key(0x0A20)}));
  EXPECT_EQ(read_index(reread, 0), "0");
  EXPECT_EQ(read_index(reread, 1), "1");
  EXPECT_EQ(read_index(reread, 2), "0");
  EXPECT_EQ(payload_of(reread, k_indx, 1), u32_bytes(1));
  EXPECT_EQ(payload_of(reread, k_head, 2), u32_bytes(raw_local(0x0A20)));
  EXPECT_EQ(count_hits(reread, k_head), 3u);
  EXPECT_TRUE(fo4::collect_form_links_detailed(reread).unclaimed_link_payloads.empty());
}