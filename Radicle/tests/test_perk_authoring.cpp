#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

Bytes make_patch(std::uint32_t flags) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0xD10);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

fo4::schema::LocalizedString inline_text(std::string text) {
  fo4::schema::LocalizedString value;
  value.text = std::move(text);
  return value;
}

fo4::schema::LocalizedString string_id(std::uint32_t id) {
  fo4::schema::LocalizedString value;
  value.id = id;
  return value;
}

fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

fo4::forms::PerkEffectEntry quest_effect() {
  fo4::forms::PerkEffectEntry effect;
  effect.type = fo4::forms::PerkEffectType::quest_stage;
  effect.rank = 1;
  effect.priority = 2;
  effect.quest = fo4::FormKey::parse("000900:Fallout4.esm");
  effect.quest_stage = 10;
  return effect;
}

fo4::forms::PerkEffectEntry ability_effect() {
  fo4::forms::PerkEffectEntry effect;
  effect.type = fo4::forms::PerkEffectType::ability;
  effect.ability = fo4::FormKey::parse("000A00:Fallout4.esm");
  return effect;
}

fo4::forms::PerkEffectEntry entry_point_effect() {
  fo4::forms::PerkEffectEntry effect;
  effect.type = fo4::forms::PerkEffectType::entry_point;
  effect.entry_point = 3;
  effect.function = 5;
  effect.parameters.type = 4;
  effect.parameters.entry_id = 7;
  effect.parameters.script_flags = 1;
  effect.parameters.data = Bytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
  return effect;
}

std::vector<std::string> signatures(const fo4::Record& record) {
  std::vector<std::string> out;
  for (const auto& subrecord : record.subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

}  // namespace

TEST(PerkAuthoring, CreatesHeaderTextAndEffectsInSchemaOrder) {
  auto plugin = fo4::read_plugin(make_patch(0), "Patch.esp");
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleCreated";
  creation.full_name = inline_text("Created");
  creation.description = inline_text("Made in test");
  creation.trait = 1;
  creation.level = 5;
  creation.ranks = 3;
  creation.playable = 0;
  creation.effects = {quest_effect(), ability_effect(), entry_point_effect()};

  const auto key = fo4::forms::create_perk(plugin, creation);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record),
            (std::vector<std::string>{"EDID", "FULL", "DESC", "DATA", "PRKE", "DATA", "PRKF",
                                      "PRKE", "DATA", "PRKF", "PRKE", "DATA", "EPFT", "EPFB",
                                      "EPF3", "EPFD", "PRKF"}));
  EXPECT_EQ(record->subrecords[3].data,
            (Bytes{std::byte{1}, std::byte{5}, std::byte{3}, std::byte{0}, std::byte{0}}));

  const auto effects = fo4::forms::read_perk_effects(plugin, *record);
  ASSERT_EQ(effects.size(), 3u);
  EXPECT_EQ(effects[0].type, fo4::forms::PerkEffectType::quest_stage);
  EXPECT_EQ(effects[0].rank, 1);
  EXPECT_EQ(effects[0].priority, 2);
  EXPECT_EQ(effects[0].quest, fo4::FormKey::parse("000900:Fallout4.esm"));
  EXPECT_EQ(effects[0].quest_stage, 10);
  EXPECT_EQ(effects[1].ability, fo4::FormKey::parse("000A00:Fallout4.esm"));
  EXPECT_EQ(effects[2].entry_point, 3);
  EXPECT_EQ(effects[2].function, 5);
  EXPECT_EQ(effects[2].parameters.type, 4);
  EXPECT_EQ(effects[2].parameters.entry_id, 7);
  EXPECT_EQ(effects[2].parameters.script_flags, 1);
  ASSERT_TRUE(effects[2].parameters.data.has_value());
  EXPECT_EQ(*effects[2].parameters.data, (Bytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}}));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* persisted = fo4::find_record(reread, key);
  ASSERT_NE(persisted, nullptr);
  EXPECT_EQ(signatures(*persisted), signatures(*record));
}

TEST(PerkAuthoring, CreatesDefaultHeaderWithoutEffects) {
  auto plugin = fo4::read_plugin(make_patch(0), "Patch.esp");
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleBare";

  const auto key = fo4::forms::create_perk(plugin, creation);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(record->subrecords[1].data,
            (Bytes{std::byte{0}, std::byte{0}, std::byte{1}, std::byte{1}, std::byte{0}}));
  EXPECT_TRUE(fo4::forms::read_perk_effects(plugin, *record).empty());
}

TEST(PerkAuthoring, RefusesLocalizedTextAndLeavesThePluginUnchanged) {
  constexpr std::uint32_t localized_flag = 0x80;
  auto plugin = fo4::read_plugin(make_patch(localized_flag), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleLocalized";
  creation.full_name = inline_text("Localized");
  EXPECT_THROW(fo4::forms::create_perk(plugin, creation), fo4::forms::PerkError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Detects: create_perk must write FULL and DESC as four-byte string IDs in a localized plugin and refuse inline text there.
TEST(PerkAuthoring, CreatesLocalizedFullAndDescAsStringIds) {
  constexpr std::uint32_t localized_flag = 0x80;
  auto plugin = fo4::read_plugin(make_patch(localized_flag), "Patch.esp");
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleStringIds";
  creation.full_name = string_id(7);
  creation.description = string_id(9);

  const auto key = fo4::forms::create_perk(plugin, creation);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record), (std::vector<std::string>{"EDID", "FULL", "DESC", "DATA"}));
  EXPECT_EQ(record->subrecords[1].data,
            (Bytes{std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(record->subrecords[2].data,
            (Bytes{std::byte{9}, std::byte{0}, std::byte{0}, std::byte{0}}));

  fo4::forms::PerkCreation inline_creation;
  inline_creation.editor_id = "RadicleInlineRefused";
  inline_creation.full_name = inline_text("Inline");
  EXPECT_THROW(fo4::forms::create_perk(plugin, inline_creation), fo4::forms::PerkError);
}

// Detects: create_perk must write root CTDA before DATA and SNAM, NNAM, FNAM after DATA, in schema order, and read them back.
TEST(PerkAuthoring, CreatesConditionsLinksAndSoundInSchemaOrder) {
  auto plugin = fo4::read_plugin(make_patch(0), "Patch.esp");
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleLinks";
  creation.full_name = inline_text("Linked");
  creation.conditions = {function_condition(7)};
  creation.sound = fo4::FormKey::parse("000B00:Fallout4.esm");
  creation.next_perk = fo4::FormKey::parse("000C00:Fallout4.esm");
  creation.swf = "Interface\\Perk.swf";
  creation.effects = {ability_effect()};

  const auto key = fo4::forms::create_perk(plugin, creation);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record),
            (std::vector<std::string>{"EDID", "FULL", "CTDA", "DATA", "SNAM", "NNAM", "FNAM",
                                      "PRKE", "DATA", "PRKF"}));

  const auto links = fo4::forms::read_perk_links(plugin, *record);
  EXPECT_EQ(links.sound, fo4::FormKey::parse("000B00:Fallout4.esm"));
  EXPECT_EQ(links.next_perk, fo4::FormKey::parse("000C00:Fallout4.esm"));
  ASSERT_TRUE(links.swf.has_value());
  EXPECT_EQ(*links.swf, "Interface\\Perk.swf");
  const auto root = fo4::schema::read_root_conditions(*record);
  ASSERT_TRUE(root.ok);
  ASSERT_EQ(root.conditions.size(), 1u);
  EXPECT_EQ(root.conditions[0].function, 7);
  EXPECT_EQ(fo4::forms::read_perk_effects(plugin, *record).size(), 1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* persisted = fo4::find_record(reread, key);
  ASSERT_NE(persisted, nullptr);
  EXPECT_EQ(signatures(*persisted), signatures(*record));
}

// Detects: set_perk_next must insert NNAM before the first FNAM or PRKE and rewrite an existing NNAM in place.
TEST(PerkAuthoring, SetPerkNextInsertsBeforeEffectsAndRewritesInPlace) {
  auto plugin = fo4::read_plugin(make_patch(0), "Patch.esp");
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleNext";
  creation.effects = {ability_effect()};
  const auto key = fo4::forms::create_perk(plugin, creation);

  const auto first = fo4::FormKey::parse("000C00:Fallout4.esm");
  EXPECT_EQ(fo4::forms::set_perk_next(plugin, key, first), fo4::forms::PerkLinkWrite::written);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record), (std::vector<std::string>{"EDID", "DATA", "NNAM", "PRKE", "DATA", "PRKF"}));
  EXPECT_EQ(fo4::forms::read_perk_links(plugin, *record).next_perk, first);

  const auto second = fo4::FormKey::parse("000D00:Fallout4.esm");
  EXPECT_EQ(fo4::forms::set_perk_next(plugin, key, second), fo4::forms::PerkLinkWrite::written);
  record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures(*record), (std::vector<std::string>{"EDID", "DATA", "NNAM", "PRKE", "DATA", "PRKF"}));
  EXPECT_EQ(fo4::forms::read_perk_links(plugin, *record).next_perk, second);

  EXPECT_EQ(fo4::forms::set_perk_next(plugin, fo4::FormKey::parse("000E01:Patch.esp"), second),
            fo4::forms::PerkLinkWrite::record_missing);
}

// Detects: perk_function_type must select actor_value for EPFT 2 only when the entry function is 5, 12, 13 or 14.
TEST(PerkAuthoring, PerkFunctionTypeFollowsEpftAndEntryFunction) {
  fo4::forms::PerkEffectEntry effect;
  effect.type = fo4::forms::PerkEffectType::entry_point;
  effect.function = 5;
  effect.parameters.type = 2;
  EXPECT_EQ(fo4::forms::perk_function_type(effect), fo4::forms::PerkFunctionType::actor_value);
  effect.function = 4;
  EXPECT_EQ(fo4::forms::perk_function_type(effect), fo4::forms::PerkFunctionType::float_pair);
  effect.parameters.type = 8;
  EXPECT_EQ(fo4::forms::perk_function_type(effect), fo4::forms::PerkFunctionType::actor_value);
  effect.parameters.type = 1;
  EXPECT_EQ(fo4::forms::perk_function_type(effect), fo4::forms::PerkFunctionType::single_float);
  effect.parameters.type.reset();
  EXPECT_EQ(fo4::forms::perk_function_type(effect), fo4::forms::PerkFunctionType::none);
}

TEST(PerkAuthoring, RefusesOutOfRangeTraitAndLeavesThePluginUnchanged) {
  auto plugin = fo4::read_plugin(make_patch(0), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  fo4::forms::PerkCreation creation;
  creation.editor_id = "RadicleBadTrait";
  creation.trait = 2;
  EXPECT_THROW(fo4::forms::create_perk(plugin, creation), std::runtime_error);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
