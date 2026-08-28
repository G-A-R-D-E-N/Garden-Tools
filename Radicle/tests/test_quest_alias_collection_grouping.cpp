#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

void write_u32_subrecord(fo4::test::ByteWriter& out,
                         std::string_view signature,
                         std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

void write_u8_subrecord(fo4::test::ByteWriter& out,
                        std::string_view signature,
                        std::uint8_t value) {
  fo4::test::write_subrecord(out, signature,
                             std::vector<std::byte>{std::byte{value}});
}

std::vector<std::byte> multi_entry_collection_quest() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  write_u32_subrecord(out, "ANAM", 10);

  write_u32_subrecord(out, "ALCS", 5);
  write_u8_subrecord(out, "ALMI", 2);
  write_u32_subrecord(out, "ALCS", 7);

  write_u32_subrecord(out, "ALST", 5);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"RefFive\0", 8});
  write_u32_subrecord(out, "FNAM", 0);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  write_u32_subrecord(out, "ALST", 8);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"RefEight\0", 9});
  write_u32_subrecord(out, "FNAM", 0);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(QuestAliasCollectionGrouping, GroupsContiguousAlcsRunAsOneAliasWithOrderedEntries) {
  const auto plugin = fo4::read_plugin(multi_entry_collection_quest(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);

  ASSERT_EQ(aliases.aliases.size(), 3u);

  const auto& collection = aliases.aliases[0];
  EXPECT_EQ(collection.kind, fo4::forms::QuestAliasKind::collection);
  EXPECT_EQ(collection.id, 0u);
  ASSERT_EQ(collection.collection_entries.size(), 2u);
  EXPECT_EQ(collection.collection_entries[0].id, 5u);
  EXPECT_EQ(collection.collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{2});
  EXPECT_EQ(collection.collection_entries[1].id, 7u);
  EXPECT_FALSE(collection.collection_entries[1].max_initial_fill_count.has_value());
  ASSERT_EQ(collection.subrecords.size(), 3u);
  EXPECT_EQ(collection.subrecords[0].signature.to_string(), "ALCS");
  EXPECT_EQ(collection.subrecords[1].signature.to_string(), "ALMI");
  EXPECT_EQ(collection.subrecords[2].signature.to_string(), "ALCS");

  EXPECT_EQ(aliases.aliases[1].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(aliases.aliases[1].id, 5u);
  EXPECT_EQ(aliases.aliases[2].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(aliases.aliases[2].id, 8u);
  EXPECT_EQ(aliases.next_alias_id, 10u);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "Quest.esp");
  const auto round = fo4::forms::decode_quest_aliases(reread, quest);
  ASSERT_EQ(round.aliases.size(), 3u);
  ASSERT_EQ(round.aliases[0].collection_entries.size(), 2u);
  EXPECT_EQ(round.aliases[0].collection_entries[0].id, 5u);
  EXPECT_EQ(round.aliases[0].collection_entries[1].id, 7u);
  EXPECT_EQ(round.aliases[1].id, 5u);
  EXPECT_EQ(round.aliases[2].id, 8u);
}

TEST(QuestAliasCollectionGrouping, ReorderReferenceAcrossCollectionKeepsRunIntact) {
  auto plugin = fo4::read_plugin(multi_entry_collection_quest(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");

  ASSERT_TRUE(fo4::forms::reorder_quest_alias(plugin, quest, 8, 0));

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "Quest.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(reread, quest);

  ASSERT_EQ(aliases.aliases.size(), 3u);
  EXPECT_EQ(aliases.aliases[0].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(aliases.aliases[0].id, 8u);
  EXPECT_EQ(aliases.aliases[1].kind, fo4::forms::QuestAliasKind::collection);
  ASSERT_EQ(aliases.aliases[1].collection_entries.size(), 2u);
  EXPECT_EQ(aliases.aliases[1].collection_entries[0].id, 5u);
  EXPECT_EQ(aliases.aliases[1].collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{2});
  EXPECT_EQ(aliases.aliases[1].collection_entries[1].id, 7u);
  EXPECT_FALSE(
      aliases.aliases[1].collection_entries[1].max_initial_fill_count.has_value());
  EXPECT_EQ(aliases.aliases[2].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(aliases.aliases[2].id, 5u);
  EXPECT_EQ(aliases.next_alias_id, 10u);
}
