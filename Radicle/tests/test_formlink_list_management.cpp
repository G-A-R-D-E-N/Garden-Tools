#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> simple_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto colors = fo4::test::begin_group(out, "CLFM", 0);
  for (std::uint32_t id = 0x800; id <= 0x802; ++id) {
    const auto color = fo4::test::begin_record(out, "CLFM", 0, id, 131);
    fo4::test::end_record(out, color);
  }
  fo4::test::end_group(out, colors);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);
  for (std::uint32_t id : {0x800u, 0x801u, 0x802u}) {
    fo4::test::ByteWriter link;
    link.u32(id);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    if (id == 0x800u) {
      fo4::test::write_subrecord(out, "SGNM", std::string_view{"keep\0", 5});
    }
  }
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);
  return out.bytes;
}

std::vector<std::byte> nested_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x803);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto keywords = fo4::test::begin_group(out, "KYWD", 0);
  for (std::uint32_t id = 0x800; id <= 0x801; ++id) {
    const auto keyword = fo4::test::begin_record(out, "KYWD", 0, id, 131);
    fo4::test::end_record(out, keyword);
  }
  fo4::test::end_group(out, keywords);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000802, 131);
  for (std::uint32_t id : {0x800u, 0x801u}) {
    fo4::test::ByteWriter link;
    link.u32(id);
    fo4::test::write_subrecord(out, "SAKD", link.bytes);
    if (id == 0x800u) {
      fo4::test::write_subrecord(out, "SGNM", std::string_view{"preserve\0", 9});
    }
  }
  const std::vector<std::byte> terminator(4, std::byte{0});
  fo4::test::write_subrecord(out, "SRAF", terminator);
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);
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
    std::uint32_t raw = 0;
    if (subrecord.data.size() == 4) {
      std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
    }
    return raw;
  }
  ADD_FAILURE() << "missing requested subrecord";
  return 0;
}

std::size_t count_signature(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) ++count;
  }
  return count;
}

}  // namespace

TEST(FormLinkListManagement, QueriesMovesAndClearsSimpleListsWithoutMovingNeighbors) {
  auto plugin = fo4::read_plugin(simple_plugin(), "Lists.esp");
  const auto race = fo4::FormKey::parse("000803:Lists.esp");
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("RACE"), "RACE\\MaleHairColors");
  ASSERT_TRUE(descriptor.has_value());

  const auto initial = fo4::schema::record_formlink_list_size(plugin, race, *descriptor);
  ASSERT_TRUE(initial.has_value());
  EXPECT_EQ(*initial, 3u);

  ASSERT_EQ(
      fo4::schema::move_record_formlink_list_element(
          plugin, race, *descriptor, 0, 2),
      fo4::schema::FormLinkListMutationStatus::updated);
  const auto* moved = fo4::find_record(plugin, race);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(nth_raw(*moved, "AHCM", 0), 0x00000801u);
  EXPECT_EQ(nth_raw(*moved, "AHCM", 1), 0x00000802u);
  EXPECT_EQ(nth_raw(*moved, "AHCM", 2), 0x00000800u);
  EXPECT_EQ(count_signature(*moved, "SGNM"), 1u);

  ASSERT_EQ(
      fo4::schema::clear_record_formlink_list(plugin, race, *descriptor),
      fo4::schema::FormLinkListMutationStatus::updated);
  const auto* cleared = fo4::find_record(plugin, race);
  ASSERT_NE(cleared, nullptr);
  EXPECT_EQ(count_signature(*cleared, "AHCM"), 0u);
  EXPECT_EQ(count_signature(*cleared, "SGNM"), 1u);
  const auto size = fo4::schema::record_formlink_list_size(plugin, race, *descriptor);
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, 0u);
}

TEST(FormLinkListManagement, QueriesMovesAndClearsNestedListsWithoutRemovingTerminator) {
  auto plugin = fo4::read_plugin(nested_plugin(), "Nested.esp");
  const auto race = fo4::FormKey::parse("000802:Nested.esp");
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[0]\\ActorKeywords");
  ASSERT_TRUE(scoped.has_value());

  const auto initial = fo4::schema::record_nested_formlink_list_size(
      plugin, race, scoped->descriptor, scoped->container_index);
  ASSERT_TRUE(initial.has_value());
  EXPECT_EQ(*initial, 2u);

  ASSERT_EQ(
      fo4::schema::move_record_nested_formlink_list_element(
          plugin, race, scoped->descriptor, scoped->container_index, 1, 0),
      fo4::schema::FormLinkListMutationStatus::updated);
  const auto* moved = fo4::find_record(plugin, race);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(nth_raw(*moved, "SAKD", 0), 0x00000801u);
  EXPECT_EQ(nth_raw(*moved, "SAKD", 1), 0x00000800u);
  EXPECT_EQ(count_signature(*moved, "SGNM"), 1u);
  EXPECT_EQ(count_signature(*moved, "SRAF"), 1u);

  ASSERT_EQ(
      fo4::schema::clear_record_nested_formlink_list(
          plugin, race, scoped->descriptor, scoped->container_index),
      fo4::schema::FormLinkListMutationStatus::updated);
  const auto* cleared = fo4::find_record(plugin, race);
  ASSERT_NE(cleared, nullptr);
  EXPECT_EQ(count_signature(*cleared, "SAKD"), 0u);
  EXPECT_EQ(count_signature(*cleared, "SGNM"), 1u);
  EXPECT_EQ(count_signature(*cleared, "SRAF"), 1u);
  const auto size = fo4::schema::record_nested_formlink_list_size(
      plugin, race, scoped->descriptor, scoped->container_index);
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, 0u);
}
