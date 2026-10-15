#include "fo4/forms/effects.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return record.subrecords.size();
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

std::vector<std::byte> effects_plugin() {
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
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000801:Fx.esp"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000830:Fx.esp"), desired),
            fo4::forms::EffectListWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000999:Fx.esp"), desired),
            fo4::forms::EffectListWrite::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, UnexpectedLayoutIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const fo4::forms::MagicEffectEntry desired[] = {
      {fo4::FormKey::parse("000801:Fx.esp"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_EQ(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000840:Fx.esp"), desired),
            fo4::forms::EffectListWrite::unexpected_layout);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MagicEffects, EffectFromUnlistedPluginIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const fo4::forms::MagicEffectEntry foreign[] = {
      {fo4::FormKey::parse("000801:Other.esm"), 1.0f, 0u, 0u, {}},
  };
  EXPECT_THROW(fo4::forms::set_magic_effects(plugin, fo4::FormKey::parse("000800:Fx.esp"), foreign),
               fo4::forms::MagicEffectError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
