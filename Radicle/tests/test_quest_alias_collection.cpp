#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

std::vector<std::byte> quest_bytes(bool second_alias_after_tail,
                                   bool duplicate_id) {
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

  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"One\0", 4});
  write_u32_subrecord(out, "FNAM", 0);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  if (duplicate_id) {
    write_u32_subrecord(out, "ALLS", 1);
    write_u32_subrecord(out, "FNAM", 0);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  }

  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});

  if (second_alias_after_tail) {
    write_u32_subrecord(out, "ALCS", 9);
    const std::vector<std::byte> count{std::byte{1}};
    fo4::test::write_subrecord(out, "ALMI", count);
  }

  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
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

std::vector<std::byte> empty_quest_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
  write_u32_subrecord(out, "ANAM", 1);
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::forms::QuestAliasCreation collection_entries(
    std::vector<fo4::forms::QuestCollectionEntry> entries) {
  fo4::forms::QuestAliasCreation creation;
  creation.kind = fo4::forms::QuestAliasKind::collection;
  creation.collection_entries = std::move(entries);
  return creation;
}

fo4::forms::QuestAliasCreation reference_alias(std::uint32_t id) {
  fo4::forms::QuestAliasCreation creation;
  creation.kind = fo4::forms::QuestAliasKind::reference;
  creation.id = id;
  creation.name = std::string_view{"ReferenceAlias"};
  creation.flags = 0u;
  return creation;
}

std::vector<std::byte> two_collections_separated() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
  write_u32_subrecord(out, "ANAM", 10);
  write_u32_subrecord(out, "ALCS", 1);
  write_u32_subrecord(out, "ALST", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"Separator\0", 10});
  write_u32_subrecord(out, "FNAM", 0);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  write_u32_subrecord(out, "ALCS", 3);
  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

void write_cnto_subrecord(fo4::test::ByteWriter& out,
                          std::uint32_t item,
                          std::int32_t count) {
  fo4::test::ByteWriter data;
  data.u32(item);
  data.i32(count);
  fo4::test::write_subrecord(out, "CNTO", data.bytes);
}

void write_coed_subrecord(fo4::test::ByteWriter& out) {
  fo4::test::ByteWriter data;
  data.u32(0);
  data.i32(0);
  data.f32(0.0f);
  fo4::test::write_subrecord(out, "COED", data.bytes);
}

std::vector<std::byte> item_run_quest() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  write_u32_subrecord(out, "ANAM", 3);
  write_u32_subrecord(out, "ALST", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"ItemRun\0", 8});
  write_u32_subrecord(out, "FNAM", 0);
  write_u32_subrecord(out, "COCT", 2);
  write_cnto_subrecord(out, 5, 1);
  write_coed_subrecord(out);
  write_cnto_subrecord(out, 6, 3);
  write_coed_subrecord(out);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> reference_extras_quest() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  write_u32_subrecord(out, "ANAM", 2);
  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"Ref\0", 4});
  write_u32_subrecord(out, "FNAM", 0);
  write_u32_subrecord(out, "KSIZ", 1);
  write_u32_subrecord(out, "KWDA", 0x111);
  write_u32_subrecord(out, "SPOR", 0x222);
  write_u32_subrecord(out, "OCOR", 0x333);
  write_u32_subrecord(out, "GWOR", 0x444);
  write_u32_subrecord(out, "ECOR", 0x555);
  fo4::test::ByteWriter alla;
  alla.u32(0x666);
  alla.i32(4);
  fo4::test::write_subrecord(out, "ALLA", alla.bytes);
  write_u32_subrecord(out, "ALPC", 0x777);
  write_u32_subrecord(out, "VTCK", 0x888);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> payload_of(const fo4::forms::QuestAlias& alias,
                                  std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (const auto& member : alias.subrecords) {
    if (member.signature == sig) return member.data;
  }
  return {};
}

const fo4::FormKey k_quest = fo4::FormKey::parse("000800:Quest.esp");

}

TEST(QuestAliasBoundary, RejectsSecondAliasArrayAfterTail) {
  auto plugin = fo4::read_plugin(
      quest_bytes(true, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliasBoundary, RejectsDuplicateAliasIdsDuringDecode) {
  auto plugin = fo4::read_plugin(
      quest_bytes(false, true), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliasCollectionGrouping, GroupsContiguousAlcsRunAsOneAliasWithOrderedEntries) {
  auto plugin = fo4::read_plugin(multi_entry_collection_quest(), "Quest.esp");
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

TEST(QuestAliasCollectionAuthoring, EncodesOptionalAlmiPerEntryInOrder) {
  auto creation = collection_entries({
      {5, std::uint8_t{2}},
      {7, std::nullopt},
      {9, std::uint8_t{1}},
  });
  const auto alias = fo4::forms::encode_quest_alias(creation);
  ASSERT_EQ(alias.collection_entries.size(), 3u);
  EXPECT_EQ(alias.collection_entries[0].id, 5u);
  EXPECT_EQ(alias.collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{2});
  EXPECT_EQ(alias.collection_entries[1].id, 7u);
  EXPECT_FALSE(alias.collection_entries[1].max_initial_fill_count.has_value());
  EXPECT_EQ(alias.collection_entries[2].id, 9u);
  std::vector<std::string> names;
  for (const auto& member : alias.subrecords) {
    names.push_back(member.signature.to_string());
  }
  const std::vector<std::string> expected{"ALCS", "ALMI", "ALCS", "ALCS", "ALMI"};
  EXPECT_EQ(names, expected);
}

TEST(QuestAliasCollectionAuthoring, RoundTripsThroughWriteAndKeepsAnamOnRefIds) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Quest.esp");
  const std::vector<fo4::forms::QuestAliasCreation> creations{
      collection_entries({{11, std::uint8_t{3}}, {4, std::nullopt}}),
      reference_alias(2),
  };
  fo4::forms::write_quest_aliases(plugin, k_quest, creations);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Quest.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 2u);
  EXPECT_EQ(decoded.aliases[0].kind, fo4::forms::QuestAliasKind::collection);
  ASSERT_EQ(decoded.aliases[0].collection_entries.size(), 2u);
  EXPECT_EQ(decoded.aliases[0].collection_entries[0].id, 11u);
  EXPECT_EQ(decoded.aliases[0].collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{3});
  EXPECT_EQ(decoded.aliases[0].collection_entries[1].id, 4u);
  EXPECT_EQ(decoded.aliases[1].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(decoded.aliases[1].id, 2u);
  EXPECT_EQ(decoded.next_alias_id, 3u);
}

TEST(QuestAliasCollectionAuthoring, RejectsDuplicateEntryIdsAndAdjacentCollections) {
  EXPECT_THROW(
      (void)fo4::forms::encode_quest_alias(collection_entries({{1, std::nullopt}, {1, std::nullopt}})),
      fo4::forms::QuestError);

  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Quest.esp");
  const std::vector<fo4::forms::QuestAliasCreation> adjacent{
      collection_entries({{1, std::nullopt}}),
      collection_entries({{2, std::nullopt}}),
  };
  EXPECT_THROW(
      fo4::forms::write_quest_aliases(plugin, k_quest, adjacent),
      fo4::forms::QuestError);
}

TEST(QuestAliasCollectionAuthoring, RejectsCollectionEntriesOnReferenceAliases) {
  auto creation = reference_alias(1);
  creation.collection_entries = {{2, std::nullopt}};
  EXPECT_THROW(
      (void)fo4::forms::encode_quest_alias(creation), fo4::forms::QuestError);
}

TEST(QuestAliasCollectionGrouping, RefusesMutationsThatWouldJoinCollectionRuns) {
  auto plugin = fo4::read_plugin(two_collections_separated(), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::remove_quest_alias(plugin, k_quest, 2)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::reorder_quest_alias(plugin, k_quest, 2, 0)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto joined = collection_entries({{2, std::uint8_t{1}}});
  joined.id = 2;
  EXPECT_THROW(
      fo4::forms::replace_quest_alias(plugin, k_quest, 2, joined),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(plugin, k_quest, collection_entries({{4, std::uint8_t{1}}})),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasBoundary, RejectsAddingAnAliasWhenTheTailAlreadyHoldsMarkers) {
  auto plugin = fo4::read_plugin(quest_bytes(true, false), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(plugin, k_quest, collection_entries({{3, std::uint8_t{1}}})),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasReplacementPreservation, KeepsAnItemRunWithCoedInOrderOnARename) {
  auto plugin = fo4::read_plugin(item_run_quest(), "Quest.esp");
  auto renamed = reference_alias(2);
  renamed.name = std::string_view{"Renamed"};
  fo4::forms::replace_quest_alias(plugin, k_quest, 2, renamed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Quest.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  const auto& alias = decoded.aliases[0];
  std::vector<std::string> names;
  for (const auto& member : alias.subrecords) {
    names.push_back(member.signature.to_string());
  }
  EXPECT_EQ(names, (std::vector<std::string>{"ALST", "ALID", "FNAM", "COCT", "CNTO",
                                             "COED", "CNTO", "COED", "ALED"}));
  ASSERT_EQ(alias.items.size(), 2u);
  EXPECT_EQ(alias.items[0].item, fo4::FormKey::parse("000005:Quest.esp"));
  EXPECT_EQ(alias.items[0].count, 1);
  EXPECT_EQ(alias.items[1].item, fo4::FormKey::parse("000006:Quest.esp"));
  EXPECT_EQ(alias.items[1].count, 3);
  EXPECT_TRUE(alias.items[0].extra.has_value());
  EXPECT_TRUE(alias.items[1].extra.has_value());
}

TEST(QuestAliasReplacementPreservation, KeepsReferenceExtrasInOrderOnARename) {
  auto plugin = fo4::read_plugin(reference_extras_quest(), "Quest.esp");
  const auto initial = fo4::forms::decode_quest_aliases(plugin, k_quest);
  ASSERT_EQ(initial.aliases.size(), 1u);
  const auto& before = initial.aliases[0];

  auto renamed = reference_alias(1);
  renamed.name = std::string_view{"Renamed"};
  fo4::forms::replace_quest_alias(plugin, k_quest, 1, renamed);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Quest.esp");
  EXPECT_EQ(fo4::write_plugin(reread), bytes);
  const auto decoded = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  const auto& after = decoded.aliases[0];
  std::vector<std::string> names;
  for (const auto& member : after.subrecords) {
    names.push_back(member.signature.to_string());
  }
  EXPECT_EQ(names, (std::vector<std::string>{"ALST", "ALID", "FNAM", "KSIZ", "KWDA",
                                             "SPOR", "OCOR", "GWOR", "ECOR", "ALLA",
                                             "ALPC", "VTCK", "ALED"}));
  for (const auto* signature : {"KSIZ", "KWDA", "SPOR", "OCOR", "GWOR", "ECOR", "ALLA",
                                "ALPC", "VTCK"}) {
    EXPECT_EQ(payload_of(after, signature), payload_of(before, signature)) << signature;
  }
  EXPECT_EQ(after.keywords, before.keywords);
  EXPECT_EQ(after.spectator_override, before.spectator_override);
  EXPECT_EQ(after.combat_override, before.combat_override);
  EXPECT_EQ(after.packages, before.packages);
  EXPECT_EQ(after.voice_types, before.voice_types);
}

TEST(QuestAliasReplacementPreservation, RefusesAKindChangeToACollectionWithResidualMembers) {
  auto plugin = fo4::read_plugin(reference_extras_quest(), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  auto collection = collection_entries({{2, std::uint8_t{1}}});
  collection.id = 1;
  EXPECT_THROW(fo4::forms::replace_quest_alias(plugin, k_quest, 1, collection),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
