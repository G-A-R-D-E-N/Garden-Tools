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

std::vector<std::byte> quest_with_tail(bool alias_after_tail = false) {
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

  if (alias_after_tail) {
    write_u32_subrecord(out, "ALCS", 9);
    const std::vector<std::byte> max_fill{std::byte{2}};
    fo4::test::write_subrecord(out, "ALMI", max_fill);
  }

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
      .conditions = {},
  };
}

fo4::forms::QuestAliasCreation reference(std::uint32_t id) {
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
      .conditions = {},
  };
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
      .conditions = {},
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
  fo4::forms::add_quest_alias(plugin, quest, reference(5));
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
      fo4::forms::add_quest_alias(plugin, quest, reference(1)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(
      fo4::forms::replace_quest_alias(plugin, quest, 1, collection(2, 2)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasMutation, RejectsAliasMarkersAfterTailBoundary) {
  auto plugin = fo4::read_plugin(quest_with_tail(true), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(plugin, quest, collection(3, 1)),
      fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAliasMutation, RejectsAliasIdThatCannotAdvanceAnam) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::forms::add_quest_alias(
          plugin, quest, reference(std::numeric_limits<std::uint32_t>::max())),
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
      .conditions = {},
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

TEST(QuestAliasMutation, WriteEmptyArrayClearsAliasesAndResetsAnam) {
  auto plugin = fo4::read_plugin(quest_with_tail(), "Quest.esp");
  const auto quest = fo4::FormKey::parse("000800:Quest.esp");
  fo4::forms::write_quest_aliases(plugin, quest, {});

  const auto aliases = fo4::forms::decode_quest_aliases(plugin, quest);
  EXPECT_TRUE(aliases.aliases.empty());
  EXPECT_EQ(aliases.next_alias_id, 0u);
  EXPECT_NE(unique_subrecord(*fo4::find_record(plugin, quest), "NNAM"), nullptr);
}
