#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

fo4::test::ByteWriter make_ctda(std::uint16_t function, std::int32_t param1) {
  fo4::test::ByteWriter ctda;
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.i32(param1);
  ctda.i32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.i32(-1);
  return ctda;
}

fo4::test::ByteWriter make_efit() {
  fo4::test::ByteWriter efit;
  efit.f32(1.0f);
  efit.u32(0);
  efit.u32(0);
  return efit;
}

struct EffectSpec {
  std::uint32_t effect_form_id;
  std::vector<std::uint16_t> condition_functions;
  bool has_efid = true;
  bool has_efit = true;
};

fo4::Plugin make_spell(const std::vector<EffectSpec>& effects) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "SPEL", 0);
  const auto record = fo4::test::begin_record(out, "SPEL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondSpell\0", 10});
  fo4::test::ByteWriter spit;
  for (int i = 0; i < 15; ++i) spit.u32(0);
  fo4::test::write_subrecord(out, "SPIT", spit.bytes);
  for (const auto& effect : effects) {
    if (effect.has_efid) {
      fo4::test::ByteWriter efid;
      efid.u32(effect.effect_form_id);
      fo4::test::write_subrecord(out, "EFID", efid.bytes);
    }
    if (effect.has_efit) {
      fo4::test::write_subrecord(out, "EFIT", make_efit().bytes);
    }
    for (const auto function : effect.condition_functions) {
      fo4::test::write_subrecord(out, "CTDA", make_ctda(function, 0).bytes);
    }
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* spell_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

fo4::schema::ConditionScopeDescriptor spel_descriptor() {
  const auto path = fo4::schema::scoped_condition_path(
      fo4::Signature::from_string("SPEL"), "Effects[0].Conditions");
  return path->descriptor;
}

}  // namespace

TEST(ConditionScopes, ResolvesEffectPathForKnownRecords) {
  const auto spel = fo4::schema::scoped_condition_path(
      fo4::Signature::from_string("SPEL"), "Effects[2].Conditions");
  ASSERT_TRUE(spel.has_value());
  EXPECT_EQ(spel->descriptor.record, fo4::Signature::from_string("SPEL"));
  EXPECT_EQ(spel->container_index, 2u);

  for (const auto* record : {"ALCH", "ENCH", "INGR", "SPEL"}) {
    EXPECT_TRUE(fo4::schema::scoped_condition_path(
                    fo4::Signature::from_string(record), "Effects[0].Conditions")
                    .has_value());
  }

  const auto lower = fo4::schema::scoped_condition_path(
      fo4::Signature::from_string("SPEL"), "effects[1].conditions");
  ASSERT_TRUE(lower.has_value());
  EXPECT_EQ(lower->container_index, 1u);
}

TEST(ConditionScopes, RejectsUnknownRecordsAndMalformedPaths) {
  EXPECT_FALSE(fo4::schema::scoped_condition_path(
                   fo4::Signature::from_string("WEAP"), "Effects[0].Conditions")
                   .has_value());
  const auto spel = fo4::Signature::from_string("SPEL");
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0].Keywords").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0]").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects.Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[x].Conditions").has_value());
}

TEST(ConditionScopes, ReadsScopedConditionsPerEffect) {
  const auto plugin = make_spell({{0x00000801, {1, 2}}, {0x00000802, {5}}, {0x00000803, {}}});
  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto descriptor = spel_descriptor();

  const auto first = fo4::schema::read_scoped_conditions(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 2u);
  EXPECT_EQ((*first)[0].function, 1u);
  EXPECT_EQ((*first)[1].function, 2u);

  const auto second = fo4::schema::read_scoped_conditions(*record, descriptor, 1);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->size(), 1u);
  EXPECT_EQ((*second)[0].function, 5u);

  const auto third = fo4::schema::read_scoped_conditions(*record, descriptor, 2);
  ASSERT_TRUE(third.has_value());
  EXPECT_TRUE(third->empty());

  EXPECT_FALSE(fo4::schema::read_scoped_conditions(*record, descriptor, 3).has_value());
}

TEST(ConditionScopes, SetReplacesOnlyTargetEffect) {
  auto plugin = make_spell({{0x00000801, {1, 2}}, {0x00000802, {5}}});
  const auto descriptor = spel_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  fo4::CtdaCondition replacement;
  replacement.function = 42;
  const std::vector<fo4::CtdaCondition> conditions{replacement};

  const auto status = fo4::schema::set_record_scoped_conditions(
      plugin, key, descriptor, 0, conditions);
  EXPECT_EQ(status, fo4::schema::ScopedConditionStatus::updated);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Cond.esp");
  const auto* record = spell_record(reread);
  ASSERT_NE(record, nullptr);

  const auto first = fo4::schema::read_scoped_conditions(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 1u);
  EXPECT_EQ((*first)[0].function, 42u);

  const auto second = fo4::schema::read_scoped_conditions(*record, descriptor, 1);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->size(), 1u);
  EXPECT_EQ((*second)[0].function, 5u);
}

TEST(ConditionScopes, SetIntoEmptyEffectInsertsWithoutDisturbingSiblings) {
  auto plugin = make_spell({{0x00000801, {1}}, {0x00000802, {}}});
  const auto descriptor = spel_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  fo4::CtdaCondition added;
  added.function = 7;
  const std::vector<fo4::CtdaCondition> conditions{added};

  const auto status = fo4::schema::set_record_scoped_conditions(
      plugin, key, descriptor, 1, conditions);
  EXPECT_EQ(status, fo4::schema::ScopedConditionStatus::updated);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Cond.esp");
  const auto* record = spell_record(reread);
  ASSERT_NE(record, nullptr);

  const auto first = fo4::schema::read_scoped_conditions(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 1u);
  EXPECT_EQ((*first)[0].function, 1u);

  const auto second = fo4::schema::read_scoped_conditions(*record, descriptor, 1);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->size(), 1u);
  EXPECT_EQ((*second)[0].function, 7u);
}

TEST(ConditionScopes, ClearScopedConditionsLeavesSiblingsIntact) {
  auto plugin = make_spell({{0x00000801, {1, 2}}, {0x00000802, {5}}});
  const auto descriptor = spel_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  const std::vector<fo4::CtdaCondition> empty;
  const auto status = fo4::schema::set_record_scoped_conditions(
      plugin, key, descriptor, 0, empty);
  EXPECT_EQ(status, fo4::schema::ScopedConditionStatus::updated);

  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(fo4::schema::read_scoped_conditions(*record, descriptor, 0)->empty());
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 1)->size(), 1u);
}

TEST(ConditionScopes, ReportsIndexAndRecordFailures) {
  auto plugin = make_spell({{0x00000801, {1}}});
  const auto descriptor = spel_descriptor();
  const std::vector<fo4::CtdaCondition> empty;

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), descriptor, 4, empty),
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("00099A:Cond.esp"), descriptor, 0, empty),
            fo4::schema::ScopedConditionStatus::record_missing);
}

TEST(ConditionScopes, SegmentsEffectsWithoutLeadingEfid) {
  // Effect 1 has a null BaseEffect, so it emits no EFID (Mutagen WriteNullable).
  auto plugin = make_spell({
      {0x00000801, {1}, true, true},
      {0x00000000, {2}, false, true},
      {0x00000803, {3}, true, true},
  });
  const auto descriptor = spel_descriptor();
  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);

  ASSERT_TRUE(fo4::schema::read_scoped_conditions(*record, descriptor, 2).has_value());
  EXPECT_FALSE(fo4::schema::read_scoped_conditions(*record, descriptor, 3).has_value());
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 0)->at(0).function, 1u);
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 1)->at(0).function, 2u);
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 2)->at(0).function, 3u);

  fo4::CtdaCondition replacement;
  replacement.function = 55;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  const auto status = fo4::schema::set_record_scoped_conditions(
      plugin, fo4::FormKey::parse("000800:Cond.esp"), descriptor, 1, conditions);
  EXPECT_EQ(status, fo4::schema::ScopedConditionStatus::updated);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Cond.esp");
  const auto* rewritten = spell_record(reread);
  ASSERT_NE(rewritten, nullptr);
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*rewritten, descriptor, 0)->at(0).function, 1u);
  const auto middle = fo4::schema::read_scoped_conditions(*rewritten, descriptor, 1);
  ASSERT_EQ(middle->size(), 1u);
  EXPECT_EQ(middle->at(0).function, 55u);
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*rewritten, descriptor, 2)->at(0).function, 3u);
}

TEST(ConditionScopes, SegmentsCtdaOnlyLeadingEffect) {
  auto plugin = make_spell({
      {0x00000000, {9}, false, false},
      {0x00000802, {5}, true, true},
  });
  const auto descriptor = spel_descriptor();
  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);

  ASSERT_TRUE(fo4::schema::read_scoped_conditions(*record, descriptor, 1).has_value());
  EXPECT_FALSE(fo4::schema::read_scoped_conditions(*record, descriptor, 2).has_value());
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 0)->at(0).function, 9u);
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, descriptor, 1)->at(0).function, 5u);
}

