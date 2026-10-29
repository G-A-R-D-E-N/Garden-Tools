#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

std::vector<std::byte> quest_with_tail() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
  write_u32_subrecord(out, "ANAM", 2);

  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
  write_u32_subrecord(out, "FNAM", 0x10);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  fo4::test::write_subrecord(out, "NNAM", std::string_view{"TailDescription\0", 16});

  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* unique_subrecord(const fo4::Record& record,
                                       std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  const fo4::Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    EXPECT_EQ(found, nullptr);
    found = &subrecord;
  }
  return found;
}

std::uint32_t raw_u32(const fo4::Subrecord& subrecord) {
  EXPECT_EQ(subrecord.data.size(), 4u);
  if (subrecord.data.size() != 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

std::size_t signature_index(const fo4::Record& record,
                            std::string_view signature,
                            std::size_t ordinal = 0) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature != sig) continue;
    if (seen++ == ordinal) return index;
  }
  ADD_FAILURE() << "missing requested subrecord";
  return record.subrecords.size();
}

fo4::forms::QuestAliasCreation collection(std::uint32_t id, std::uint8_t max_fill) {
  return {
      .kind = fo4::forms::QuestAliasKind::collection,
      .id = id,
      .name = std::nullopt,
      .flags = std::nullopt,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = max_fill,
      .closest_to_alias = std::nullopt,
      .fill = std::monostate{},
      .keywords = {},
      .items = {},
      .linked_aliases = {},
      .spells = {},
      .factions = {},
      .packages = {},
      .spectator_override = {},
      .observe_dead_body_override = {},
      .guard_warn_override = {},
      .combat_override = {},
      .display_name = {},
      .forced_voice = {},
      .death_item = {},
      .voice_types = {},
      .conditions = {},
      .collection_entries = {},
  };
}

fo4::forms::QuestAliasCreation reference_alias(std::uint32_t id) {
  return {
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = id,
      .name = std::string_view{"Ref"},
      .flags = 0x10u,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = std::nullopt,
      .closest_to_alias = std::nullopt,
      .fill = std::monostate{},
      .keywords = {},
      .items = {},
      .linked_aliases = {},
      .spells = {},
      .factions = {},
      .packages = {},
      .spectator_override = {},
      .observe_dead_body_override = {},
      .guard_warn_override = {},
      .combat_override = {},
      .display_name = {},
      .forced_voice = {},
      .death_item = {},
      .voice_types = {},
      .conditions = {},
      .collection_entries = {},
  };
}

fo4::forms::QuestAliasCreation location_alias(std::uint32_t id) {
  fo4::forms::QuestAliasCreation creation;
  creation.kind = fo4::forms::QuestAliasKind::location;
  creation.id = id;
  creation.flags = 0x20u;
  return creation;
}

using MemberList = std::vector<std::pair<std::string, std::vector<std::byte>>>;

MemberList alias_members(const fo4::Record& record, std::string_view start,
                         std::size_t ordinal = 0) {
  const auto start_signature = fo4::Signature::from_string(start);
  MemberList members;
  std::size_t seen = 0;
  bool inside = false;
  for (const auto& subrecord : record.subrecords) {
    if (!inside) {
      if (subrecord.signature != start_signature) continue;
      if (seen++ != ordinal) continue;
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

std::vector<std::byte> alias_with_members(std::string_view start, std::uint32_t id,
                                          const MemberList& members) {
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
  write_u32_subrecord(out, start, id);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"Only\0", 5});
  write_u32_subrecord(out, "FNAM", 0);
  for (const auto& member : members) {
    fo4::test::write_subrecord(out, member.first, member.second);
  }
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

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

TEST(QuestAliasMutation, AddsBeforeTailAndAdvancesNextAliasId) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");

  fo4::forms::QuestAliasCreation creation{
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = 5,
      .name = std::string_view{"MasterRef"},
      .flags = 0x20u,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = std::nullopt,
      .closest_to_alias = std::nullopt,
      .fill = fo4::forms::QuestSpecificReferenceFill{
          fo4::FormKey::parse("000123:Master.esm")},
      .keywords = {},
      .items = {},
      .linked_aliases = {},
      .spells = {},
      .factions = {},
      .packages = {},
      .spectator_override = {},
      .observe_dead_body_override = {},
      .guard_warn_override = {},
      .combat_override = {},
      .display_name = {},
      .forced_voice = {},
      .death_item = {},
      .voice_types = {},
      .conditions = {},
      .collection_entries = {},
  };
  fo4::forms::add_quest_alias(plugin, quest, creation);

  const auto* record = fo4::find_record(plugin, quest);
  ASSERT_NE(record, nullptr);
  const auto* anam = unique_subrecord(*record, "ANAM");
  ASSERT_NE(anam, nullptr);
  EXPECT_EQ(raw_u32(*anam), 6u);
  EXPECT_LT(signature_index(*record, "ALST", 1), signature_index(*record, "NNAM"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Quest.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(reread, quest);
  ASSERT_EQ(aliases.aliases.size(), 2u);
  EXPECT_EQ(aliases.aliases[1].id, 5u);
  const auto* fill =
      std::get_if<fo4::forms::QuestSpecificReferenceFill>(&aliases.aliases[1].fill);
  ASSERT_NE(fill, nullptr);
  ASSERT_TRUE(fill->reference.has_value());
  EXPECT_EQ(*fill->reference, fo4::FormKey::parse("000123:Master.esm"));

  const auto* round_record = fo4::find_record(reread, quest);
  ASSERT_NE(round_record, nullptr);
  EXPECT_LT(signature_index(*round_record, "ALED", 1),
            signature_index(*round_record, "NNAM"));
}

TEST(QuestAliasMutation, ReplacesAliasInPlaceWithoutTouchingTailOrAnam) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto* before_record = fo4::find_record(plugin, quest);
  ASSERT_NE(before_record, nullptr);
  const auto* before_nnam = unique_subrecord(*before_record, "NNAM");
  ASSERT_NE(before_nnam, nullptr);
  const auto tail_bytes = before_nnam->data;

  fo4::forms::replace_quest_alias(plugin, quest, 1, collection(1, 4));

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  ASSERT_EQ(aliases.aliases.size(), 1u);
  EXPECT_EQ(aliases.aliases[0].kind, fo4::forms::QuestAliasKind::collection);
  ASSERT_EQ(aliases.aliases[0].collection_entries.size(), 1u);
  EXPECT_EQ(aliases.aliases[0].collection_entries[0].id, 1u);
  EXPECT_EQ(aliases.aliases[0].collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{4});
  EXPECT_EQ(aliases.next_alias_id, 2u);

  const auto* record = fo4::find_record(plugin, quest);
  ASSERT_NE(record, nullptr);
  const auto* nnam = unique_subrecord(*record, "NNAM");
  ASSERT_NE(nnam, nullptr);
  EXPECT_EQ(nnam->data, tail_bytes);
  EXPECT_LT(signature_index(*record, "ALCS"), signature_index(*record, "NNAM"));
}

TEST(QuestAliasMutation, RemovesAliasWithoutRecyclingNextAliasId) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  fo4::forms::add_quest_alias(plugin, quest, reference_alias(5));
  ASSERT_TRUE(fo4::forms::remove_quest_alias(plugin, quest, 1));

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  ASSERT_EQ(aliases.aliases.size(), 1u);
  EXPECT_EQ(aliases.aliases[0].id, 5u);
  EXPECT_EQ(aliases.next_alias_id, 6u);

  const auto before = fo4::write_plugin(plugin);
  EXPECT_FALSE(fo4::forms::remove_quest_alias(plugin, quest, 99));
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasMutation, RejectsDuplicateIdsAndIdChangingReplacementTransactionally) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(plugin, quest, reference_alias(1)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(
      fo4::forms::replace_quest_alias(plugin, quest, 1, collection(2, 2)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasMutation, RejectsAliasIdThatCannotAdvanceAnam) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(
          plugin, quest, reference_alias(std::numeric_limits<std::uint32_t>::max())),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasMutation, WriteReplacesWholeArrayIncludingCollectionsAndPreservesTail) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto* before_record = fo4::find_record(plugin, quest);
  ASSERT_NE(before_record, nullptr);
  const auto* before_nnam = unique_subrecord(*before_record, "NNAM");
  ASSERT_NE(before_nnam, nullptr);
  const auto tail_bytes = before_nnam->data;

  const fo4::forms::QuestAliasCreation location{
      .kind = fo4::forms::QuestAliasKind::location,
      .id = 4,
      .name = std::nullopt,
      .flags = 0x40u,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = std::nullopt,
      .closest_to_alias = std::nullopt,
      .fill = std::monostate{},
      .keywords = {},
      .items = {},
      .linked_aliases = {},
      .spells = {},
      .factions = {},
      .packages = {},
      .spectator_override = {},
      .observe_dead_body_override = {},
      .guard_warn_override = {},
      .combat_override = {},
      .display_name = {},
      .forced_voice = {},
      .death_item = {},
      .voice_types = {},
      .conditions = {},
      .collection_entries = {},
  };
  const std::array<fo4::forms::QuestAliasCreation, 2> creations{
      location,
      collection(9, 2),
  };
  fo4::forms::write_quest_aliases(plugin, quest, creations);

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  ASSERT_EQ(aliases.aliases.size(), 2u);
  EXPECT_EQ(aliases.aliases[0].kind, fo4::forms::QuestAliasKind::location);
  EXPECT_EQ(aliases.aliases[0].id, 4u);
  EXPECT_EQ(aliases.aliases[1].kind, fo4::forms::QuestAliasKind::collection);
  EXPECT_EQ(aliases.next_alias_id, 5u);

  const auto* record = fo4::find_record(plugin, quest);
  ASSERT_NE(record, nullptr);
  const auto* nnam = unique_subrecord(*record, "NNAM");
  ASSERT_NE(nnam, nullptr);
  EXPECT_EQ(nnam->data, tail_bytes);
  EXPECT_LT(signature_index(*record, "ALED"), signature_index(*record, "NNAM"));
}

TEST(QuestAliasMutation, WriteEmptyArrayClearsAliasesAndKeepsAnam) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  fo4::forms::write_quest_aliases(plugin, quest, {});

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  EXPECT_TRUE(aliases.aliases.empty());
  EXPECT_EQ(aliases.next_alias_id, 2u);
  EXPECT_NE(unique_subrecord(*fo4::find_record(plugin, quest), "NNAM"), nullptr);
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

  const auto after_reference = alias_members(*record, "ALST");
  EXPECT_EQ(member_names(after_reference),
            (std::vector<std::string>{"ALST", "ALID", "FNAM", "ALFI", "ALNA",
                                      "ALNT", "ALED"}));
  EXPECT_EQ(member_payload(after_reference, "ALST"), u32_bytes(1));
  EXPECT_EQ(member_payload(after_reference, "ALFI"),
            member_payload(before_reference, "ALFI"));

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
  EXPECT_EQ(member_names(after_location),
            (std::vector<std::string>{"ALLS", "ALID", "FNAM", "ALFI", "ALEA",
                                      "CTDA", "ALCC", "ALED"}));
  EXPECT_EQ(member_payload(after_location, "ALEA"),
            member_payload(before_location, "ALEA"));
  EXPECT_EQ(member_payload(after_location, "ALCC"),
            member_payload(before_location, "ALCC"));
  EXPECT_EQ(member_payload(after_location, "CTDA"),
            fo4::serialize_ctda_condition(condition));
}

TEST(QuestAliasReplacementPreservation, DropsLocationClosestToWhenAReferenceFillIsWritten) {
  auto plugin = fo4::read_plugin(unmodelled_alias_member_quest(), "Quest.esp");
  const auto* before = fo4::find_record(plugin, k_quest);
  ASSERT_NE(before, nullptr);
  const auto before_location = alias_members(*before, "ALLS");

  auto reference = reference_alias(2);
  reference.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceNearAlias{.alias = 6, .type = 1}};
  fo4::forms::replace_quest_alias(plugin, k_quest, 2, reference);

  const auto* record = fo4::find_record(plugin, k_quest);
  ASSERT_NE(record, nullptr);
  const auto after = alias_members(*record, "ALST", 1);
  EXPECT_EQ(member_names(after),
            (std::vector<std::string>{"ALST", "ALID", "FNAM", "ALNA", "ALNT",
                                      "CTDA", "CIS1", "ALED"}));
  EXPECT_EQ(member_payload(after, "CTDA"), member_payload(before_location, "CTDA"));
  EXPECT_EQ(member_payload(after, "CIS1"), member_payload(before_location, "CIS1"));

  const auto decoded = fo4::forms::decode_quest_aliases(plugin, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 2u);
  EXPECT_EQ(decoded.aliases[1].kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(decoded.aliases[1].id, 2u);
  EXPECT_NE(std::get_if<fo4::forms::QuestFindMatchingReferenceFill>(
                &decoded.aliases[1].fill),
            nullptr);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Quest.esp");
  EXPECT_EQ(fo4::write_plugin(reread), bytes);
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

TEST(QuestAliasReplacementPreservation, RefusesKindChangesOverSharedFillMembers) {
  auto plugin = fo4::read_plugin(
      alias_with_members("ALLS", 2, {{"ALCC", u32_bytes(4)}}), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::forms::replace_quest_alias(plugin, k_quest, 2, reference_alias(2)),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto other = fo4::read_plugin(
      alias_with_members("ALST", 1, {{"ALEA", u32_bytes(5)}}), "Quest.esp");
  const auto other_before = fo4::write_plugin(other);
  EXPECT_THROW(fo4::forms::replace_quest_alias(other, k_quest, 1, location_alias(1)),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(other), other_before);
}

TEST(QuestAliasReplacementPreservation, RefusesOrDropsASpecificReferenceOnAKindChange) {
  auto plugin = fo4::read_plugin(
      alias_with_members("ALST", 1, {{"ALFR", u32_bytes(0x800)}}), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::forms::replace_quest_alias(plugin, k_quest, 1, location_alias(1)),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto location = location_alias(1);
  location.fill = fo4::forms::QuestFindMatchingLocationFill{.event = 1, .data = 2};
  fo4::forms::replace_quest_alias(plugin, k_quest, 1, location);

  const auto* record = fo4::find_record(plugin, k_quest);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(member_names(alias_members(*record, "ALLS")),
            (std::vector<std::string>{"ALLS", "FNAM", "ALFE", "ALFD", "ALED"}));
}

TEST(QuestAliasReplacementPreservation, RefusesAKindChangeThatWouldCarryVoiceTypes) {
  auto plugin = fo4::read_plugin(
      alias_with_members("ALST", 1, {{"VTCK", u32_bytes(0x800)}}), "Quest.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::forms::replace_quest_alias(plugin, k_quest, 1, location_alias(1)),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasReplacementPreservation, KeepsAConditionRunWithACis1InOrderOnARename) {
  fo4::test::ByteWriter cis1;
  cis1.raw(std::string_view{"One\0", 4});
  auto plugin = fo4::read_plugin(
      alias_with_members("ALLS", 2, {{"CTDA", ctda_payload()},
                                     {"CIS1", cis1.bytes},
                                     {"CTDA", ctda_payload()}}),
      "Quest.esp");
  auto renamed = location_alias(2);
  renamed.name = std::string_view{"Renamed"};
  fo4::forms::replace_quest_alias(plugin, k_quest, 2, renamed);

  const auto* record = fo4::find_record(plugin, k_quest);
  ASSERT_NE(record, nullptr);
  const auto after = alias_members(*record, "ALLS");
  EXPECT_EQ(member_names(after),
            (std::vector<std::string>{"ALLS", "ALID", "FNAM", "CTDA", "CIS1", "CTDA",
                                      "ALED"}));
  EXPECT_EQ(member_payload(after, "CIS1"), cis1.bytes);
}
