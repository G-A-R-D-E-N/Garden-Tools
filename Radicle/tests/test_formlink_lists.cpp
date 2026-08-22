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
