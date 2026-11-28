#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Subrecords = std::vector<std::pair<std::string, std::vector<std::byte>>>;

struct Effect {
  std::string effect;
  float magnitude{0.0f};
  std::uint32_t area{0};
  std::uint32_t duration{0};
  bool operator==(const Effect&) const = default;
};

Subrecords subrecords_of(const fo4::Plugin& plugin, std::string_view key) {
  Subrecords out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

std::vector<std::string> signatures_of(const fo4::Plugin& plugin, std::string_view key) {
  std::vector<std::string> out;
  for (const auto& [signature, data] : subrecords_of(plugin, key)) out.push_back(signature);
  return out;
}

std::string record_signature(const fo4::Plugin& plugin, std::string_view key) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  return record == nullptr ? std::string{} : record->header.signature.to_string();
}

std::string row(const std::string& signature, std::string_view leaf) {
  return signature + "\\" + std::string(leaf);
}

std::size_t list_size(const fo4::Plugin& plugin, std::string_view key, const std::string& path) {
  std::size_t size = 99;
  EXPECT_EQ(fo4::record_list_size(plugin, fo4::FormKey::parse(key), path, size), fo4::FieldStatus::ok);
  return size;
}

std::string field(const fo4::Plugin& plugin, std::string_view key, const std::string& path, std::size_t index) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  EXPECT_NE(record, nullptr);
  std::string text;
  if (record != nullptr) {
    EXPECT_EQ(fo4::read_record_field(plugin, *record, path, index, text), fo4::FieldStatus::ok);
  }
  return text;
}

std::vector<Effect> effects_of(const fo4::Plugin& plugin, std::string_view key) {
  const auto signature = record_signature(plugin, key);
  const auto size = list_size(plugin, key, row(signature, "Effects"));
  std::vector<Effect> out;
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back({field(plugin, key, row(signature, "EffectBaseEffect"), i),
                   std::stof(field(plugin, key, row(signature, "EffectMagnitude"), i)),
                   static_cast<std::uint32_t>(std::stoul(field(plugin, key, row(signature, "EffectArea"), i))),
                   static_cast<std::uint32_t>(std::stoul(field(plugin, key, row(signature, "EffectDuration"), i)))});
  }
  return out;
}

void put_effect(fo4::Plugin& plugin, std::string_view key, std::size_t index, const Effect& effect) {
  const auto form_key = fo4::FormKey::parse(key);
  const auto signature = record_signature(plugin, key);
  EXPECT_EQ(fo4::write_record_field(plugin, form_key, row(signature, "EffectBaseEffect"), index, effect.effect),
            fo4::FieldStatus::ok);
  EXPECT_EQ(fo4::write_record_field(plugin, form_key, row(signature, "EffectMagnitude"), index,
                                    std::format("{}", effect.magnitude)),
            fo4::FieldStatus::ok);
  EXPECT_EQ(fo4::write_record_field(plugin, form_key, row(signature, "EffectArea"), index,
                                    std::to_string(effect.area)),
            fo4::FieldStatus::ok);
  EXPECT_EQ(fo4::write_record_field(plugin, form_key, row(signature, "EffectDuration"), index,
                                    std::to_string(effect.duration)),
            fo4::FieldStatus::ok);
}

void add_effect(fo4::Plugin& plugin, std::string_view key, const Effect& effect) {
  const auto signature = record_signature(plugin, key);
  const auto at = list_size(plugin, key, row(signature, "Effects"));
  ASSERT_EQ(fo4::insert_record_list_element(plugin, fo4::FormKey::parse(key), row(signature, "Effects"), at, nullptr),
            fo4::FieldStatus::ok);
  put_effect(plugin, key, at, effect);
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

  const auto ingr_group = fo4::test::begin_group(out, "INGR", 0);
  auto ingredient = fo4::test::begin_record(out, "INGR", 0, 0x00000850, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Ingredient"));
  write_effect(out, 0x00000803, 4.0f, 1, 6);
  fo4::test::end_record(out, ingredient);
  fo4::test::end_group(out, ingr_group);
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

// Detects: the Effects group and the EffectMagnitude rows must count the same effects, so a mismatch or an EFIT value read from the wrong slot is caught.
TEST(MagicEffects, ReadsSpellEffectsThroughTheGenericRows) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto effects = effects_of(plugin, "000800:Fx.esp");
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0], (Effect{"000801:Fx.esp", 1.5f, 2u, 3u}));
  EXPECT_EQ(effects[1], (Effect{"000802:Fx.esp", 2.5f, 0u, 10u}));
  EXPECT_EQ(list_size(plugin, "000800:Fx.esp", "SPEL\\EffectMagnitude"), 2u);
  EXPECT_EQ(list_size(plugin, "000811:Fx.esp", "ALCH\\Effects"), 0u);
}

// Detects: ENCH, INGR, SPEL or ALCH losing its Effects group or flat rows, which must stay so an effect can be inserted, edited and removed on every host.
TEST(MagicEffects, EveryEffectHostInsertsEditsAndRemovesAnEffect) {
  for (const auto key : {"000800:Fx.esp", "000810:Fx.esp", "000820:Fx.esp", "000850:Fx.esp"}) {
    SCOPED_TRACE(key);
    auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
    const auto before = effects_of(plugin, key);
    const Effect added{"000803:Fx.esp", 7.5f, 3u, 8u};
    add_effect(plugin, key, added);
    auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
    const auto after = effects_of(reread, key);
    ASSERT_EQ(after.size(), before.size() + 1);
    EXPECT_EQ(after.back(), added);

    const auto signature = record_signature(reread, key);
    EXPECT_EQ(fo4::remove_record_list_element(reread, fo4::FormKey::parse(key), row(signature, "Effects"),
                                              before.size()),
              fo4::FieldStatus::ok);
    EXPECT_EQ(effects_of(reread, key), before);
  }
}

// Detects: an effect added to a record with no effect block that must land at the end of the record, not ahead of the earlier subrecords.
TEST(MagicEffects, InsertAppendsAtRecordEndWhenRecordHasNoEffects) {
  for (const auto key : {"000820:Fx.esp", "000811:Fx.esp"}) {
    SCOPED_TRACE(key);
    auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
    const auto before = subrecords_of(plugin, key);
    add_effect(plugin, key, {"000801:Fx.esp", 2.0f, 0u, 5u});

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
    const auto after = subrecords_of(reread, key);
    ASSERT_EQ(after.size(), before.size() + 2);
    EXPECT_EQ(Subrecords(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size())), before);
    EXPECT_EQ(after[before.size()].first, "EFID");
    EXPECT_EQ(after.back().first, "EFIT");
    EXPECT_EQ(effects_of(reread, key), (std::vector<Effect>{{"000801:Fx.esp", 2.0f, 0u, 5u}}));
  }
}

// Detects: an effect appended after the last effect that must land past that effect's CTDA run, not between its EFIT and its conditions.
TEST(MagicEffects, InsertAfterTheLastEffectLandsPastItsConditions) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  add_effect(plugin, "000800:Fx.esp", {"000803:Fx.esp", 7.0f, 3u, 8u});

  const auto after = subrecords_of(plugin, "000800:Fx.esp");
  ASSERT_EQ(after.size(), before.size() + 2);
  EXPECT_EQ(Subrecords(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(before.size())), before);
  EXPECT_EQ(after[before.size()].first, "EFID");
  EXPECT_EQ(after.back().first, "EFIT");
  const auto effects = effects_of(plugin, "000800:Fx.esp");
  ASSERT_EQ(effects.size(), 3u);
  EXPECT_EQ(effects[2], (Effect{"000803:Fx.esp", 7.0f, 3u, 8u}));
}

// Detects: an insert before an existing effect that must land ahead of that effect's EFID, without splitting the earlier effect or reordering the rest.
TEST(MagicEffects, InsertBeforeAnEffectLandsAheadOfItsLeader) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = "000800:Fx.esp";
  const auto before = subrecords_of(plugin, key);
  ASSERT_EQ(fo4::insert_record_list_element(plugin, fo4::FormKey::parse(key), "SPEL\\Effects", 1, nullptr),
            fo4::FieldStatus::ok);
  put_effect(plugin, key, 1, {"000803:Fx.esp", 9.0f, 0u, 1u});

  const auto effects = effects_of(plugin, key);
  ASSERT_EQ(effects.size(), 3u);
  EXPECT_EQ(effects[0], (Effect{"000801:Fx.esp", 1.5f, 2u, 3u}));
  EXPECT_EQ(effects[1], (Effect{"000803:Fx.esp", 9.0f, 0u, 1u}));
  EXPECT_EQ(effects[2], (Effect{"000802:Fx.esp", 2.5f, 0u, 10u}));
  const auto after = subrecords_of(plugin, key);
  ASSERT_EQ(after.size(), before.size() + 2);
  EXPECT_EQ(Subrecords(after.begin() + 5, after.end()), Subrecords(before.begin() + 3, before.end()));
}

// Detects: editing an effect through the rows that must keep its CTDA run, which the retired whole-entry set rewrote without conditions.
TEST(MagicEffects, RowWritesKeepTheEffectConditions) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = "000800:Fx.esp";
  const auto before = signatures_of(plugin, key);
  put_effect(plugin, key, 1, {"000803:Fx.esp", 9.0f, 4u, 1u});

  EXPECT_EQ(signatures_of(plugin, key), before);
  const auto effects = effects_of(plugin, key);
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0], (Effect{"000801:Fx.esp", 1.5f, 2u, 3u}));
  EXPECT_EQ(effects[1], (Effect{"000803:Fx.esp", 9.0f, 4u, 1u}));
}

// Detects: a removed effect that must take its CTDA and CIS1 with it, so they do not attach to the previous effect.
TEST(MagicEffects, RemoveTakesTheEffectAndItsConditions) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto before = subrecords_of(plugin, "000800:Fx.esp");
  ASSERT_GT(before.size(), 5u);

  EXPECT_EQ(fo4::remove_record_list_element(plugin, key, "SPEL\\Effects", 1), fo4::FieldStatus::ok);
  EXPECT_EQ(subrecords_of(plugin, "000800:Fx.esp"), Subrecords(before.begin(), before.begin() + 3));

  auto other = fo4::read_plugin(effects_plugin(), "Fx.esp");
  EXPECT_EQ(fo4::remove_record_list_element(other, key, "SPEL\\Effects", 0), fo4::FieldStatus::ok);
  Subrecords expected{before.front()};
  expected.insert(expected.end(), before.begin() + 3, before.end());
  EXPECT_EQ(subrecords_of(other, "000800:Fx.esp"), expected);
  EXPECT_EQ(effects_of(other, "000800:Fx.esp"), (std::vector<Effect>{{"000802:Fx.esp", 2.5f, 0u, 10u}}));
}

// Detects: removing the last effect that must keep the subrecords ahead of the effect block.
TEST(MagicEffects, RemovingEveryEffectKeepsEarlierSubrecords) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");
  const auto edid = subrecords_of(plugin, "000800:Fx.esp").front();
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key, "SPEL\\Effects", 1), fo4::FieldStatus::ok);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key, "SPEL\\Effects", 0), fo4::FieldStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  EXPECT_EQ(subrecords_of(reread, "000800:Fx.esp"), (Subrecords{edid}));
  EXPECT_TRUE(effects_of(reread, "000800:Fx.esp").empty());
}

// Detects: a SPEL whose only subrecord after the effect is a DESC that must leave with the effect, since the greedy group owns every subrecord up to the next leader and a refactor that narrows the span would strand the DESC.
TEST(MagicEffects, RemovingTheLastEffectTakesTheTrailingSubrecordsOfTheGreedyGroup) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000840:Fx.esp");
  EXPECT_EQ(signatures_of(plugin, "000840:Fx.esp"), (std::vector<std::string>{"EFID", "EFIT", "DESC"}));
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key, "SPEL\\Effects", 0), fo4::FieldStatus::ok);
  EXPECT_TRUE(signatures_of(plugin, "000840:Fx.esp").empty());
  EXPECT_EQ(list_size(plugin, "000840:Fx.esp", "SPEL\\Effects"), 0u);
}

// Detects: an out-of-range index, a missing record or a record without effects that must be refused without mutating the plugin.
TEST(MagicEffects, RefusesBadIndexMissingAndUnsupportedRecordsWithoutMutation) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto spell = fo4::FormKey::parse("000800:Fx.esp");

  EXPECT_EQ(fo4::remove_record_list_element(plugin, spell, "SPEL\\Effects", 2), fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, spell, "SPEL\\Effects", 99), fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_list_element(plugin, spell, "SPEL\\Effects", 3, nullptr),
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(fo4::write_record_field(plugin, spell, "SPEL\\EffectMagnitude", 2, "1"),
            fo4::FieldStatus::occurrence_missing);

  const auto missing = fo4::FormKey::parse("000999:Fx.esp");
  EXPECT_EQ(fo4::insert_record_list_element(plugin, missing, "SPEL\\Effects", 0, nullptr),
            fo4::FieldStatus::record_missing);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, missing, "SPEL\\Effects", 0), fo4::FieldStatus::record_missing);

  const auto weapon = fo4::FormKey::parse("000830:Fx.esp");
  EXPECT_EQ(fo4::insert_record_list_element(plugin, weapon, "SPEL\\Effects", 0, nullptr),
            fo4::FieldStatus::record_mismatch);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, weapon, "SPEL\\Effects", 0), fo4::FieldStatus::record_mismatch);

  EXPECT_EQ(fo4::insert_record_list_element(plugin, spell, "SPEL\\EffectMagnitude", 0, nullptr),
            fo4::FieldStatus::wrong_kind);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, spell, "SPEL\\EffectBaseEffect", 0), fo4::FieldStatus::wrong_kind);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Detects: a malformed effect block that must read as a mismatch, since the greedy group counts EFID leaders only and a reader compares it with the EFIT row count.
TEST(MagicEffects, MalformedEffectBlocksDisagreeWithTheEfitRowCount) {
  {
    auto plugin = fo4::read_plugin(spell_plugin([](fo4::test::ByteWriter& out) {
                                     write_sized_subrecord(out, "EFID", 4);
                                   }),
                                   "Fx.esp");
    EXPECT_EQ(list_size(plugin, "000800:Fx.esp", "SPEL\\Effects"), 1u);
    EXPECT_EQ(list_size(plugin, "000800:Fx.esp", "SPEL\\EffectMagnitude"), 0u);
  }
  {
    auto plugin = fo4::read_plugin(spell_plugin([](fo4::test::ByteWriter& out) {
                                     write_sized_subrecord(out, "EFID", 5);
                                     write_sized_subrecord(out, "EFIT", 12);
                                   }),
                                   "Fx.esp");
    std::size_t size = 0;
    EXPECT_THROW(static_cast<void>(fo4::record_list_size(plugin, fo4::FormKey::parse("000800:Fx.esp"),
                                                         "SPEL\\Effects", size)),
                 fo4::schema::SubrecordGroupError);
  }
}

// Detects: an edited effect list that must survive a save and reload in the order insert, edit and remove left it.
TEST(MagicEffects, EditedListSurvivesSaveAndReload) {
  auto plugin = fo4::read_plugin(effects_plugin(), "Fx.esp");
  const auto key = fo4::FormKey::parse("000800:Fx.esp");

  add_effect(plugin, "000800:Fx.esp", {"000803:Fx.esp", 4.0f, 1u, 6u});
  put_effect(plugin, "000800:Fx.esp", 0, {"000802:Fx.esp", 5.0f, 2u, 4u});
  EXPECT_EQ(fo4::remove_record_list_element(plugin, key, "SPEL\\Effects", 1), fo4::FieldStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  const auto effects = effects_of(reread, "000800:Fx.esp");
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0], (Effect{"000802:Fx.esp", 5.0f, 2u, 4u}));
  EXPECT_EQ(effects[1], (Effect{"000803:Fx.esp", 4.0f, 1u, 6u}));
}

// Detects: a master-owned effect that must encode its master index, and an owner that is not a master that must be refused instead of written into the EFID.
TEST(MagicEffects, MasterOwnedEffectEncodesItsMasterIndexAndUndeclaredOwnerIsRefused) {
  auto plugin = fo4::read_plugin(master_owned_effect_plugin(), "Fx.esp");
  add_effect(plugin, "000800:Fx.esp", {"000801:Fallout4.esm", 2.0f, 0u, 3u});

  const auto subrecords = subrecords_of(plugin, "000800:Fx.esp");
  ASSERT_EQ(subrecords.size(), 3u);
  ASSERT_EQ(subrecords[1].first, "EFID");
  EXPECT_EQ(subrecords[1].second, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x08}, std::byte{0x00},
                                                          std::byte{0x00}}));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Fx.esp");
  EXPECT_EQ(effects_of(reread, "000800:Fx.esp"),
            (std::vector<Effect>{{"000801:Fallout4.esm", 2.0f, 0u, 3u}}));

  const auto before = fo4::write_plugin(plugin);
  EXPECT_ANY_THROW(static_cast<void>(fo4::write_record_field(plugin, fo4::FormKey::parse("000800:Fx.esp"),
                                                            "SPEL\\EffectBaseEffect", 0, "000801:Other.esm")));
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
