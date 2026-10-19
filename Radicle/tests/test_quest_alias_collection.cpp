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

void write_i32_subrecord(fo4::test::ByteWriter& out,
                         std::string_view signature,
                         std::int32_t value) {
  fo4::test::ByteWriter data;
  data.i32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

// Every form ID offset in this CTDA payload must stay null.
std::vector<std::byte> ctda_payload() {
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  data.u16(1);
  data.u16(0);
  data.u32(0);
  data.u32(0);
  data.u8(0);
  data.u8(0);
  data.u16(0);
  data.u32(0);
  data.u32(0);
  return data.bytes;
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

fo4::forms::QuestAliasCreation location_alias(std::uint32_t id) {
  fo4::forms::QuestAliasCreation creation;
  creation.kind = fo4::forms::QuestAliasKind::location;
  creation.id = id;
  creation.flags = 0x20u;
  return creation;
}

using MemberList = std::vector<std::pair<std::string, std::vector<std::byte>>>;

MemberList alias_members(const fo4::Record& record, std::string_view start) {
  const auto start_signature = fo4::Signature::from_string(start);
  MemberList members;
  bool inside = false;
  for (const auto& subrecord : record.subrecords) {
    if (!inside) {
      if (subrecord.signature != start_signature) continue;
      inside = true;
    }
    members.emplace_back(subrecord.signature.to_string(), subrecord.data);
    if (subrecord.signature.to_string() == "ALED") break;
  }
  return members;
}

std::vector<std::string> member_names(const MemberList& members) {
  std::vector<std::string> names;
  names.reserve(members.size());
  for (const auto& member : members) names.push_back(member.first);
  return names;
}

std::vector<std::byte> member_payload(const MemberList& members,
                                      std::string_view signature) {
  for (const auto& member : members) {
    if (member.first == signature) return member.second;
  }
  return {};
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  return data.bytes;
}

const fo4::FormKey k_quest = fo4::FormKey::parse("000800:Quest.esp");

// A replacement must keep the ALFI, ALID, ALEA, CTDA/CIS1 and ALCC here.
std::vector<std::byte> unmodelled_alias_member_quest() {
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

  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"RefOne\0", 7});
  write_u32_subrecord(out, "FNAM", 0x10);
  write_i32_subrecord(out, "ALFI", -3);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  write_u32_subrecord(out, "ALLS", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"LocTwo\0", 7});
  write_u32_subrecord(out, "FNAM", 0x20);
  write_i32_subrecord(out, "ALFI", -7);
  write_i32_subrecord(out, "ALEA", 5);
  fo4::test::write_subrecord(out, "CTDA", ctda_payload());
  fo4::test::write_subrecord(out, "CIS1", std::string_view{"One\0", 4});
  write_i32_subrecord(out, "ALCC", 4);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

// ALFL must not be placeable in the reference alias ordering.
std::vector<std::byte> reference_alias_with_location_member() {
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
  write_u32_subrecord(out, "ANAM", 1);
  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"RefOne\0", 7});
  write_u32_subrecord(out, "FNAM", 0x10);
  write_u32_subrecord(out, "ALFL", 7);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

// Preserved members must be in member order: ALCC before CTDA is not.
std::vector<std::byte> location_alias_out_of_member_order() {
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
  write_u32_subrecord(out, "ALLS", 2);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"LocTwo\0", 7});
  write_u32_subrecord(out, "FNAM", 0x20);
  write_i32_subrecord(out, "ALCC", 4);
  fo4::test::write_subrecord(out, "CTDA", ctda_payload());
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(QuestAliasBoundary, RejectsSecondAliasArrayAfterTail) {
  const auto plugin = fo4::read_plugin(
      quest_bytes(true, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliasBoundary, RejectsDuplicateAliasIdsDuringDecode) {
  const auto plugin = fo4::read_plugin(
      quest_bytes(false, true), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

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

TEST(QuestAliasReplacementPreservation, KeepsMembersTheCreationDoesNotWrite) {
  auto plugin = fo4::read_plugin(unmodelled_alias_member_quest(), "Quest.esp");
  const auto* before = fo4::find_record(plugin, k_quest);
  ASSERT_NE(before, nullptr);
  const auto before_reference = alias_members(*before, "ALST");
  const auto before_location = alias_members(*before, "ALLS");

  auto reference = reference_alias(1);
  reference.name = std::string_view{"RefOne"};
  reference.flags = 0x10u;
  reference.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceNearAlias{.alias = 6, .type = 1}};
  fo4::forms::replace_quest_alias(plugin, k_quest, 1, reference);

  auto location = location_alias(2);
  location.fill = fo4::forms::QuestFindMatchingLocationFill{.event = 1, .data = 2};
  fo4::forms::replace_quest_alias(plugin, k_quest, 2, location);

  const auto* record = fo4::find_record(plugin, k_quest);
  ASSERT_NE(record, nullptr);

  // ALFI must stay ahead of the written fill in the reference alias ordering.
  const auto after_reference = alias_members(*record, "ALST");
  EXPECT_EQ(member_names(after_reference),
            (std::vector<std::string>{"ALST", "ALID", "FNAM", "ALFI", "ALNA",
                                      "ALNT", "ALED"}));
  EXPECT_EQ(member_payload(after_reference, "ALST"), u32_bytes(1));
  EXPECT_EQ(member_payload(after_reference, "ALFI"),
            member_payload(before_reference, "ALFI"));

  // ALID, ALFI, CTDA/CIS1 and ALCC must survive; the ALEA fill must not, because a Fill Type union is one run.
  const auto after_location = alias_members(*record, "ALLS");
  EXPECT_EQ(member_names(after_location),
            (std::vector<std::string>{"ALLS", "ALID", "FNAM", "ALFI", "ALFE",
                                      "ALFD", "CTDA", "CIS1", "ALCC", "ALED"}));
  EXPECT_EQ(member_payload(after_location, "ALLS"), u32_bytes(2));
  EXPECT_EQ(member_payload(after_location, "ALID"),
            member_payload(before_location, "ALID"));
  EXPECT_EQ(member_payload(after_location, "ALFI"),
            member_payload(before_location, "ALFI"));
  EXPECT_EQ(member_payload(after_location, "CTDA"),
            member_payload(before_location, "CTDA"));
  EXPECT_EQ(member_payload(after_location, "CIS1"),
            member_payload(before_location, "CIS1"));
  EXPECT_EQ(member_payload(after_location, "ALCC"),
            member_payload(before_location, "ALCC"));

  const auto decoded = fo4::forms::decode_quest_aliases(plugin, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 2u);
  EXPECT_EQ(decoded.aliases[0].id, 1u);
  EXPECT_EQ(decoded.aliases[0].force_into_alias, std::optional<std::int32_t>{-3});
  EXPECT_EQ(decoded.aliases[1].id, 2u);
  EXPECT_EQ(decoded.aliases[1].closest_to_alias, std::optional<std::int32_t>{4});
  EXPECT_NE(std::get_if<fo4::forms::QuestFindMatchingLocationFill>(
                &decoded.aliases[1].fill),
            nullptr);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Quest.esp");
  EXPECT_EQ(fo4::write_plugin(reread), bytes);
  const auto round = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(round.aliases.size(), 2u);
  const auto* round_record = fo4::find_record(reread, k_quest);
  ASSERT_NE(round_record, nullptr);
  EXPECT_EQ(alias_members(*round_record, "ALLS"), after_location);
}

TEST(QuestAliasReplacementPreservation, ReplacesAWrittenConditionRunAsAUnit) {
  auto plugin = fo4::read_plugin(unmodelled_alias_member_quest(), "Quest.esp");
  const auto* before = fo4::find_record(plugin, k_quest);
  ASSERT_NE(before, nullptr);
  const auto before_location = alias_members(*before, "ALLS");

  auto location = location_alias(2);
  fo4::CtdaCondition condition;
  condition.function = 1;
  location.conditions.push_back(condition);
  fo4::forms::replace_quest_alias(plugin, k_quest, 2, location);

  const auto* record = fo4::find_record(plugin, k_quest);
  ASSERT_NE(record, nullptr);
  const auto after_location = alias_members(*record, "ALLS");
  // A written condition run must replace CTDA and CIS1; ALEA and ALCC must stay.
  EXPECT_EQ(member_names(after_location),
            (std::vector<std::string>{"ALLS", "ALID", "FNAM", "ALFI", "ALEA",
                                      "CTDA", "ALCC", "ALED"}));
  EXPECT_EQ(member_payload(after_location, "ALEA"),
            member_payload(before_location, "ALEA"));
  EXPECT_EQ(member_payload(after_location, "ALCC"),
            member_payload(before_location, "ALCC"));
  EXPECT_NE(member_payload(after_location, "CTDA"),
            member_payload(before_location, "CTDA"));
}

TEST(QuestAliasReplacementPreservation, RefusesPreservedMembersThatCannotBeOrdered) {
  auto misplaced = fo4::read_plugin(reference_alias_with_location_member(), "Quest.esp");
  const auto misplaced_before = fo4::write_plugin(misplaced);
  EXPECT_THROW(
      fo4::forms::replace_quest_alias(misplaced, k_quest, 1, reference_alias(1)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(misplaced), misplaced_before);

  auto unordered = fo4::read_plugin(location_alias_out_of_member_order(), "Quest.esp");
  const auto unordered_before = fo4::write_plugin(unordered);
  EXPECT_THROW(
      fo4::forms::replace_quest_alias(unordered, k_quest, 2, location_alias(2)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(unordered), unordered_before);
}
