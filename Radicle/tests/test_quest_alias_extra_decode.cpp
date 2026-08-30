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

const fo4::FormKey k_quest = fo4::FormKey::parse("000800:Patch.esp");
const fo4::FormKey k_keyword = fo4::FormKey::parse("000111:Master.esm");
const fo4::FormKey k_item = fo4::FormKey::parse("000211:Master.esm");
const fo4::FormKey k_list = fo4::FormKey::parse("000311:Master.esm");
const fo4::FormKey k_message = fo4::FormKey::parse("000411:Master.esm");
const fo4::FormKey k_voice = fo4::FormKey::parse("000511:Master.esm");
const fo4::FormKey k_leveled = fo4::FormKey::parse("000611:Master.esm");
const fo4::FormKey k_spell = fo4::FormKey::parse("000711:Master.esm");
const fo4::FormKey k_faction = fo4::FormKey::parse("000811:Master.esm");
const fo4::FormKey k_package = fo4::FormKey::parse("000911:Master.esm");
const fo4::FormKey k_voice_types = fo4::FormKey::parse("000A11:Master.esm");

}

TEST(QuestAliasExtraDecode, RoundTripsReferenceExtrasThroughWriteAndDecode) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  auto creation = reference_alias(1);
  creation.keywords = {k_keyword};
  creation.items = {{.item = k_item, .count = 3, .extra = {}}};
  creation.spectator_override = k_list;
  creation.observe_dead_body_override = k_list;
  creation.guard_warn_override = k_list;
  creation.combat_override = k_list;
  creation.linked_aliases = {{k_keyword, 4}, {k_keyword, -1}};
  creation.display_name = k_message;
  creation.forced_voice = k_voice;
  creation.death_item = k_leveled;
  creation.spells = {k_spell, k_spell};
  creation.factions = {k_faction};
  creation.packages = {k_package};
  creation.voice_types = k_voice_types;

  const std::vector<fo4::forms::QuestAliasCreation> creations{creation};
  fo4::forms::write_quest_aliases(plugin, k_quest, creations);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  const auto& alias = decoded.aliases[0];

  ASSERT_EQ(alias.keywords.size(), 1u);
  EXPECT_EQ(alias.keywords[0], k_keyword);
  ASSERT_EQ(alias.items.size(), 1u);
  EXPECT_EQ(alias.items[0].item, k_item);
  EXPECT_EQ(alias.items[0].count, 3);
  EXPECT_FALSE(alias.items[0].extra.has_value());
  EXPECT_EQ(alias.spectator_override, std::optional<fo4::FormKey>{k_list});
  EXPECT_EQ(alias.observe_dead_body_override, std::optional<fo4::FormKey>{k_list});
  EXPECT_EQ(alias.guard_warn_override, std::optional<fo4::FormKey>{k_list});
  EXPECT_EQ(alias.combat_override, std::optional<fo4::FormKey>{k_list});
  ASSERT_EQ(alias.linked_aliases.size(), 2u);
  EXPECT_EQ(alias.linked_aliases[0].keyword, k_keyword);
  EXPECT_EQ(alias.linked_aliases[0].alias, 4);
  EXPECT_EQ(alias.linked_aliases[1].keyword, k_keyword);
  EXPECT_EQ(alias.linked_aliases[1].alias, -1);
  EXPECT_EQ(alias.display_name, std::optional<fo4::FormKey>{k_message});
  EXPECT_EQ(alias.forced_voice, std::optional<fo4::FormKey>{k_voice});
  EXPECT_EQ(alias.death_item, std::optional<fo4::FormKey>{k_leveled});
  ASSERT_EQ(alias.spells.size(), 2u);
  EXPECT_EQ(alias.spells[0], k_spell);
  EXPECT_EQ(alias.spells[1], k_spell);
  ASSERT_EQ(alias.factions.size(), 1u);
  EXPECT_EQ(alias.factions[0], k_faction);
  ASSERT_EQ(alias.packages.size(), 1u);
  EXPECT_EQ(alias.packages[0], k_package);
  EXPECT_EQ(alias.voice_types, std::optional<fo4::FormKey>{k_voice_types});
}

TEST(QuestAliasExtraDecode, BindsCoedToPrecedingCntoInMixedList) {
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
  write_u32_subrecord(out, "ANAM", 1);
  write_u32_subrecord(out, "ALST", 0);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
  write_u32_subrecord(out, "FNAM", 0);
  write_u32_subrecord(out, "COCT", 2);
  fo4::test::ByteWriter first;
  first.u32(0x00000211);
  first.i32(1);
  fo4::test::write_subrecord(out, "CNTO", first.bytes);
  fo4::test::ByteWriter second;
  second.u32(0x00000211);
  second.i32(2);
  fo4::test::write_subrecord(out, "CNTO", second.bytes);
  fo4::test::ByteWriter coed;
  coed.u32(0x00000811);
  coed.i32(3);
  coed.f32(1.0f);
  fo4::test::write_subrecord(out, "COED", coed.bytes);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);

  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(plugin, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  ASSERT_EQ(decoded.aliases[0].items.size(), 2u);
  EXPECT_FALSE(decoded.aliases[0].items[0].extra.has_value());
  ASSERT_TRUE(decoded.aliases[0].items[1].extra.has_value());
  EXPECT_EQ(decoded.aliases[0].items[1].extra->owner, std::optional<fo4::FormKey>{k_faction});
  EXPECT_EQ(decoded.aliases[0].items[1].extra->required_rank, std::optional<std::int32_t>{3});
  EXPECT_FLOAT_EQ(decoded.aliases[0].items[1].extra->item_condition, 1.0f);
}

TEST(QuestAliasExtraDecode, RoundTripsMixedCoedThroughWriteAndDecode) {
  auto plugin = fo4::read_plugin(empty_quest_plugin(), "Patch.esp");
  auto creation = reference_alias(1);
  fo4::forms::QuestAliasItem plain{.item = k_item, .count = 1, .extra = {}};
  fo4::forms::QuestAliasItem owned{.item = k_item, .count = 2, .extra = {}};
  owned.extra = fo4::forms::QuestAliasItemExtra{
      .owner = k_faction,
      .global = {},
      .required_rank = 4,
      .item_condition = 0.5f,
  };
  fo4::forms::QuestAliasItem npcOwned{.item = k_item, .count = 3, .extra = {}};
  npcOwned.extra = fo4::forms::QuestAliasItemExtra{
      .owner = k_voice,
      .global = k_list,
      .required_rank = {},
      .item_condition = 1.0f,
  };
  creation.items = {plain, owned, npcOwned};

  const std::vector<fo4::forms::QuestAliasCreation> creations{creation};
  fo4::forms::write_quest_aliases(plugin, k_quest, creations);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(reread, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  std::vector<std::string> names;
  for (const auto& member : decoded.aliases[0].subrecords) {
    names.push_back(member.signature.to_string());
  }
  const std::vector<std::string> expected{
      "ALST", "ALID", "FNAM", "COCT", "CNTO", "CNTO", "COED", "CNTO", "COED", "ALED"};
  EXPECT_EQ(names, expected);
  ASSERT_EQ(decoded.aliases[0].items.size(), 3u);
  EXPECT_FALSE(decoded.aliases[0].items[0].extra.has_value());
  ASSERT_TRUE(decoded.aliases[0].items[1].extra.has_value());
  EXPECT_EQ(decoded.aliases[0].items[1].extra->owner, std::optional<fo4::FormKey>{k_faction});
  EXPECT_EQ(decoded.aliases[0].items[1].extra->required_rank, std::optional<std::int32_t>{4});
  EXPECT_FLOAT_EQ(decoded.aliases[0].items[1].extra->item_condition, 0.5f);
  ASSERT_TRUE(decoded.aliases[0].items[2].extra.has_value());
  EXPECT_EQ(decoded.aliases[0].items[2].extra->owner, std::optional<fo4::FormKey>{k_voice});
  EXPECT_EQ(decoded.aliases[0].items[2].extra->global, std::optional<fo4::FormKey>{k_list});
  EXPECT_FLOAT_EQ(decoded.aliases[0].items[2].extra->item_condition, 1.0f);
}

TEST(QuestAliasExtraDecode, RejectsShortCoedAndStrayCoed) {
  auto shortCoed = [] {
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
    write_u32_subrecord(out, "ANAM", 1);
    write_u32_subrecord(out, "ALST", 0);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
    write_u32_subrecord(out, "FNAM", 0);
    write_u32_subrecord(out, "COCT", 1);
    fo4::test::ByteWriter cnto;
    cnto.u32(0x00000211);
    cnto.i32(2);
    fo4::test::write_subrecord(out, "CNTO", cnto.bytes);
    fo4::test::ByteWriter coed;
    coed.u32(0);
    fo4::test::write_subrecord(out, "COED", coed.bytes);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
  };
  auto strayCoed = [] {
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
    write_u32_subrecord(out, "ANAM", 1);
    write_u32_subrecord(out, "ALST", 0);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
    write_u32_subrecord(out, "FNAM", 0);
    fo4::test::ByteWriter coed;
    coed.u32(0);
    coed.i32(0);
    coed.f32(0.0f);
    fo4::test::write_subrecord(out, "COED", coed.bytes);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
  };

  EXPECT_THROW(
      (void)fo4::forms::decode_quest_aliases(
          fo4::read_plugin(shortCoed(), "Patch.esp"), k_quest),
      fo4::forms::QuestError);
  EXPECT_THROW(
      (void)fo4::forms::decode_quest_aliases(
          fo4::read_plugin(strayCoed(), "Patch.esp"), k_quest),
      fo4::forms::QuestError);
}

TEST(QuestAliasExtraDecode, DanglingKeywordDegradesThatEntry) {
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
  write_u32_subrecord(out, "ANAM", 1);
  write_u32_subrecord(out, "ALST", 0);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
  write_u32_subrecord(out, "FNAM", 0);
  write_u32_subrecord(out, "KSIZ", 2);
  fo4::test::ByteWriter kwda;
  kwda.u32(0x00000111);
  kwda.u32(0x03000123);
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);

  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto decoded = fo4::forms::decode_quest_aliases(plugin, k_quest);
  ASSERT_EQ(decoded.aliases.size(), 1u);
  const auto& alias = decoded.aliases[0];
  ASSERT_EQ(alias.keywords.size(), 1u);
  EXPECT_EQ(alias.keywords[0], k_keyword);
  ASSERT_EQ(alias.unresolved_links.size(), 1u);
  EXPECT_EQ(alias.unresolved_links[0].to_string(), "KWDA");
}

TEST(QuestAliasExtraDecode, RejectsMismatchedKeywordCount) {
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
  write_u32_subrecord(out, "ANAM", 1);
  write_u32_subrecord(out, "ALST", 0);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
  write_u32_subrecord(out, "FNAM", 0);
  write_u32_subrecord(out, "KSIZ", 2);
  fo4::test::ByteWriter kwda;
  kwda.u32(0x00000111);
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);

  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_THROW(
      (void)fo4::forms::decode_quest_aliases(plugin, k_quest),
      fo4::forms::QuestError);
}
