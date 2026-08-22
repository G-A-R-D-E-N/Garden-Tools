#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

std::vector<std::byte> quest_plugin(bool missing_reference_end = false,
                                    bool orphan_end = false,
                                    bool malformed_collection_count = false) {
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
  fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
  write_u32_subrecord(out, "ANAM", 10);

  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"ReferenceOne\0", 13});
  const std::vector<std::byte> unknown{
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  fo4::test::write_subrecord(out, "ZZZZ", unknown);
  if (!missing_reference_end) {
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  }

  write_u32_subrecord(out, "ALLS", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"LocationTwo\0", 12});
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  write_u32_subrecord(out, "ALCS", 3);
  if (malformed_collection_count) {
    const std::vector<std::byte> malformed{std::byte{1}, std::byte{2}};
    fo4::test::write_subrecord(out, "ALMI", malformed);
  } else {
    const std::vector<std::byte> max_fill{std::byte{5}};
    fo4::test::write_subrecord(out, "ALMI", max_fill);
  }

  if (orphan_end) {
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  }

  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::uint32_t raw_u32(const fo4::Subrecord& subrecord) {
  EXPECT_EQ(subrecord.data.size(), 4u);
  if (subrecord.data.size() != 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

}

TEST(QuestAliases, PreservesMixedAliasVariantsAndRepeatedBoundaries) {
  const auto plugin = fo4::read_plugin(quest_plugin(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);

  EXPECT_EQ(aliases.next_alias_id, 10u);
  ASSERT_EQ(aliases.aliases.size(), 3u);

  const auto& reference = aliases.aliases[0];
  EXPECT_EQ(reference.kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(reference.id, 1u);
  ASSERT_EQ(reference.subrecords.size(), 4u);
  EXPECT_EQ(reference.subrecords[0].signature.to_string(), "ALST");
  EXPECT_EQ(reference.subrecords[1].signature.to_string(), "ALID");
  EXPECT_EQ(reference.subrecords[2].signature.to_string(), "ZZZZ");
  EXPECT_EQ(reference.subrecords[2].data,
            (std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}}));
  EXPECT_EQ(reference.subrecords[3].signature.to_string(), "ALED");

  const auto& location = aliases.aliases[1];
  EXPECT_EQ(location.kind, fo4::forms::QuestAliasKind::location);
  EXPECT_EQ(location.id, 2u);
  ASSERT_EQ(location.subrecords.size(), 3u);
  EXPECT_EQ(location.subrecords.front().signature.to_string(), "ALLS");
  EXPECT_EQ(location.subrecords.back().signature.to_string(), "ALED");

  const auto& collection = aliases.aliases[2];
  EXPECT_EQ(collection.kind, fo4::forms::QuestAliasKind::collection);
  EXPECT_EQ(collection.id, 3u);
  ASSERT_EQ(collection.subrecords.size(), 2u);
  EXPECT_EQ(collection.subrecords[0].signature.to_string(), "ALCS");
  EXPECT_EQ(raw_u32(collection.subrecords[0]), 3u);
  EXPECT_EQ(collection.subrecords[1].signature.to_string(), "ALMI");
  ASSERT_EQ(collection.subrecords[1].data.size(), 1u);
  EXPECT_EQ(collection.subrecords[1].data[0], std::byte{5});
}

TEST(QuestAliases, MissingEndMarkerFailsClosedBeforeNextAlias) {
  const auto plugin = fo4::read_plugin(
      quest_plugin(true, false, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliases, OrphanEndMarkerFailsClosed) {
  const auto plugin = fo4::read_plugin(
      quest_plugin(false, true, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliases, MalformedCollectionALMIFailsClosed) {
  const auto plugin = fo4::read_plugin(
      quest_plugin(false, false, true), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliases, RequiresUniqueFourByteNextAliasId) {
  auto bytes = quest_plugin();
  auto plugin = fo4::read_plugin(bytes, "Quest.esp");
  auto* quest = const_cast<fo4::Record*>(
      fo4::find_record(plugin, fo4::FormKey::parse("000800:Quest.esp")));
  ASSERT_NE(quest, nullptr);
  quest->subrecords.push_back(
      fo4::Subrecord{fo4::Signature::from_string("ANAM"),
                     std::vector<std::byte>(4, std::byte{0}), false});
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}
