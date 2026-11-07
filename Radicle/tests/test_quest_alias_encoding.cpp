#include "fo4/forms/quest.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

TEST(QuestAliasEncoding, BuildsReferenceAliasInProvenOrder) {
  const auto alias = fo4::forms::encode_quest_alias({
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = 7,
      .name = std::string_view{"ReferenceSeven"},
      .flags = 0x12u,
      .force_into_alias = -1,
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
  });

  EXPECT_EQ(alias.kind, fo4::forms::QuestAliasKind::reference);
  EXPECT_EQ(alias.id, 7u);
  EXPECT_EQ(alias.name, std::optional<std::string>{"ReferenceSeven"});
  EXPECT_EQ(alias.flags, std::optional<std::uint32_t>{0x12u});
  EXPECT_EQ(alias.force_into_alias, std::optional<std::int32_t>{-1});
  ASSERT_EQ(alias.subrecords.size(), 5u);
  EXPECT_EQ(alias.subrecords[0].signature.to_string(), "ALST");
  EXPECT_EQ(alias.subrecords[1].signature.to_string(), "ALID");
  EXPECT_EQ(alias.subrecords[2].signature.to_string(), "FNAM");
  EXPECT_EQ(alias.subrecords[3].signature.to_string(), "ALFI");
  EXPECT_EQ(alias.subrecords[4].signature.to_string(), "ALED");
  EXPECT_TRUE(alias.subrecords.back().data.empty());
}

TEST(QuestAliasEncoding, InsertsConditionsBeforeAled) {
  fo4::forms::QuestAliasCreation creation{
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = 7,
      .name = std::string_view{"ReferenceSeven"},
      .flags = 0x12u,
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
  fo4::CtdaCondition condition;
  condition.function = 675;
  condition.parameter1_string = "MyVar";
  creation.conditions.push_back(std::move(condition));

  const auto alias = fo4::forms::encode_quest_alias(creation);
  ASSERT_EQ(alias.subrecords.size(), 6u);
  EXPECT_EQ(alias.subrecords[0].signature.to_string(), "ALST");
  EXPECT_EQ(alias.subrecords[1].signature.to_string(), "ALID");
  EXPECT_EQ(alias.subrecords[2].signature.to_string(), "FNAM");
  EXPECT_EQ(alias.subrecords[3].signature.to_string(), "CTDA");
  EXPECT_EQ(alias.subrecords[4].signature.to_string(), "CIS1");
  EXPECT_EQ(alias.subrecords[5].signature.to_string(), "ALED");
}

TEST(QuestAliasEncoding, BuildsLocationAliasWithoutInventingName) {
  const auto alias = fo4::forms::encode_quest_alias({
      .kind = fo4::forms::QuestAliasKind::location,
      .id = 8,
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
  });

  EXPECT_EQ(alias.kind, fo4::forms::QuestAliasKind::location);
  EXPECT_FALSE(alias.name.has_value());
  EXPECT_EQ(alias.flags, std::optional<std::uint32_t>{0x40u});
  ASSERT_EQ(alias.subrecords.size(), 3u);
  EXPECT_EQ(alias.subrecords[0].signature.to_string(), "ALLS");
  EXPECT_EQ(alias.subrecords[1].signature.to_string(), "FNAM");
  EXPECT_EQ(alias.subrecords[2].signature.to_string(), "ALED");
}

TEST(QuestAliasEncoding, BuildsCompactCollectionAlias) {
  const auto alias = fo4::forms::encode_quest_alias({
      .kind = fo4::forms::QuestAliasKind::collection,
      .id = 9,
      .name = std::nullopt,
      .flags = std::nullopt,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = std::uint8_t{6},
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
  });

  EXPECT_EQ(alias.kind, fo4::forms::QuestAliasKind::collection);
  ASSERT_EQ(alias.collection_entries.size(), 1u);
  EXPECT_EQ(alias.collection_entries[0].id, 9u);
  EXPECT_EQ(alias.collection_entries[0].max_initial_fill_count,
            std::optional<std::uint8_t>{6});
  ASSERT_EQ(alias.subrecords.size(), 2u);
  EXPECT_EQ(alias.subrecords[0].signature.to_string(), "ALCS");
  EXPECT_EQ(alias.subrecords[1].signature.to_string(), "ALMI");
  ASSERT_EQ(alias.subrecords[1].data.size(), 1u);
  EXPECT_EQ(alias.subrecords[1].data[0], std::byte{6});
}

TEST(QuestAliasEncoding, RejectsImpossibleFieldCombinations) {
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias({
          .kind = fo4::forms::QuestAliasKind::reference,
          .id = 1,
          .name = std::nullopt,
          .flags = 0u,
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
      })),
      fo4::forms::QuestError);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias({
          .kind = fo4::forms::QuestAliasKind::location,
          .id = 1,
          .name = std::nullopt,
          .flags = std::nullopt,
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
      })),
      fo4::forms::QuestError);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias({
          .kind = fo4::forms::QuestAliasKind::collection,
          .id = 1,
          .name = std::nullopt,
          .flags = 0u,
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
      })),
      fo4::forms::QuestError);

  const std::string embedded{"Bad\0Name", 8};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias({
          .kind = fo4::forms::QuestAliasKind::reference,
          .id = 1,
          .name = std::string_view{embedded},
          .flags = 0u,
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
      })),
      fo4::forms::QuestError);
}
