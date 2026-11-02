#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
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
  creation.full_name = "Created";
  creation.description = "Made in test";
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
  creation.full_name = "Localized";
  EXPECT_THROW(fo4::forms::create_perk(plugin, creation), fo4::forms::PerkError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
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
