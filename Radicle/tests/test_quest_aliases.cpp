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

void write_i32_subrecord(fo4::test::ByteWriter& out,
                         std::string_view signature,
                         std::int32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  fo4::test::write_subrecord(out, signature, data);
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
  write_u32_subrecord(out, "FNAM", 0x12);
  write_i32_subrecord(out, "ALFI", -1);
  const std::vector<std::byte> unknown{
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  fo4::test::write_subrecord(out, "ZZZZ", unknown);
  if (!missing_reference_end) {
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  }

  write_u32_subrecord(out, "ALLS", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"LocationTwo\0", 12});
  write_u32_subrecord(out, "FNAM", 0x34);
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
  auto plugin = fo4::read_plugin(quest_plugin(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);

  EXPECT_EQ(aliases.next_alias_id, 10u);
  ASSERT_EQ(aliases.aliases.size(), 3u);

  const auto& reference = aliases.aliases[0];
  EXPECT_EQ(reference.kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(reference.id, 1u);
  EXPECT_EQ(reference.name, std::optional<std::string>{"ReferenceOne"});
  EXPECT_EQ(reference.flags, std::optional<std::uint32_t>{0x12});
  EXPECT_EQ(reference.force_into_alias, std::optional<std::int32_t>{-1});
  EXPECT_TRUE(reference.collection_entries.empty());
  ASSERT_EQ(reference.subrecords.size(), 6u);
  EXPECT_EQ(reference.subrecords[0].signature.to_string(), "ALST");
  EXPECT_EQ(reference.subrecords[1].signature.to_string(), "ALID");
  EXPECT_EQ(reference.subrecords[2].signature.to_string(), "FNAM");
  EXPECT_EQ(reference.subrecords[3].signature.to_string(), "ALFI");
  EXPECT_EQ(reference.subrecords[4].signature.to_string(), "ZZZZ");
  EXPECT_EQ(reference.subrecords[4].data,
            (std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}}));
  EXPECT_EQ(reference.subrecords[5].signature.to_string(), "ALED");

  const auto& location = aliases.aliases[1];
  EXPECT_EQ(location.kind, fo4::forms::QuestAliasKind::location);
  EXPECT_EQ(location.id, 2u);
  EXPECT_EQ(location.name, std::optional<std::string>{"LocationTwo"});
  EXPECT_EQ(location.flags, std::optional<std::uint32_t>{0x34});
  EXPECT_FALSE(location.force_into_alias.has_value());
  ASSERT_EQ(location.subrecords.size(), 4u);
  EXPECT_EQ(location.subrecords.front().signature.to_string(), "ALLS");
  EXPECT_EQ(location.subrecords.back().signature.to_string(), "ALED");

  const auto& collection = aliases.aliases[2];
  EXPECT_EQ(collection.kind, fo4::forms::QuestAliasKind::collection);
  EXPECT_EQ(collection.id, 0u);
  EXPECT_FALSE(collection.name.has_value());
  EXPECT_FALSE(collection.flags.has_value());
  ASSERT_EQ(collection.collection_entries.size(), 1u);
  EXPECT_EQ(collection.collection_entries[0].id, 3u);
  EXPECT_EQ(collection.collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{5});
  ASSERT_EQ(collection.subrecords.size(), 2u);
  EXPECT_EQ(collection.subrecords[0].signature.to_string(), "ALCS");
  EXPECT_EQ(raw_u32(collection.subrecords[0]), 3u);
  EXPECT_EQ(collection.subrecords[1].signature.to_string(), "ALMI");
  ASSERT_EQ(collection.subrecords[1].data.size(), 1u);
  EXPECT_EQ(collection.subrecords[1].data[0], std::byte{5});
}

TEST(QuestAliases, MissingEndMarkerFailsClosedBeforeNextAlias) {
  auto plugin = fo4::read_plugin(
      quest_plugin(true, false, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliases, OrphanEndMarkerFailsClosed) {
  auto plugin = fo4::read_plugin(
      quest_plugin(false, true, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliases, MalformedCollectionALMIFailsClosed) {
  auto plugin = fo4::read_plugin(
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

TEST(QuestAliases, RequiresReferenceNameAndReferenceLocationFlags) {
  auto plugin = fo4::read_plugin(quest_plugin(), "Quest.esp");
  auto* quest = const_cast<fo4::Record*>(
      fo4::find_record(plugin, fo4::FormKey::parse("000800:Quest.esp")));
  ASSERT_NE(quest, nullptr);

  for (auto it = quest->subrecords.begin(); it != quest->subrecords.end(); ++it) {
    if (it->signature == fo4::Signature::from_string("FNAM")) {
      quest->subrecords.erase(it);
      break;
    }
  }
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

// Real mods ship dangling FormLinks: OWR.esp has 28 ALFR references naming
// master index 3 while declaring a single master. Refusing to decode would
// make the whole quest unreadable in an editor whose job is to report exactly
// that. The link is dropped from the semantic fill, recorded, and the raw
// subrecord is preserved.
TEST(QuestAliases, DanglingFormLinkDegradesTheFieldNotTheQuest) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x01000800);
  fo4::test::ByteWriter anam;
  anam.u32(1);
  fo4::test::write_subrecord(out, "ANAM", anam.bytes);
  fo4::test::ByteWriter alst;
  alst.u32(0);
  fo4::test::write_subrecord(out, "ALST", alst.bytes);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
  fo4::test::ByteWriter fnam;
  fnam.u32(0);
  fo4::test::write_subrecord(out, "FNAM", fnam.bytes);
  // Master index 3 with only one master declared.
  fo4::test::ByteWriter alfr;
  alfr.u32(0x03000123);
  fo4::test::write_subrecord(out, "ALFR", alfr.bytes);
  fo4::test::write_subrecord(out, "ALED", std::string_view{"", 0});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Dangling.esp");
  const auto key = fo4::FormKey::parse("000800:Dangling.esp");

  const auto decoded = fo4::forms::decode_quest_aliases(plugin, key);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  const auto& alias = decoded.aliases[0];

  // Reported rather than thrown.
  ASSERT_EQ(alias.unresolved_links.size(), 1u);
  EXPECT_EQ(alias.unresolved_links[0].to_string(), "ALFR");

  // The raw subrecord survives, so a rewrite is unaffected.
  bool kept = false;
  for (const auto& sub : alias.subrecords) {
    if (sub.signature.to_string() == "ALFR") kept = true;
  }
  EXPECT_TRUE(kept);
}
