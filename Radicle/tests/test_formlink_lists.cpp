#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> plugin_with_race_hair_colors(bool malformed_second = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto color_group = fo4::test::begin_group(out, "CLFM", 0);
  auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800, 131);
  fo4::test::end_record(out, color);
  color = fo4::test::begin_record(out, "CLFM", 0, 0x00000801, 131);
  fo4::test::end_record(out, color);
  fo4::test::end_group(out, color_group);

  const auto npc_group = fo4::test::begin_group(out, "NPC_", 0);
  const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000802, 131);
  fo4::test::end_record(out, npc);
  fo4::test::end_group(out, npc_group);

  const auto race_group = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);
  fo4::test::ByteWriter first;
  first.u32(0x00000800);
  fo4::test::write_subrecord(out, "AHCM", first.bytes);
  if (malformed_second) {
    const std::vector<std::byte> malformed{std::byte{0x01}, std::byte{0x02}};
    fo4::test::write_subrecord(out, "AHCM", malformed);
  } else {
    fo4::test::ByteWriter second;
    second.u32(0x00000800);
    fo4::test::write_subrecord(out, "AHCM", second.bytes);
  }
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, race_group);
  return out.bytes;
}

std::uint32_t nth_raw(const fo4::Record& record,
                      std::string_view signature,
                      std::size_t wanted) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t ordinal = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    if (ordinal++ != wanted) continue;
    EXPECT_EQ(subrecord.data.size(), 4u);
    if (subrecord.data.size() != 4) return 0;
    std::uint32_t raw = 0;
    std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
    return raw;
  }
  ADD_FAILURE() << "missing requested subrecord ordinal";
  return 0;
}

std::size_t count_subrecords(const fo4::Record& record,
                             std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) ++count;
  }
  return count;
}

std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  ADD_FAILURE() << "missing requested subrecord";
  return record.subrecords.size();
}

// A RACE whose only FormLink-list subrecord is one sibling of MaleHairColors,
// so an empty MaleHairColors list has a schema sibling to anchor against.
std::vector<std::byte> plugin_with_race_sibling(std::string_view sibling_signature,
                                                std::uint32_t sibling_value) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto color_group = fo4::test::begin_group(out, "CLFM", 0);
  auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800, 131);
  fo4::test::end_record(out, color);
  color = fo4::test::begin_record(out, "CLFM", 0, 0x00000801, 131);
  fo4::test::end_record(out, color);
  fo4::test::end_group(out, color_group);

  const auto npc_group = fo4::test::begin_group(out, "NPC_", 0);
  const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000802, 131);
  fo4::test::end_record(out, npc);
  fo4::test::end_group(out, npc_group);

  const auto race_group = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);
  fo4::test::ByteWriter link;
  link.u32(sibling_value);
  fo4::test::write_subrecord(out, sibling_signature, link.bytes);
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, race_group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_door_destinations(const std::vector<std::uint32_t>& links) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  for (std::uint32_t id = 0x00000800; id <= 0x00000804; ++id) {
    const auto cell = fo4::test::begin_record(out, "CELL", 0, id, 131);
    fo4::test::end_record(out, cell);
  }
  fo4::test::end_group(out, cell_group);

  const auto door_group = fo4::test::begin_group(out, "DOOR", 0);
  const auto door = fo4::test::begin_record(out, "DOOR", 0, 0x00000805, 131);
  for (const auto link : links) {
    fo4::test::ByteWriter destination;
    destination.u32(link);
    fo4::test::write_subrecord(out, "TNAM", destination.bytes);
  }
  fo4::test::end_record(out, door);
  fo4::test::end_group(out, door_group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_otft_items(const std::vector<std::uint32_t>& items) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto armo_group = fo4::test::begin_group(out, "ARMO", 0);
  for (std::uint32_t id = 0x00000800; id <= 0x00000804; ++id) {
    const auto armo = fo4::test::begin_record(out, "ARMO", 0, id, 131);
    fo4::test::end_record(out, armo);
  }
  fo4::test::end_group(out, armo_group);

  const auto otft_group = fo4::test::begin_group(out, "OTFT", 0);
  const auto otft = fo4::test::begin_record(out, "OTFT", 0, 0x00000805, 131);
  fo4::test::ByteWriter payload;
  for (const auto item : items) payload.u32(item);
  fo4::test::write_subrecord(out, "INAM", payload.bytes);
  fo4::test::end_record(out, otft);
  fo4::test::end_group(out, otft_group);
  return out.bytes;
}

std::uint32_t nth_packed(const fo4::Record& record,
                         std::string_view signature,
                         std::size_t wanted) {
  const auto sig = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    EXPECT_EQ(subrecord.data.size() % 4, 0u);
    EXPECT_LT(wanted * 4 + 4, subrecord.data.size() + 1);
    std::uint32_t raw = 0;
    std::memcpy(&raw, subrecord.data.data() + wanted * 4, sizeof(raw));
    return raw;
  }
  ADD_FAILURE() << "missing packed list subrecord";
  return 0;
}

}

TEST(FormLinkListSchema, ParsesGeneratedIndexedRaceHeadPath) {
  const auto parsed =
      fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[12]");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->descriptor.record, fo4::Signature::from_string("RACE"));
  EXPECT_EQ(parsed->descriptor.subrecord, fo4::Signature::from_string("AHCM"));
  EXPECT_EQ(parsed->index, 12u);
  ASSERT_EQ(fo4::schema::formlink_list_targets(parsed->descriptor).size(), 1u);
  EXPECT_EQ(fo4::schema::formlink_list_targets(parsed->descriptor)[0],
            fo4::Signature::from_string("CLFM"));
  EXPECT_TRUE(parsed->descriptor.can_insert);
}

TEST(FormLinkListSchema, RejectsMalformedOrUnknownIndexedPaths) {
  EXPECT_FALSE(fo4::schema::indexed_formlink_list_path(
      "RACE\\MaleHairColors[]").has_value());
  EXPECT_FALSE(fo4::schema::indexed_formlink_list_path(
      "RACE\\MaleHairColors[-1]").has_value());
  EXPECT_FALSE(fo4::schema::indexed_formlink_list_path(
      "RACE\\Unknown[0]").has_value());
}

TEST(FormLinkListMutation, ReplacesOnlyRequestedExistingElement) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto new_color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto parsed =
      fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[1]");
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(
      fo4::schema::set_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, parsed->index, new_color),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "HeadLists.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  EXPECT_EQ(nth_raw(*race, "AHCM", 0), 0x00000800u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 1), 0x00000801u);
}

TEST(FormLinkListMutation, InsertsUsingExistingNeighborsWithoutTouchingThem) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto new_color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, *descriptor, 1, new_color),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "HeadLists.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  ASSERT_EQ(count_subrecords(*race, "AHCM"), 3u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 0), 0x00000800u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 1), 0x00000801u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 2), 0x00000800u);
}

TEST(FormLinkListMutation, AppendsUsingLastExistingNeighbor) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto new_color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, *descriptor, 2, new_color),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto* race = fo4::find_record(plugin, race_key);
  ASSERT_NE(race, nullptr);
  ASSERT_EQ(count_subrecords(*race, "AHCM"), 3u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 2), 0x00000801u);
}

TEST(FormLinkListMutation, RemovesOnlyRequestedElement) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(
      fo4::schema::remove_record_formlink_list_element(
          plugin, race_key, *descriptor, 0),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "HeadLists.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  ASSERT_EQ(count_subrecords(*race, "AHCM"), 1u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 0), 0x00000800u);
}

TEST(FormLinkListMutation, EmptyListInsertionFailsClosedWithoutSchemaAnchor) {
  // This RACE carries no sibling FormLink list, so once MaleHairColors is empty
  // there is nothing in the record to anchor the first element against and the
  // insert must fail closed. When a sibling is present the list can grow from
  // empty -- see the two anchoring tests below.
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  ASSERT_EQ(
      fo4::schema::remove_record_formlink_list_element(
          plugin, race_key, *descriptor, 1),
      fo4::schema::FormLinkListMutationStatus::updated);
  ASSERT_EQ(
      fo4::schema::remove_record_formlink_list_element(
          plugin, race_key, *descriptor, 0),
      fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(
      fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, *descriptor, 0, color),
      fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable);
}

TEST(FormLinkListMutation, InsertsFirstEmptyElementAfterPrecedingSibling) {
  // MalePresets (RPRM) precedes MaleHairColors (AHCM) in the RACE template, so
  // the first element of an empty MaleHairColors list anchors right after it.
  auto plugin = fo4::read_plugin(
      plugin_with_race_sibling("RPRM", 0x00000802), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto color = fo4::FormKey::parse("000800:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, *descriptor, 0, color),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "HeadLists.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  ASSERT_EQ(count_subrecords(*race, "AHCM"), 1u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 0), 0x00000800u);
  EXPECT_EQ(index_of(*race, "AHCM"), index_of(*race, "RPRM") + 1);
}

TEST(FormLinkListMutation, InsertsFirstEmptyElementBeforeFollowingSibling) {
  // With no preceding sibling present, MaleHairColors (AHCM) anchors right
  // before its nearest following sibling, MaleFaceDetails (FTSM).
  auto plugin = fo4::read_plugin(
      plugin_with_race_sibling("FTSM", 0x00000801), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto color = fo4::FormKey::parse("000800:HeadLists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, *descriptor, 0, color),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "HeadLists.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  ASSERT_EQ(count_subrecords(*race, "AHCM"), 1u);
  EXPECT_EQ(nth_raw(*race, "AHCM", 0), 0x00000800u);
  EXPECT_EQ(index_of(*race, "AHCM") + 1, index_of(*race, "FTSM"));
}

TEST(FormLinkListMutation, RejectsWrongLocalTargetType) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto npc = fo4::FormKey::parse("000802:HeadLists.esp");
  const auto parsed =
      fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[0]");
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(
      fo4::schema::set_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, parsed->index, npc),
      fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
}

TEST(FormLinkListMutation, RefusesOutOfRangeInsteadOfInserting) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto parsed =
      fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[2]");
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(
      fo4::schema::set_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, parsed->index, color),
      fo4::schema::FormLinkListMutationStatus::index_out_of_range);
}

TEST(FormLinkListMutation, MalformedExistingElementFailsClosed) {
  auto plugin = fo4::read_plugin(
      plugin_with_race_hair_colors(true), "HeadLists.esp");
  const auto race_key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto color = fo4::FormKey::parse("000801:HeadLists.esp");
  const auto parsed =
      fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[1]");
  ASSERT_TRUE(parsed.has_value());

  EXPECT_THROW(
      static_cast<void>(fo4::schema::set_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, parsed->index, color)),
      fo4::schema::FormLinkListError);
  EXPECT_THROW(
      static_cast<void>(fo4::schema::insert_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, 1, color)),
      fo4::schema::FormLinkListError);
  EXPECT_THROW(
      static_cast<void>(fo4::schema::remove_record_formlink_list_element(
          plugin, race_key, parsed->descriptor, 0)),
      fo4::schema::FormLinkListError);
}

TEST(FormLinkListRead, PreservesStoredOrderAndRefusesMalformedOrWrongRecord) {
  auto plugin = fo4::read_plugin(plugin_with_race_hair_colors(), "HeadLists.esp");
  const auto parsed = fo4::schema::indexed_formlink_list_path("RACE\\MaleHairColors[0]");
  ASSERT_TRUE(parsed.has_value());
  const auto key = fo4::FormKey::parse("000803:HeadLists.esp");
  const auto values = fo4::schema::record_formlink_list_values(plugin, key, parsed->descriptor);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000800:HeadLists.esp"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000800:HeadLists.esp"));
  EXPECT_FALSE(fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse("000900:HeadLists.esp"), parsed->descriptor));
  EXPECT_THROW(fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse("000800:HeadLists.esp"), parsed->descriptor), fo4::schema::FormLinkListError);
  const auto malformed = fo4::read_plugin(plugin_with_race_hair_colors(true), "HeadLists.esp");
  EXPECT_THROW(fo4::schema::record_formlink_list_values(malformed, key, parsed->descriptor),
               fo4::schema::FormLinkListError);
}

namespace {

std::vector<std::byte> plugin_with_mgef_counter_effects(std::size_t data_size = 22) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MGEF", 0);
  auto first = fo4::test::begin_record(out, "MGEF", 0, 0x00000800, 131);
  fo4::test::end_record(out, first);
  auto second = fo4::test::begin_record(out, "MGEF", 0, 0x00000801, 131);
  fo4::test::end_record(out, second);

  const auto host = fo4::test::begin_record(out, "MGEF", 0, 0x00000802, 131);
  std::vector<std::byte> data(data_size, std::byte{0});
  if (data_size >= 22) data[20] = std::byte{2};
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::ByteWriter entry;
  entry.u32(0x00000800);
  fo4::test::write_subrecord(out, "ESCE", entry.bytes);
  entry = fo4::test::ByteWriter{};
  entry.u32(0x00000801);
  fo4::test::write_subrecord(out, "ESCE", entry.bytes);
  fo4::test::end_record(out, host);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::uint16_t data_u16(const fo4::Record& record, std::size_t offset) {
  const auto data = fo4::Signature::from_string("DATA");
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != data) continue;
    if (subrecord.data.size() < offset + 2) break;
    std::uint16_t value = 0;
    std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
    return value;
  }
  ADD_FAILURE() << "missing DATA long enough for the counter";
  return 0;
}

}  // namespace

TEST(FormLinkListSchema, SortedListsCarryTheXEditSortFlag) {
  const auto door = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("DOOR"), "DOOR\\RandomTeleportDestinations");
  ASSERT_TRUE(door.has_value());
  EXPECT_TRUE(door->sorted);
  const auto otft = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("OTFT"), "OTFT\\Items");
  ASSERT_TRUE(otft.has_value());
  EXPECT_TRUE(otft->sorted);
  const auto counter_effects = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("MGEF"), "MGEF\\CounterEffects");
  ASSERT_TRUE(counter_effects.has_value());
  EXPECT_TRUE(counter_effects->sorted);
  const auto hair_colors = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(hair_colors.has_value());
  EXPECT_FALSE(hair_colors->sorted);
}

TEST(FormLinkListMutation, SortedListsPlaceTheNewFormIdAtItsValueNotTheCallerIndex) {
// Constraint: Regression: D-25 inserts landed at the caller's index even for xEdit's
  // sorted arrays. A caller asking for index 0 with a high FormID must still get
// Constraint: ascending FormID order, for both per-subrecord and packed storage.
  const auto door_key = fo4::FormKey::parse("000805:Sorted.esp");
  const auto door = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("DOOR"), "DOOR\\RandomTeleportDestinations");
  ASSERT_TRUE(door.has_value());
  auto plugin = fo4::read_plugin(
      plugin_with_door_destinations({0x00000800, 0x00000804}), "Sorted.esp");

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, door_key, *door, 0, fo4::FormKey::parse("000801:Sorted.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Sorted.esp");
  auto* record = fo4::find_record(reread, door_key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(nth_raw(*record, "TNAM", 0), 0x00000800u);
  EXPECT_EQ(nth_raw(*record, "TNAM", 1), 0x00000801u);
  EXPECT_EQ(nth_raw(*record, "TNAM", 2), 0x00000804u);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, door_key, *door, 0, fo4::FormKey::parse("000803:Sorted.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(plugin), "Sorted.esp");
  record = fo4::find_record(reread, door_key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(nth_raw(*record, "TNAM", 0), 0x00000800u);
  EXPECT_EQ(nth_raw(*record, "TNAM", 1), 0x00000801u);
  EXPECT_EQ(nth_raw(*record, "TNAM", 2), 0x00000803u);
  EXPECT_EQ(nth_raw(*record, "TNAM", 3), 0x00000804u);

  const auto otft_key = fo4::FormKey::parse("000805:Sorted.esp");
  const auto otft = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("OTFT"), "OTFT\\Items");
  ASSERT_TRUE(otft.has_value());
  auto packed = fo4::read_plugin(
      plugin_with_otft_items({0x00000800, 0x00000802}), "Sorted.esp");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                packed, otft_key, *otft, 0, fo4::FormKey::parse("000801:Sorted.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  auto packed_reread = fo4::read_plugin(fo4::write_plugin(packed), "Sorted.esp");
  record = fo4::find_record(packed_reread, otft_key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(nth_packed(*record, "INAM", 0), 0x00000800u);
  EXPECT_EQ(nth_packed(*record, "INAM", 1), 0x00000801u);
  EXPECT_EQ(nth_packed(*record, "INAM", 2), 0x00000802u);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                packed, otft_key, *otft, 0, fo4::FormKey::parse("000804:Sorted.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  packed_reread = fo4::read_plugin(fo4::write_plugin(packed), "Sorted.esp");
  record = fo4::find_record(packed_reread, otft_key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(nth_packed(*record, "INAM", 0), 0x00000800u);
  EXPECT_EQ(nth_packed(*record, "INAM", 1), 0x00000801u);
  EXPECT_EQ(nth_packed(*record, "INAM", 2), 0x00000802u);
  EXPECT_EQ(nth_packed(*record, "INAM", 3), 0x00000804u);
}

TEST(FormLinkListMutation, MaintainsTheCounterStoredInsideAnotherSubrecord) {
  auto plugin = fo4::read_plugin(plugin_with_mgef_counter_effects(), "CounterEffects.esp");
  const auto host_key = fo4::FormKey::parse("000802:CounterEffects.esp");
  const auto first = fo4::FormKey::parse("000800:CounterEffects.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("MGEF"), "MGEF\\CounterEffects");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->counter.to_string(), "DATA");
  EXPECT_EQ(descriptor->counter_offset, 20u);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, host_key, *descriptor, 2, first),
            fo4::schema::FormLinkListMutationStatus::updated);
  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "CounterEffects.esp");
  const auto* host = fo4::find_record(reread, host_key);
  ASSERT_NE(host, nullptr);
  ASSERT_EQ(count_subrecords(*host, "ESCE"), 3u);
  EXPECT_EQ(data_u16(*host, 20), 3u);

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, host_key, *descriptor, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(plugin), "CounterEffects.esp");
  host = fo4::find_record(reread, host_key);
  ASSERT_NE(host, nullptr);
  ASSERT_EQ(count_subrecords(*host, "ESCE"), 2u);
  EXPECT_EQ(data_u16(*host, 20), 2u);

  EXPECT_EQ(fo4::schema::clear_record_formlink_list(plugin, host_key, *descriptor),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(plugin), "CounterEffects.esp");
  host = fo4::find_record(reread, host_key);
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(count_subrecords(*host, "ESCE"), 0u);
  EXPECT_EQ(count_subrecords(*host, "DATA"), 1u);
  EXPECT_EQ(data_u16(*host, 20), 0u);
}

TEST(FormLinkListMutation, RefusesACounterSubrecordTooShortForTheCount) {
  auto plugin = fo4::read_plugin(plugin_with_mgef_counter_effects(8), "CounterEffects.esp");
  const auto host_key = fo4::FormKey::parse("000802:CounterEffects.esp");
  const auto first = fo4::FormKey::parse("000800:CounterEffects.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("MGEF"), "MGEF\\CounterEffects");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(fo4::schema::insert_record_formlink_list_element(
                   plugin, host_key, *descriptor, 2, first),
               fo4::schema::FormLinkListError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
