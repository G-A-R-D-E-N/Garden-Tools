#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace {

void write_u32_subrecord(fo4::test::ByteWriter& out,
                         std::string_view signature,
                         std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> empty_quest_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
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
  write_u32_subrecord(out, "ANAM", 1);
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
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
  creation.flags = 0u;
  return creation;
}

std::uint32_t raw_u32(const fo4::Subrecord& subrecord) {
  EXPECT_EQ(subrecord.data.size(), 4u);
  if (subrecord.data.size() != 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

std::int16_t raw_i16(const fo4::Subrecord& subrecord, std::size_t offset) {
  EXPECT_GE(subrecord.data.size(), offset + 2);
  if (subrecord.data.size() < offset + 2) return 0;
  std::int16_t value = 0;
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
  return value;
}

std::uint16_t raw_u16(const fo4::Subrecord& subrecord, std::size_t offset) {
  EXPECT_GE(subrecord.data.size(), offset + 2);
  if (subrecord.data.size() < offset + 2) return 0;
  std::uint16_t value = 0;
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
  return value;
}

fo4::Subrecord u32_subrecord(std::string_view signature, std::uint32_t value) {
  fo4::Subrecord result;
  result.signature = fo4::Signature::from_string(signature);
  result.data.resize(4);
  std::memcpy(result.data.data(), &value, sizeof(value));
  return result;
}

}

TEST(QuestAliasRemainingFills, CreateReferenceRoundTripsMasterObjectAndALCA) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  const auto quest = fo4::FormKey::parse("000800:Patch.esp");

  auto creation = reference_alias(1);
  creation.fill = fo4::forms::QuestCreateReferenceFill{
      .object = fo4::FormKey::parse("000123:Master.esm"),
      .alias = -2,
      .create_mode = 0x8000u,
      .level = 2,
  };
  fo4::forms::add_quest_alias(plugin, quest, creation);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(reread, quest);
  ASSERT_EQ(aliases.aliases.size(), 1u);
  const auto* fill = std::get_if<fo4::forms::QuestCreateReferenceFill>(
      &aliases.aliases[0].fill);
  ASSERT_NE(fill, nullptr);
  EXPECT_EQ(fill->object, std::optional<fo4::FormKey>{
                              fo4::FormKey::parse("000123:Master.esm")});
  EXPECT_EQ(fill->alias, -2);
  EXPECT_EQ(fill->create_mode, 0x8000u);
  EXPECT_EQ(fill->level, 2u);

  const auto& raw = aliases.aliases[0].subrecords;
  ASSERT_EQ(raw.size(), 7u);
  EXPECT_EQ(raw[3].signature.to_string(), "ALCO");
  EXPECT_EQ(raw_u32(raw[3]), 0x00000123u);
  EXPECT_EQ(raw[4].signature.to_string(), "ALCA");
  EXPECT_EQ(raw_i16(raw[4], 0), -2);
  EXPECT_EQ(raw_u16(raw[4], 2), 0x8000u);
  EXPECT_EQ(raw[5].signature.to_string(), "ALCL");
  EXPECT_EQ(raw_u32(raw[5]), 2u);
  EXPECT_EQ(raw[6].signature.to_string(), "ALED");
}

TEST(QuestAliasRemainingFills, CreateReferenceRoundTripsTheNoneLevel) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  const auto quest = fo4::FormKey::parse("000800:Patch.esp");
  auto creation = reference_alias(1);
  creation.fill = fo4::forms::QuestCreateReferenceFill{
      .object = std::nullopt,
      .alias = 0,
      .create_mode = 0,
      .level = 4,
  };
  fo4::forms::add_quest_alias(plugin, quest, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto aliases = fo4::forms::decode_quest_aliases(reread, quest);
  ASSERT_EQ(aliases.aliases.size(), 1u);
  const auto* fill = std::get_if<fo4::forms::QuestCreateReferenceFill>(&aliases.aliases[0].fill);
  ASSERT_NE(fill, nullptr);
  EXPECT_EQ(fill->level, 4u);
}

TEST(QuestAliasRemainingFills, MatchingReferenceSelectorsRoundTripDistinctly) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  const auto quest = fo4::FormKey::parse("000800:Patch.esp");

  auto near = reference_alias(1);
  near.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceNearAlias{.alias = -3, .type = 1}};
  fo4::forms::add_quest_alias(plugin, quest, near);

  auto event = reference_alias(2);
  event.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceEvent{.event = 17, .data = 4}};
  fo4::forms::add_quest_alias(plugin, quest, event);

  auto closest = reference_alias(3);
  closest.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceClosest{.alias = 1}};
  fo4::forms::add_quest_alias(plugin, quest, closest);

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  ASSERT_EQ(aliases.aliases.size(), 3u);

  const auto* near_fill = std::get_if<fo4::forms::QuestFindMatchingReferenceFill>(
      &aliases.aliases[0].fill);
  ASSERT_NE(near_fill, nullptr);
  const auto* near_selector =
      std::get_if<fo4::forms::QuestMatchingReferenceNearAlias>(&near_fill->selector);
  ASSERT_NE(near_selector, nullptr);
  EXPECT_EQ(near_selector->alias, -3);
  EXPECT_EQ(near_selector->type, 1u);

  const auto* event_fill = std::get_if<fo4::forms::QuestFindMatchingReferenceFill>(
      &aliases.aliases[1].fill);
  ASSERT_NE(event_fill, nullptr);
  const auto* event_selector =
      std::get_if<fo4::forms::QuestMatchingReferenceEvent>(&event_fill->selector);
  ASSERT_NE(event_selector, nullptr);
  EXPECT_EQ(event_selector->event, 17u);
  EXPECT_EQ(event_selector->data, 4u);

  const auto* closest_fill = std::get_if<fo4::forms::QuestFindMatchingReferenceFill>(
      &aliases.aliases[2].fill);
  ASSERT_NE(closest_fill, nullptr);
  const auto* closest_selector =
      std::get_if<fo4::forms::QuestMatchingReferenceClosest>(&closest_fill->selector);
  ASSERT_NE(closest_selector, nullptr);
  EXPECT_EQ(closest_selector->alias, 1);
}

TEST(QuestAliasRemainingFills, MatchingLocationAndPostClosestAliasRoundTripInOrder) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  const auto quest = fo4::FormKey::parse("000800:Patch.esp");

  auto creation = location_alias(1);
  creation.fill = fo4::forms::QuestFindMatchingLocationFill{.event = 9, .data = 2};
  creation.closest_to_alias = -5;
  fo4::forms::add_quest_alias(plugin, quest, creation);

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  ASSERT_EQ(aliases.aliases.size(), 1u);
  const auto* fill = std::get_if<fo4::forms::QuestFindMatchingLocationFill>(
      &aliases.aliases[0].fill);
  ASSERT_NE(fill, nullptr);
  EXPECT_EQ(fill->event, 9u);
  EXPECT_EQ(fill->data, 2u);
  EXPECT_EQ(aliases.aliases[0].closest_to_alias,
            std::optional<std::int32_t>{-5});

  const auto& raw = aliases.aliases[0].subrecords;
  ASSERT_EQ(raw.size(), 6u);
  EXPECT_EQ(raw[2].signature.to_string(), "ALFE");
  EXPECT_EQ(raw[3].signature.to_string(), "ALFD");
  EXPECT_EQ(raw[4].signature.to_string(), "ALCC");
  EXPECT_EQ(raw[5].signature.to_string(), "ALED");
}

TEST(QuestAliasRemainingFills, RejectsInvalidAuthoredEnumsAndMixedMatchingSelector) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");

  auto create = reference_alias(1);
  create.fill = fo4::forms::QuestCreateReferenceFill{
      .object = std::nullopt,
      .alias = 0,
      .create_mode = 7,
      .level = 0,
  };
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, create)),
      fo4::forms::QuestError);
  create.fill = fo4::forms::QuestCreateReferenceFill{
      .object = std::nullopt,
      .alias = 0,
      .create_mode = 0,
      .level = 5,
  };
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, create)),
      fo4::forms::QuestError);

  auto near = reference_alias(1);
  near.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceNearAlias{.alias = 0, .type = 2}};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, near)),
      fo4::forms::QuestError);

  const auto quest = fo4::FormKey::parse("000800:Patch.esp");
  auto valid = reference_alias(1);
  valid.fill = fo4::forms::QuestFindMatchingReferenceFill{
      fo4::forms::QuestMatchingReferenceNearAlias{.alias = 0, .type = 0}};
  fo4::forms::add_quest_alias(plugin, quest, valid);

  auto* record = const_cast<fo4::Record*>(fo4::find_record(plugin, quest));
  ASSERT_NE(record, nullptr);
  const auto aled = std::find_if(
      record->subrecords.begin(), record->subrecords.end(), [](const auto& subrecord) {
        return subrecord.signature == fo4::Signature::from_string("ALED");
      });
  ASSERT_NE(aled, record->subrecords.end());
  record->subrecords.insert(aled, u32_subrecord("ALFE", 3));
  record->subrecords.insert(
      std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& subrecord) {
        return subrecord.signature == fo4::Signature::from_string("ALED");
      }),
      u32_subrecord("ALFD", 4));

  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(plugin, quest)),
      fo4::forms::QuestError);
}
