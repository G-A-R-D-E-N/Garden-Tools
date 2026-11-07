#include "fo4/forms/effects.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return record.subrecords.size();
}

std::vector<std::pair<std::string, std::vector<std::byte>>> subrecords_of(const fo4::Plugin& plugin,
                                                                          std::string_view key) {
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

std::size_t nth_efid(const std::vector<std::pair<std::string, std::vector<std::byte>>>& subrecords,
                     std::size_t wanted) {
  std::size_t seen = 0;
  for (std::size_t i = 0; i < subrecords.size(); ++i) {
    if (subrecords[i].first != "EFID") continue;
    if (seen == wanted) return i;
    ++seen;
  }
  return subrecords.size();
}

void write_effect(fo4::test::ByteWriter& out,
                  std::uint32_t effect,
                  float magnitude,
                  std::uint32_t area,
                  std::uint32_t duration) {
  fo4::test::ByteWriter efid;
  efid.u32(effect);
  fo4::test::write_subrecord(out, "EFID", efid.bytes);
  fo4::test::ByteWriter efit;
  efit.f32(magnitude);
  efit.u32(area);
  efit.u32(duration);
  fo4::test::write_subrecord(out, "EFIT", efit.bytes);
}

void write_conditions(fo4::test::ByteWriter& out, std::span<const fo4::CtdaCondition> conditions) {
  for (const auto& subrecord : fo4::serialize_ctda_run(conditions)) {
    fo4::test::write_subrecord(out, subrecord.signature.to_string(), subrecord.data);
  }
}

void write_sized_subrecord(fo4::test::ByteWriter& out,
                           std::string_view signature,
                           std::size_t size) {
  fo4::test::ByteWriter data;
  for (std::size_t i = 0; i < size; ++i) data.u8(1);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> effects_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0xB00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto mgef_group = fo4::test::begin_group(out, "MGEF", 0);
  auto fire = fo4::test::begin_record(out, "MGEF", 0, 0x00000801, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("FireDamage"));
  fo4::test::end_record(out, fire);
  auto frost = fo4::test::begin_record(out, "MGEF", 0, 0x00000802, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("FrostDamage"));
  fo4::test::end_record(out, frost);
  auto shock = fo4::test::begin_record(out, "MGEF", 0, 0x00000803, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("ShockDamage"));
  fo4::test::end_record(out, shock);
  fo4::test::end_group(out, mgef_group);

  const auto spel_group = fo4::test::begin_group(out, "SPEL", 0);
  auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Spell"));
  write_effect(out, 0x00000801, 1.5f, 2, 3);
  write_effect(out, 0x00000802, 2.5f, 0, 10);
  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.parameter1_string = std::string("Param");
  const std::vector<fo4::CtdaCondition> conditions{condition};
  write_conditions(out, conditions);
  fo4::test::end_record(out, spell);

  auto unexpected = fo4::test::begin_record(out, "SPEL", 0, 0x00000840, 131);
  write_effect(out, 0x00000801, 1.0f, 0, 0);
  fo4::test::write_subrecord(out, "DESC", std::string_view("text"));
  fo4::test::end_record(out, unexpected);
  fo4::test::end_group(out, spel_group);

  const auto alch_group = fo4::test::begin_group(out, "ALCH", 0);
  auto potion = fo4::test::begin_record(out, "ALCH", 0, 0x00000810, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Potion"));
  write_effect(out, 0x00000801, 1.0f, 0, 0);
  write_effect(out, 0x00000802, 3.0f, 0, 0);
  fo4::test::end_record(out, potion);
  auto empty_potion = fo4::test::begin_record(out, "ALCH", 0, 0x00000811, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("EmptyPotion"));
  fo4::test::end_record(out, empty_potion);
  fo4::test::end_group(out, alch_group);

  const auto ench_group = fo4::test::begin_group(out, "ENCH", 0);
  auto enchantment = fo4::test::begin_record(out, "ENCH", 0, 0x00000820, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Ench"));
  fo4::test::end_record(out, enchantment);
  fo4::test::end_group(out, ench_group);

  const auto weap_group = fo4::test::begin_group(out, "WEAP", 0);
  auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000830, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Weapon"));
  fo4::test::end_record(out, weapon);
  fo4::test::end_group(out, weap_group);
  return out.bytes;
}

std::vector<std::byte> spell_plugin(const std::function<void(fo4::test::ByteWriter&)>& body) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0xB00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto spel_group = fo4::test::begin_group(out, "SPEL", 0);
  auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x00000800, 131);
  body(out);
  fo4::test::end_record(out, spell);
  fo4::test::end_group(out, spel_group);
  return out.bytes;
}

std::vector<std::byte> master_owned_effect_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0xB00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto spel_group = fo4::test::begin_group(out, "SPEL", 0);
  auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x01000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("MasterSpell"));
  fo4::test::end_record(out, spell);
  fo4::test::end_group(out, spel_group);
  return out.bytes;
}

}  // namespace

TEST(MagicEffects, ReadsSpellEffectsWithConditionAndString) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto* spell = fo4::find_record(plugin, fo4::FormKey::parse("000800:Fx.esp"));
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(plugin, *spell);
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000801:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 1.5f);
  EXPECT_EQ(effects[0].area, 2u);
  EXPECT_EQ(effects[0].duration, 3u);
  EXPECT_TRUE(effects[0].conditions.empty());
  EXPECT_EQ(effects[1].effect, fo4::FormKey::parse("000802:Fx.esp"));
  ASSERT_EQ(effects[1].conditions.size(), 1u);
  EXPECT_EQ(effects[1].conditions[0].function, 72u);
  ASSERT_TRUE(effects[1].conditions[0].parameter1_string.has_value());
  EXPECT_EQ(*effects[1].conditions[0].parameter1_string, "Param");
}

TEST(MagicEffects, ReplacesAlchListAndRoundTrips) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000810:Fx.esp");
  fo4::CtdaCondition condition;
  condition.function = 9;
  condition.parameter2_string = std::string("Second");
  const std::vector<fo4::CtdaCondition> conditions{condition};
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000802:Fx.esp"), 4.0f, 1u, 2u, conditions},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, key, desired),
            fo4::forms::EffectListWrite::written);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto* potion = fo4::find_record(reread, key);
  ASSERT_NE(potion, nullptr);
  const auto effects = fo4::forms::read_magic_effects(reread, *potion);
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000802:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 4.0f);
  EXPECT_EQ(effects[0].area, 1u);
  EXPECT_EQ(effects[0].duration, 2u);
  ASSERT_EQ(effects[0].conditions.size(), 1u);
  EXPECT_EQ(effects[0].conditions[0].function, 9u);
  ASSERT_TRUE(effects[0].conditions[0].parameter2_string.has_value());
  EXPECT_EQ(*effects[0].conditions[0].parameter2_string, "Second");
}

TEST(MagicEffects, AppendsBlockAtEndWhenRecordHasNoEffects) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000820:Fx.esp");
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000801:Fx.esp"), 2.0f, 0u, 5u, {}},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, key, desired),
            fo4::forms::EffectListWrite::written);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto* enchantment = fo4::find_record(reread, key);
  ASSERT_NE(enchantment, nullptr);
  EXPECT_EQ(index_of(*enchantment, "EFID"), enchantment->subrecords.size() - 2);
  EXPECT_EQ(index_of(*enchantment, "EFIT"), enchantment->subrecords.size() - 1);
  const auto effects = fo4::forms::read_magic_effects(reread, *enchantment);
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000801:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 2.0f);
  EXPECT_EQ(effects[0].duration, 5u);
  EXPECT_TRUE(effects[0].conditions.empty());
}

TEST(MagicEffects, UnsupportedOrMissingRecordIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto weapon_before = subrecords_of(plugin, "000830:Fx.esp");
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000801:Fx.esp"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000830:Fx.esp"), desired),
            fo4::forms::EffectListWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000999:Fx.esp"), desired),
            fo4::forms::EffectListWrite::record_missing);
  EXPECT_EQ(subrecords_of(plugin, "000830:Fx.esp"), weapon_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, UnexpectedLayoutIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto spell_before = subrecords_of(plugin, "000840:Fx.esp");
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000801:Fx.esp"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000840:Fx.esp"), desired),
            fo4::forms::EffectListWrite::unexpected_layout);
  EXPECT_EQ(subrecords_of(plugin, "000840:Fx.esp"), spell_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, EffectFromUnlistedPluginIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto spell_before = subrecords_of(plugin, "000800:Fx.esp");
  const fo4::forms::MagicEffectEntry foreign[] = {
      {fo4::FormKey::parse("000801:Other.esm"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_THROW(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000800:Fx.esp"), foreign),
               fo4::forms::MagicEffectError);
  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), spell_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, EmptyListRemovesTheWholeBlockAndKeepsEarlierSubrecords) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto edid = subrecords_of(plugin, "000800:Fx.esp").front();
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, key,
                                          std::span<const fo4::forms::MagicEffectEntry>{}),
            fo4::forms::EffectListWrite::written);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto* spell = fo4::find_record(reread, key);
  ASSERT_NE(spell, nullptr);
  EXPECT_EQ(subrecords_of(reread, "000800:Fx.esp"), (std::vector{edid}));
  EXPECT_TRUE(fo4::forms::read_magic_effects(reread, *spell).empty());
}

TEST(MagicEffects, ReadRefusesRecordsThatCarryNoEffects) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto* weapon = fo4::find_record(plugin, fo4::FormKey::parse("000830:Fx.esp"));
  ASSERT_NE(weapon, nullptr);
  EXPECT_THROW(static_cast<void>(fo4::forms::read_magic_effects(plugin, *weapon)),
               fo4::forms::MagicEffectError);
  const auto* enchantment = fo4::find_record(plugin, fo4::FormKey::parse("000820:Fx.esp"));
  ASSERT_NE(enchantment, nullptr);
  EXPECT_TRUE(fo4::forms::read_magic_effects(plugin, *enchantment).empty());
}

TEST(MagicEffects, ReadFailsClosedOnMalformedEffectBlocks) {
  const std::vector<std::pair<std::string, std::function<void(fo4::test::ByteWriter&)>>> cases{
      {"lone EFID without EFIT",
       [](fo4::test::ByteWriter& out) { write_sized_subrecord(out, "EFID", 4); }},
      {"EFID of five bytes",
       [](fo4::test::ByteWriter& out) {
         write_sized_subrecord(out, "EFID", 5);
         write_sized_subrecord(out, "EFIT", 12);
       }},
      {"EFIT of eleven bytes",
       [](fo4::test::ByteWriter& out) {
         write_sized_subrecord(out, "EFID", 4);
         write_sized_subrecord(out, "EFIT", 11);
       }},
      {"second EFIT with no conditions between",
       [](fo4::test::ByteWriter& out) {
         write_sized_subrecord(out, "EFID", 4);
         write_sized_subrecord(out, "EFIT", 12);
         write_sized_subrecord(out, "EFIT", 12);
       }},
      {"second EFIT after a CTDA",
       [](fo4::test::ByteWriter& out) {
         write_sized_subrecord(out, "EFID", 4);
         write_sized_subrecord(out, "EFIT", 12);
         write_sized_subrecord(out, "CTDA", 4);
         write_sized_subrecord(out, "EFIT", 12);
       }},
      {"conditions starting with CIS1",
       [](fo4::test::ByteWriter& out) {
         write_sized_subrecord(out, "EFID", 4);
         write_sized_subrecord(out, "EFIT", 12);
         fo4::test::write_subrecord(out, "CIS1", std::string_view("Param"));
       }},
      {"DESC inside the effect block",
       [](fo4::test::ByteWriter& out) {
         write_effect(out, 0x00000801, 1.0f, 0, 0);
         fo4::test::write_subrecord(out, "DESC", std::string_view("text"));
         write_effect(out, 0x00000802, 2.0f, 0, 0);
       }},
  };
  for (const auto& [label, body] : cases) {
    SCOPED_TRACE(label);
    auto plugin = fo4::read_plugin(spell_plugin(body), "Fx.esp");
    const auto* spell = fo4::find_record(plugin, fo4::FormKey::parse("000800:Fx.esp"));
    ASSERT_NE(spell, nullptr);
    EXPECT_THROW(static_cast<void>(fo4::forms::read_magic_effects(plugin, *spell)),
                 fo4::forms::MagicEffectError);
  }
}

TEST(MagicEffects, ReplaceWithTheSameEntriesKeepsConditionSubrecordsIdentical) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto* spell = fo4::find_record(plugin, key);
  ASSERT_NE(spell, nullptr);
  const auto entries = fo4::forms::read_magic_effects(plugin, *spell);
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, key, entries),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), before);
}

TEST(MagicEffects, AddsEffectToRecordWithNoEffects) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000811:Fx.esp");
  const auto before = subrecords_of(plugin, "000811:Fx.esp");
  ASSERT_EQ(nth_efid(before, 0), before.size());

  std::size_t index = 99;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key,
                                         {fo4::FormKey::parse("000803:Fx.esp"), 2.0f, 1u, 4u, {}},
                                         &index),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(index, 0u);

  const auto* potion = fo4::find_record(plugin, key);
  ASSERT_NE(potion, nullptr);
  const auto effects = fo4::forms::read_magic_effects(plugin, *potion);
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000803:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 2.0f);
  EXPECT_EQ(effects[0].area, 1u);
  EXPECT_EQ(effects[0].duration, 4u);
  EXPECT_TRUE(effects[0].conditions.empty());

  const auto after = subrecords_of(plugin, "000811:Fx.esp");
  ASSERT_EQ(after.size(), before.size() + 2);
  EXPECT_EQ(std::vector(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size())),
            before);
  EXPECT_EQ(after[before.size()].first, "EFID");
  EXPECT_EQ(after.back().first, "EFIT");
}

TEST(MagicEffects, AddsThirdEffectAndKeepsEarlierEntriesIdentical) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  const auto second_before = nth_efid(before, 1);
  ASSERT_LT(second_before, before.size());

  std::size_t index = 99;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key,
                                         {fo4::FormKey::parse("000803:Fx.esp"), 7.0f, 3u, 8u, {}},
                                         &index),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(index, 2u);

  const auto* spell = fo4::find_record(plugin, key);
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(plugin, *spell);
  ASSERT_EQ(effects.size(), 3u);
  EXPECT_EQ(effects[2].effect, fo4::FormKey::parse("000803:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[2].magnitude, 7.0f);
  EXPECT_EQ(effects[2].area, 3u);
  EXPECT_EQ(effects[2].duration, 8u);

  const auto after = subrecords_of(plugin, "000800:Fx.esp");
  ASSERT_EQ(after.size(), before.size() + 2);
  EXPECT_EQ(nth_efid(after, 1), second_before);
  EXPECT_EQ(std::vector(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size())),
            before);
}

TEST(MagicEffects, SetReplacesOnlyTheChosenEntry) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  const auto second_before = nth_efid(before, 1);
  ASSERT_LT(second_before, before.size());

  EXPECT_EQ(fo4::forms::set_magic_effect(
                plugin, key, 0, {fo4::FormKey::parse("000803:Fx.esp"), 9.0f, 0u, 1u, {}}),
            fo4::forms::EffectListWrite::written);

  const auto after = subrecords_of(plugin, "000800:Fx.esp");
  EXPECT_EQ(after.front(), before.front());
  const auto second_after = nth_efid(after, 1);
  ASSERT_LT(second_after, after.size());
  EXPECT_EQ(std::vector(after.begin() + static_cast<std::ptrdiff_t>(second_after), after.end()),
            std::vector(before.begin() + static_cast<std::ptrdiff_t>(second_before), before.end()));

  const auto* spell = fo4::find_record(plugin, key);
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(plugin, *spell);
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000803:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 9.0f);
  EXPECT_EQ(effects[0].duration, 1u);
  EXPECT_TRUE(effects[0].conditions.empty());
  EXPECT_EQ(effects[1].effect, fo4::FormKey::parse("000802:Fx.esp"));
  ASSERT_EQ(effects[1].conditions.size(), 1u);
  EXPECT_EQ(effects[1].conditions[0].function, 72u);
}

TEST(MagicEffects, RemoveDropsOnlyTheChosenEntryAndShiftsTheRest) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  const auto second_before = nth_efid(before, 1);
  ASSERT_LT(second_before, before.size());

  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, key, 0),
            fo4::forms::EffectListWrite::written);

  auto expected = before;
  expected.erase(expected.begin() + 1,
                 expected.begin() + static_cast<std::ptrdiff_t>(second_before));
  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), expected);

  const auto* spell = fo4::find_record(plugin, key);
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(plugin, *spell);
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000802:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 2.5f);
  EXPECT_EQ(effects[0].duration, 10u);
  ASSERT_EQ(effects[0].conditions.size(), 1u);
  EXPECT_EQ(effects[0].conditions[0].function, 72u);
}

TEST(MagicEffects, RefusesIndexOutsideTheListWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto subrecords_before = subrecords_of(plugin, "000800:Fx.esp");

  EXPECT_EQ(fo4::forms::set_magic_effect(
                plugin, key, 2, {fo4::FormKey::parse("000803:Fx.esp"), 1.0f, 0u, 0u, {}}),
            fo4::forms::EffectListWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::set_magic_effect(
                plugin, key, 99, {fo4::FormKey::parse("000803:Fx.esp"), 1.0f, 0u, 0u, {}}),
            fo4::forms::EffectListWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, key, 2),
            fo4::forms::EffectListWrite::index_out_of_range);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, key, 99),
            fo4::forms::EffectListWrite::index_out_of_range);

  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), subrecords_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, RefusesEffectsThatAreNotMagicEffectsWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto subrecords_before = subrecords_of(plugin, "000800:Fx.esp");

  const fo4::forms::MagicEffectEntry enchantment{fo4::FormKey::parse("000820:Fx.esp"), 1.0f, 0u, 0u,
                                                 {}};
  const fo4::forms::MagicEffectEntry weapon{fo4::FormKey::parse("000830:Fx.esp"), 1.0f, 0u, 0u, {}};
  const fo4::forms::MagicEffectEntry zero{fo4::FormKey{}, 1.0f, 0u, 0u, {}};
  const fo4::forms::MagicEffectEntry foreign{fo4::FormKey::parse("000801:Other.esm"), 1.0f, 0u, 0u,
                                             {}};

  std::size_t index = 77;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key, enchantment, &index),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key, weapon, &index),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key, zero, &index),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key, foreign, &index),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(fo4::forms::set_magic_effect(plugin, key, 0, zero),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(fo4::forms::set_magic_effect(plugin, key, 1, weapon),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(index, 77u);

  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), subrecords_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, RefusesMissingUnsupportedAndMalformedRecordsWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto weapon_before = subrecords_of(plugin, "000830:Fx.esp");
  const auto malformed_before = subrecords_of(plugin, "000840:Fx.esp");
  const fo4::forms::MagicEffectEntry entry{fo4::FormKey::parse("000803:Fx.esp"), 1.0f, 0u, 0u, {}};

  std::size_t index = 5;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, fo4::FormKey::parse("000999:Fx.esp"), entry, &index),
            fo4::forms::EffectListWrite::record_missing);
  EXPECT_EQ(fo4::forms::set_magic_effect(plugin, fo4::FormKey::parse("000999:Fx.esp"), 0, entry),
            fo4::forms::EffectListWrite::record_missing);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, fo4::FormKey::parse("000999:Fx.esp"), 0),
            fo4::forms::EffectListWrite::record_missing);

  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, fo4::FormKey::parse("000830:Fx.esp"), entry, &index),
            fo4::forms::EffectListWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::set_magic_effect(plugin, fo4::FormKey::parse("000830:Fx.esp"), 0, entry),
            fo4::forms::EffectListWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, fo4::FormKey::parse("000830:Fx.esp"), 0),
            fo4::forms::EffectListWrite::unsupported_record);

  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, fo4::FormKey::parse("000840:Fx.esp"), entry, &index),
            fo4::forms::EffectListWrite::unexpected_layout);
  EXPECT_EQ(fo4::forms::set_magic_effect(plugin, fo4::FormKey::parse("000840:Fx.esp"), 0, entry),
            fo4::forms::EffectListWrite::unexpected_layout);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, fo4::FormKey::parse("000840:Fx.esp"), 0),
            fo4::forms::EffectListWrite::unexpected_layout);
  EXPECT_EQ(index, 5u);

  EXPECT_EQ(subrecords_of(plugin, "000830:Fx.esp"), weapon_before);
  EXPECT_EQ(subrecords_of(plugin, "000840:Fx.esp"), malformed_before);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, EditedListSurvivesSaveAndReload) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");

  std::size_t index = 99;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key,
                                         {fo4::FormKey::parse("000803:Fx.esp"), 4.0f, 1u, 6u, {}},
                                         &index),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(index, 2u);
  EXPECT_EQ(fo4::forms::set_magic_effect(
                plugin, key, 0, {fo4::FormKey::parse("000802:Fx.esp"), 5.0f, 2u, 4u, {}}),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(fo4::forms::remove_magic_effect(plugin, key, 1),
            fo4::forms::EffectListWrite::written);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto* spell = fo4::find_record(reread, key);
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(reread, *spell);
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000802:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[0].magnitude, 5.0f);
  EXPECT_EQ(effects[0].area, 2u);
  EXPECT_EQ(effects[0].duration, 4u);
  EXPECT_TRUE(effects[0].conditions.empty());
  EXPECT_EQ(effects[1].effect, fo4::FormKey::parse("000803:Fx.esp"));
  EXPECT_FLOAT_EQ(effects[1].magnitude, 4.0f);
  EXPECT_EQ(effects[1].area, 1u);
  EXPECT_EQ(effects[1].duration, 6u);
  EXPECT_TRUE(effects[1].conditions.empty());
}

TEST(MagicEffects, AcceptsMasterOwnedEffectWithItsMasterIndexAndRefusesUndeclaredOwner) {
  auto plugin = fo4::read_plugin(master_owned_effect_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");

  std::size_t index = 99;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key,
                                         {fo4::FormKey::parse("000801:Fallout4.esm"), 2.0f, 0u, 3u, {}},
                                         &index),
            fo4::forms::EffectListWrite::written);
  EXPECT_EQ(index, 0u);
  const auto subrecords = subrecords_of(plugin, "000800:Fx.esp");
  const auto efid = nth_efid(subrecords, 0);
  ASSERT_LT(efid, subrecords.size());
  EXPECT_EQ(subrecords[efid].second, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x08},
                                                             std::byte{0x00}, std::byte{0x00}}));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto* spell = fo4::find_record(reread, key);
  ASSERT_NE(spell, nullptr);
  const auto effects = fo4::forms::read_magic_effects(reread, *spell);
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(effects[0].effect, fo4::FormKey::parse("000801:Fallout4.esm"));
  EXPECT_EQ(effects[0].duration, 3u);

  const auto before = fo4::write_plugin(plugin);
  const fo4::forms::MagicEffectEntry foreign{fo4::FormKey::parse("000801:Other.esm"), 1.0f, 0u, 0u,
                                             {}};
  std::size_t refused = 77;
  EXPECT_EQ(fo4::forms::add_magic_effect(plugin, key, foreign, &refused),
            fo4::forms::EffectListWrite::effect_not_magic_effect);
  EXPECT_EQ(refused, 77u);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
