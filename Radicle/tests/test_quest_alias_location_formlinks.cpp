#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> plugin_with_master() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::uint32_t raw_u32(const fo4::Subrecord& subrecord) {
  EXPECT_EQ(subrecord.data.size(), 4u);
  if (subrecord.data.size() != 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

const fo4::Subrecord* member(const fo4::forms::QuestAlias& alias,
                             std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  const fo4::Subrecord* found = nullptr;
  for (const auto& subrecord : alias.subrecords) {
    if (subrecord.signature != sig) continue;
    EXPECT_EQ(found, nullptr);
    found = &subrecord;
  }
  return found;
}

fo4::forms::QuestAliasCreation base_location() {
  return {
      .kind = fo4::forms::QuestAliasKind::location,
      .id = 8,
      .name = std::string_view{"LocationEight"},
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
}

}

TEST(QuestAliasLocationFormLinks, EncodesSpecificLocationOwnedByCurrentPlugin) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_location();
  creation.fill = fo4::forms::QuestSpecificLocationFill{
      fo4::FormKey::parse("000500:Patch.esp")};

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  const auto* alfl = member(alias, "ALFL");
  ASSERT_NE(alfl, nullptr);
  EXPECT_EQ(raw_u32(*alfl), 0x01000500u);

  const auto* decoded = std::get_if<fo4::forms::QuestSpecificLocationFill>(&alias.fill);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->location, fo4::FormKey::parse("000500:Patch.esp"));
}

TEST(QuestAliasLocationFormLinks, EncodesReferenceAliasLocationWithMasterKeyword) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_location();
  creation.fill = fo4::forms::QuestReferenceAliasLocationFill{
      .alias = -1,
      .keyword = fo4::FormKey::parse("000222:Master.esm"),
  };

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  const auto* alfa = member(alias, "ALFA");
  const auto* knam = member(alias, "KNAM");
  ASSERT_NE(alfa, nullptr);
  ASSERT_NE(knam, nullptr);
  EXPECT_EQ(raw_u32(*alfa), 0xFFFFFFFFu);
  EXPECT_EQ(raw_u32(*knam), 0x00000222u);

  const auto* decoded =
      std::get_if<fo4::forms::QuestReferenceAliasLocationFill>(&alias.fill);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->alias, -1);
  ASSERT_TRUE(decoded->keyword.has_value());
  EXPECT_EQ(*decoded->keyword, fo4::FormKey::parse("000222:Master.esm"));
}

TEST(QuestAliasLocationFormLinks, EncodesExternalLocationWithOptionalMembers) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_location();
  creation.fill = fo4::forms::QuestExternalAliasLocationFill{
      .quest = fo4::FormKey::parse("000333:Patch.esp"),
      .alias = -2,
  };

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  const auto* aleq = member(alias, "ALEQ");
  const auto* alea = member(alias, "ALEA");
  ASSERT_NE(aleq, nullptr);
  ASSERT_NE(alea, nullptr);
  EXPECT_EQ(raw_u32(*aleq), 0x01000333u);
  EXPECT_EQ(raw_u32(*alea), 0xFFFFFFFEu);

  auto quest_only = base_location();
  quest_only.fill = fo4::forms::QuestExternalAliasLocationFill{
      .quest = fo4::FormKey::parse("000444:Patch.esp"),
      .alias = std::nullopt,
  };
  const auto encoded_quest_only = fo4::forms::encode_quest_alias(plugin, quest_only);
  EXPECT_NE(member(encoded_quest_only, "ALEQ"), nullptr);
  EXPECT_EQ(member(encoded_quest_only, "ALEA"), nullptr);
}

TEST(QuestAliasLocationFormLinks, RejectsEmptyExternalAndWrongAliasKind) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto empty_external = base_location();
  empty_external.fill = fo4::forms::QuestExternalAliasLocationFill{
      .quest = std::nullopt,
      .alias = std::nullopt,
  };
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, empty_external)),
      fo4::forms::QuestError);

  fo4::forms::QuestAliasCreation reference{
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = 1,
      .name = std::string_view{"Ref"},
      .flags = 0u,
      .force_into_alias = std::nullopt,
      .max_initial_fill_count = std::nullopt,
      .closest_to_alias = std::nullopt,
      .fill = fo4::forms::QuestSpecificLocationFill{
          fo4::FormKey::parse("000500:Patch.esp")},
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
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, reference)),
      fo4::forms::QuestError);
}
