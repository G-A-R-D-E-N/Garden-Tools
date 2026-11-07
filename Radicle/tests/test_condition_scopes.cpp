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
#include <string>
#include <string_view>
#include <utility>
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

std::optional<std::vector<fo4::CtdaCondition>> read_opt(
    const fo4::Record& record,
    const fo4::schema::ConditionScopeDescriptor& descriptor, std::size_t index) {
  const auto result = fo4::schema::read_scoped_conditions(record, descriptor, index);
  if (!result.ok) return std::nullopt;
  return result.conditions;
}

fo4::schema::ConditionScopeDescriptor qust_descriptor() {
  const auto path = fo4::schema::scoped_condition_path(
      fo4::Signature::from_string("QUST"), "Aliases[0].Conditions");
  return path->descriptor;
}

enum class AliasKind { reference, location, collection };

struct AliasSpec {
  AliasKind kind;
  std::uint32_t id;
  std::vector<std::uint16_t> condition_functions;
  bool with_almi = false;
  std::vector<std::string> post_condition_fields = {};
};

void write_u32_subrecord(fo4::test::ByteWriter& out, std::string_view signature,
                         std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

// dialog_functions become record-level DialogConditions CTDA (before ANAM), which
// the alias scanner must never touch.
fo4::Plugin make_quest(const std::vector<std::uint16_t>& dialog_functions,
                       const std::vector<AliasSpec>& aliases) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto record = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondQuest\0", 10});
  for (const auto function : dialog_functions) {
    fo4::test::write_subrecord(out, "CTDA", make_ctda(function, 0).bytes);
  }
  write_u32_subrecord(out, "ANAM", static_cast<std::uint32_t>(aliases.size()));
  for (const auto& alias : aliases) {
    if (alias.kind == AliasKind::collection) {
      write_u32_subrecord(out, "ALCS", alias.id);
      if (alias.with_almi) {
        fo4::test::ByteWriter almi;
        almi.u8(1);
        fo4::test::write_subrecord(out, "ALMI", almi.bytes);
      }
      continue;
    }
    write_u32_subrecord(out, alias.kind == AliasKind::reference ? "ALST" : "ALLS",
                        alias.id);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"A\0", 2});
    for (const auto function : alias.condition_functions) {
      fo4::test::write_subrecord(out, "CTDA", make_ctda(function, 0).bytes);
    }
    for (const auto& field : alias.post_condition_fields) {
      write_u32_subrecord(out, field, 0);
    }
    fo4::test::write_subrecord(out, "ALED", std::span<const std::byte>{});
  }
  fo4::test::write_subrecord(out, "NNAM", std::string_view{"\0", 1});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* quest_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

std::optional<std::size_t> first_index_of(const fo4::Record& record,
                                          std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return std::nullopt;
}

fo4::Plugin make_quest_raw(
    const std::vector<std::pair<std::string, std::vector<std::byte>>>& body) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto record = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondQuest\0", 10});
  for (const auto& [signature, data] : body) {
    fo4::test::write_subrecord(out, signature, std::span<const std::byte>{data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter writer;
  writer.u32(value);
  return writer.bytes;
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

  for (const auto path : {" Effects[0].Conditions ", "Effects[ 0 ].Conditions",
                          "Effects[0] . Conditions", "Effects[0]. Conditions "}) {
    const auto spaced = fo4::schema::scoped_condition_path(
        fo4::Signature::from_string("SPEL"), path);
    ASSERT_TRUE(spaced.has_value()) << path;
    EXPECT_EQ(spaced->container_index, 0u);
  }
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
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects [0].Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[-1].Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0.Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0]].Conditions").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0].Conditions.more").has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(spel, "Effects[0]..Conditions").has_value());
}

TEST(ConditionScopes, ReadsScopedConditionsPerEffect) {
  auto plugin = make_spell({{0x00000801, {1, 2}}, {0x00000802, {5}}, {0x00000803, {}}});
  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto descriptor = spel_descriptor();

  const auto first = read_opt(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 2u);
  EXPECT_EQ((*first)[0].function, 1u);
  EXPECT_EQ((*first)[1].function, 2u);

  const auto second = read_opt(*record, descriptor, 1);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->size(), 1u);
  EXPECT_EQ((*second)[0].function, 5u);

  const auto third = read_opt(*record, descriptor, 2);
  ASSERT_TRUE(third.has_value());
  EXPECT_TRUE(third->empty());

  EXPECT_FALSE(read_opt(*record, descriptor, 3).has_value());
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

  const auto first = read_opt(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 1u);
  EXPECT_EQ((*first)[0].function, 42u);

  const auto second = read_opt(*record, descriptor, 1);
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

  const auto first = read_opt(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 1u);
  EXPECT_EQ((*first)[0].function, 1u);

  const auto second = read_opt(*record, descriptor, 1);
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
  EXPECT_TRUE(read_opt(*record, descriptor, 0)->empty());
  EXPECT_EQ(read_opt(*record, descriptor, 1)->size(), 1u);
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

  ASSERT_TRUE(read_opt(*record, descriptor, 2).has_value());
  EXPECT_FALSE(read_opt(*record, descriptor, 3).has_value());
  EXPECT_EQ(read_opt(*record, descriptor, 0)->at(0).function, 1u);
  EXPECT_EQ(read_opt(*record, descriptor, 1)->at(0).function, 2u);
  EXPECT_EQ(read_opt(*record, descriptor, 2)->at(0).function, 3u);

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
  EXPECT_EQ(read_opt(*rewritten, descriptor, 0)->at(0).function, 1u);
  const auto middle = read_opt(*rewritten, descriptor, 1);
  ASSERT_EQ(middle->size(), 1u);
  EXPECT_EQ(middle->at(0).function, 55u);
  EXPECT_EQ(read_opt(*rewritten, descriptor, 2)->at(0).function, 3u);
}

TEST(ConditionScopes, SegmentsCtdaOnlyLeadingEffect) {
  auto plugin = make_spell({
      {0x00000000, {9}, false, false},
      {0x00000802, {5}, true, true},
  });
  const auto descriptor = spel_descriptor();
  const auto* record = spell_record(plugin);
  ASSERT_NE(record, nullptr);

  ASSERT_TRUE(read_opt(*record, descriptor, 1).has_value());
  EXPECT_FALSE(read_opt(*record, descriptor, 2).has_value());
  EXPECT_EQ(read_opt(*record, descriptor, 0)->at(0).function, 9u);
  EXPECT_EQ(read_opt(*record, descriptor, 1)->at(0).function, 5u);
}

TEST(ConditionScopes, AliasScannerSegmentsInterleavedAliasesWithIndexParity) {
  auto plugin = make_quest(
      {100},
      {
          {AliasKind::reference, 0, {1}},
          {AliasKind::location, 1, {2, 3}},
          {AliasKind::collection, 2, {}, true},
          {AliasKind::reference, 3, {}},
      });
  const auto descriptor = qust_descriptor();
  const auto* record = quest_record(plugin);
  ASSERT_NE(record, nullptr);

  const auto first = read_opt(*record, descriptor, 0);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 1u);
  EXPECT_EQ((*first)[0].function, 1u);

  const auto second = read_opt(*record, descriptor, 1);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(second->size(), 2u);
  EXPECT_EQ((*second)[0].function, 2u);
  EXPECT_EQ((*second)[1].function, 3u);

  const auto collection = fo4::schema::read_scoped_conditions(*record, descriptor, 2);
  EXPECT_FALSE(collection.ok);
  EXPECT_EQ(collection.error, fo4::schema::ScopedConditionStatus::element_not_conditioned);

  const auto empty_ref = read_opt(*record, descriptor, 3);
  ASSERT_TRUE(empty_ref.has_value());
  EXPECT_TRUE(empty_ref->empty());

  const auto oob = fo4::schema::read_scoped_conditions(*record, descriptor, 4);
  EXPECT_FALSE(oob.ok);
  EXPECT_EQ(oob.error, fo4::schema::ScopedConditionStatus::container_index_out_of_range);
}

TEST(ConditionScopes, MultipleContiguousAlcsCollapseToOneAliasIndex) {
  auto plugin = make_quest(
      {},
      {
          {AliasKind::reference, 0, {1}},
          {AliasKind::collection, 10, {}, true},
          {AliasKind::collection, 11, {}, true},
          {AliasKind::reference, 2, {5}},
          {AliasKind::location, 3, {6}},
      });
  const auto descriptor = qust_descriptor();
  const auto* record = quest_record(plugin);
  ASSERT_NE(record, nullptr);

  EXPECT_EQ(read_opt(*record, descriptor, 0)->at(0).function, 1u);

  const auto collection = fo4::schema::read_scoped_conditions(*record, descriptor, 1);
  EXPECT_FALSE(collection.ok);
  EXPECT_EQ(collection.error, fo4::schema::ScopedConditionStatus::element_not_conditioned);

  EXPECT_EQ(read_opt(*record, descriptor, 2)->at(0).function, 5u);
  EXPECT_EQ(read_opt(*record, descriptor, 3)->at(0).function, 6u);
  EXPECT_FALSE(fo4::schema::read_scoped_conditions(*record, descriptor, 4).ok);
}

TEST(ConditionScopes, SetAliasConditionsLeavesSiblingsAndDialogConditionsIntact) {
  auto plugin = make_quest(
      {100},
      {
          {AliasKind::reference, 0, {1}},
          {AliasKind::location, 1, {2, 3}},
          {AliasKind::collection, 2, {}},
          {AliasKind::reference, 3, {8}},
      });
  const auto descriptor = qust_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  fo4::CtdaCondition replacement;
  replacement.function = 55;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Cond.esp");
  const auto* record = quest_record(reread);
  ASSERT_NE(record, nullptr);

  EXPECT_EQ(read_opt(*record, descriptor, 0)->at(0).function, 1u);
  const auto middle = read_opt(*record, descriptor, 1);
  ASSERT_EQ(middle->size(), 1u);
  EXPECT_EQ(middle->at(0).function, 55u);
  EXPECT_EQ(read_opt(*record, descriptor, 3)->at(0).function, 8u);

  bool dialog_condition_present = false;
  const auto alias_begin = std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& member) {
    return member.signature == fo4::Signature::from_string("ANAM");
  });
  for (const auto& condition : fo4::read_ctda_conditions_in(
           *record, 0, static_cast<std::size_t>(alias_begin - record->subrecords.begin()))) {
    if (condition.function == 100u) dialog_condition_present = true;
  }
  EXPECT_TRUE(dialog_condition_present);
}

TEST(ConditionScopes, InsertsConditionsAtCanonicalPositionForReferenceAlias) {
  auto plugin = make_quest(
      {},
      {
          {AliasKind::reference, 0, {}, false, {"KWDA"}},
      });
  const auto descriptor = qust_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  fo4::CtdaCondition added;
  added.function = 7;
  const std::vector<fo4::CtdaCondition> conditions{added};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 0, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto* record = quest_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto ctda = first_index_of(*record, "CTDA");
  const auto kwda = first_index_of(*record, "KWDA");
  ASSERT_TRUE(ctda.has_value());
  ASSERT_TRUE(kwda.has_value());
  EXPECT_LT(*ctda, *kwda);
  EXPECT_EQ(read_opt(*record, descriptor, 0)->at(0).function, 7u);
}

TEST(ConditionScopes, InsertsConditionsBeforeAlccForLocationAlias) {
  auto plugin = make_quest(
      {},
      {
          {AliasKind::location, 0, {}, false, {"ALCC"}},
      });
  const auto descriptor = qust_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  fo4::CtdaCondition added;
  added.function = 7;
  const std::vector<fo4::CtdaCondition> conditions{added};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 0, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto* record = quest_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto ctda = first_index_of(*record, "CTDA");
  const auto alcc = first_index_of(*record, "ALCC");
  ASSERT_TRUE(ctda.has_value());
  ASSERT_TRUE(alcc.has_value());
  EXPECT_LT(*ctda, *alcc);
}

TEST(ConditionScopes, CollectionAliasRejectsConditionAccess) {
  auto plugin = make_quest(
      {},
      {
          {AliasKind::reference, 0, {1}},
          {AliasKind::collection, 1, {}},
      });
  const auto descriptor = qust_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  const auto read = fo4::schema::read_scoped_conditions(*quest_record(plugin), descriptor, 1);
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::element_not_conditioned);

  fo4::CtdaCondition added;
  added.function = 9;
  const std::vector<fo4::CtdaCondition> conditions{added};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::element_not_conditioned);

  const std::vector<fo4::CtdaCondition> empty;
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 1, empty),
            fo4::schema::ScopedConditionStatus::element_not_conditioned);
}

TEST(ConditionScopes, MalformedAliasStructureFailsClosed) {
  const auto descriptor = qust_descriptor();
  const auto key = fo4::FormKey::parse("000800:Cond.esp");
  const std::vector<fo4::CtdaCondition> empty;

  const std::vector<std::vector<std::pair<std::string, std::vector<std::byte>>>> malformed = {
      {{"ANAM", u32_bytes(1)}, {"ALST", u32_bytes(0)}, {"ALID", {std::byte{'A'}, std::byte{0}}},
       {"CTDA", make_ctda(1, 0).bytes}},
      {{"ANAM", u32_bytes(1)}, {"ALCS", u32_bytes(0)}, {"ALMI", {std::byte{0}, std::byte{0}}}},
      {{"ANAM", u32_bytes(1)}, {"ANAM", u32_bytes(2)}},
      {{"ALST", u32_bytes(0)}, {"ALID", {std::byte{'A'}, std::byte{0}}}, {"ALED", {}}},
      {{"ANAM", u32_bytes(1)}, {"ALST", {std::byte{0}, std::byte{0}}},
       {"ALID", {std::byte{'A'}, std::byte{0}}}, {"ALED", {}}},
      {{"ANAM", u32_bytes(1)}, {"ALCS", {std::byte{0}, std::byte{0}}}},
      {{"ANAM", u32_bytes(1)}, {"ALST", u32_bytes(0)}, {"ALID", {std::byte{'A'}, std::byte{0}}},
       {"ALED", {}}, {"NNAM", {std::byte{0}}}, {"ALST", u32_bytes(0)},
       {"ALID", {std::byte{'A'}, std::byte{0}}}, {"ALED", {}}},
  };

  for (const auto& body : malformed) {
    auto plugin = make_quest_raw(body);
    const auto* record = quest_record(plugin);
    ASSERT_NE(record, nullptr);
    const auto read = fo4::schema::read_scoped_conditions(*record, descriptor, 0);
    EXPECT_FALSE(read.ok);
    EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::alias_structure_invalid);
    EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, descriptor, 0, empty),
              fo4::schema::ScopedConditionStatus::alias_structure_invalid);
  }
}

TEST(ConditionScopes, RefusesScopeHoldingSeveralConditionLists) {
  auto plugin = make_quest_raw({
      {"ANAM", u32_bytes(1)},
      {"ALST", u32_bytes(0)},
      {"ALID", {std::byte{'A'}, std::byte{0}}},
      {"CTDA", make_ctda(1, 0).bytes},
      {"ALFA", u32_bytes(0)},
      {"CTDA", make_ctda(2, 0).bytes},
      {"ALED", {}},
      {"NNAM", {std::byte{0}}},
  });
  const auto before = plugin;
  const auto* record = quest_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto read = fo4::schema::read_scoped_conditions(*record, qust_descriptor(), 0);
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::multiple_lists);
  EXPECT_TRUE(read.conditions.empty());
  fo4::CtdaCondition replacement;
  replacement.function = 42;
  const std::vector<fo4::CtdaCondition> conditions{replacement};

  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), qust_descriptor(), 0, conditions),
            fo4::schema::ScopedConditionStatus::multiple_lists);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(before));
}
