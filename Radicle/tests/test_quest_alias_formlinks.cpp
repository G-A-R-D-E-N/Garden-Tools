#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
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

fo4::forms::QuestAliasCreation base_reference() {
  return {
      .kind = fo4::forms::QuestAliasKind::reference,
      .id = 7,
      .name = std::string_view{"RefSeven"},
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
}

}

TEST(QuestAliasFormLinks, EmitsRemainingMembersInMutagenOrder) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_reference();
  creation.keywords = {fo4::FormKey::parse("000111:Master.esm")};
  creation.spectator_override = fo4::FormKey::parse("000122:Master.esm");
  creation.linked_aliases = {{fo4::FormKey::parse("000111:Master.esm"), 4}};
  creation.display_name = fo4::FormKey::parse("000133:Master.esm");
  creation.spells = {fo4::FormKey::parse("000144:Master.esm")};
  creation.voice_types = fo4::FormKey::parse("000155:Master.esm");

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  std::vector<std::string> names;
  for (const auto& sub : alias.subrecords) names.push_back(sub.signature.to_string());
  const std::vector<std::string> expected{
      "ALST", "ALID", "FNAM", "KSIZ", "KWDA", "SPOR", "ALLA", "ALDN", "ALSP", "VTCK",
      "ALED"};
  EXPECT_EQ(names, expected);
  ASSERT_EQ(alias.keywords.size(), 1u);
  EXPECT_EQ(alias.keywords[0], fo4::FormKey::parse("000111:Master.esm"));
  ASSERT_TRUE(alias.spectator_override.has_value());
  EXPECT_EQ(*alias.spectator_override, fo4::FormKey::parse("000122:Master.esm"));
  ASSERT_EQ(alias.linked_aliases.size(), 1u);
  EXPECT_EQ(alias.linked_aliases[0].keyword, fo4::FormKey::parse("000111:Master.esm"));
  EXPECT_EQ(alias.linked_aliases[0].alias, 4);
  ASSERT_TRUE(alias.display_name.has_value());
  EXPECT_EQ(*alias.display_name, fo4::FormKey::parse("000133:Master.esm"));
  ASSERT_EQ(alias.spells.size(), 1u);
  EXPECT_EQ(alias.spells[0], fo4::FormKey::parse("000144:Master.esm"));
  ASSERT_TRUE(alias.voice_types.has_value());
  EXPECT_EQ(*alias.voice_types, fo4::FormKey::parse("000155:Master.esm"));
}

TEST(QuestAliasFormLinks, EncodesSpecificMasterReferenceWithPluginMasterIndex) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_reference();
  creation.fill = fo4::forms::QuestSpecificReferenceFill{
      fo4::FormKey::parse("000123:Master.esm")};

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  const auto* alfr = member(alias, "ALFR");
  ASSERT_NE(alfr, nullptr);
  EXPECT_EQ(raw_u32(*alfr), 0x00000123u);

  const auto* decoded =
      std::get_if<fo4::forms::QuestSpecificReferenceFill>(&alias.fill);
  ASSERT_NE(decoded, nullptr);
  ASSERT_TRUE(decoded->reference.has_value());
  EXPECT_EQ(*decoded->reference, fo4::FormKey::parse("000123:Master.esm"));
}

TEST(QuestAliasFormLinks, EncodesUniqueActorOwnedByCurrentPlugin) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  auto creation = base_reference();
  creation.fill = fo4::forms::QuestUniqueActorFill{
      fo4::FormKey::parse("000456:Patch.esp")};

  const auto alias = fo4::forms::encode_quest_alias(plugin, creation);
  const auto* alua = member(alias, "ALUA");
  ASSERT_NE(alua, nullptr);
  EXPECT_EQ(raw_u32(*alua), 0x01000456u);

  const auto* decoded = std::get_if<fo4::forms::QuestUniqueActorFill>(&alias.fill);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->actor, fo4::FormKey::parse("000456:Patch.esp"));
}

TEST(QuestAliasFormLinks, EncodesLocationAliasReferenceAndExternalAlias) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");

  auto location_creation = base_reference();
  location_creation.fill = fo4::forms::QuestLocationAliasReferenceFill{
      .alias = -2,
      .reference_type = fo4::FormKey::parse("000555:Master.esm"),
  };
  const auto location = fo4::forms::encode_quest_alias(plugin, location_creation);
  const auto* alfa = member(location, "ALFA");
  const auto* alrt = member(location, "ALRT");
  ASSERT_NE(alfa, nullptr);
  ASSERT_NE(alrt, nullptr);
  EXPECT_EQ(raw_u32(*alfa), 0xFFFFFFFEu);
  EXPECT_EQ(raw_u32(*alrt), 0x00000555u);
  const auto* decoded_location =
      std::get_if<fo4::forms::QuestLocationAliasReferenceFill>(&location.fill);
  ASSERT_NE(decoded_location, nullptr);
  EXPECT_EQ(decoded_location->alias, -2);
  EXPECT_EQ(decoded_location->reference_type,
            fo4::FormKey::parse("000555:Master.esm"));

  auto external_creation = base_reference();
  external_creation.fill = fo4::forms::QuestExternalAliasReferenceFill{
      .quest = fo4::FormKey::parse("000666:Patch.esp"),
      .alias = 3,
  };
  const auto external = fo4::forms::encode_quest_alias(plugin, external_creation);
  const auto* aleq = member(external, "ALEQ");
  const auto* alea = member(external, "ALEA");
  ASSERT_NE(aleq, nullptr);
  ASSERT_NE(alea, nullptr);
  EXPECT_EQ(raw_u32(*aleq), 0x01000666u);
  EXPECT_EQ(raw_u32(*alea), 3u);
}

TEST(QuestAliasFormLinks, SupportsNullableSpecificAndExternalQuestFields) {
  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");

  auto specific_creation = base_reference();
  specific_creation.fill = fo4::forms::QuestSpecificReferenceFill{std::nullopt};
  const auto specific = fo4::forms::encode_quest_alias(plugin, specific_creation);
  const auto* alfr = member(specific, "ALFR");
  ASSERT_NE(alfr, nullptr);
  EXPECT_EQ(raw_u32(*alfr), 0u);

  auto external_creation = base_reference();
  external_creation.fill = fo4::forms::QuestExternalAliasReferenceFill{
      .quest = std::nullopt,
      .alias = 4,
  };
  const auto external = fo4::forms::encode_quest_alias(plugin, external_creation);
  EXPECT_EQ(member(external, "ALEQ"), nullptr);
  const auto* alea = member(external, "ALEA");
  ASSERT_NE(alea, nullptr);
  EXPECT_EQ(raw_u32(*alea), 4u);
}

TEST(QuestAliasFormLinks, RefusesFormLinkFillWithoutPluginContextOrMaster) {
  auto creation = base_reference();
  creation.fill = fo4::forms::QuestUniqueActorFill{
      fo4::FormKey::parse("000456:Patch.esp")};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(creation)),
      fo4::forms::QuestError);

  auto plugin = fo4::read_plugin(plugin_with_master(), "Patch.esp");
  creation.fill = fo4::forms::QuestUniqueActorFill{
      fo4::FormKey::parse("000456:Missing.esm")};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::encode_quest_alias(plugin, creation)),
      fo4::forms::QuestError);
}
