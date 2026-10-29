#include "fo4/forms/faction.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
  bool localized = false;
  bool short_relation = false;
  bool titles_before_rank = false;
  std::uint32_t first_reaction = 1;
};

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> relation_bytes(std::uint32_t form_id,
                                      std::int32_t modifier,
                                      std::uint32_t reaction,
                                      std::size_t size = 12) {
  fo4::test::ByteWriter out;
  out.u32(form_id);
  out.i32(modifier);
  out.u32(reaction);
  out.bytes.resize(size);
  return out.bytes;
}

void write_title(fo4::test::ByteWriter& out,
                 std::string_view signature,
                 std::string_view text,
                 std::uint32_t id,
                 bool localized) {
  if (localized) {
    fo4::test::write_subrecord(out, signature, u32_bytes(id));
    return;
  }
  std::string value(text);
  value.push_back('\0');
  fo4::test::write_subrecord(out, signature, std::string_view{value});
}

std::vector<std::byte> make_plugin(const Options& options = {}) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", options.localized ? 0x80u : 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MagicMaster.esp\0", 16});
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "FACT", 0);
  const auto record = fo4::test::begin_record(out, "FACT", 0, 0x02000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"RadicleFaction\0", 16});
  write_title(out, "FULL", "Garden Faction", 0x42, options.localized);
  fo4::test::write_subrecord(out, "XNAM",
                             relation_bytes(0x01000123, -5, options.first_reaction,
                                            options.short_relation ? 8 : 12));
  fo4::test::write_subrecord(out, "XNAM", relation_bytes(0x02000800, 20, 2));
  fo4::test::write_subrecord(out, "DATA", u32_bytes(3));
  fo4::test::ByteWriter crime;
  crime.u8(1);
  crime.u8(0);
  crime.u16(10);
  crime.u16(20);
  crime.u16(30);
  crime.u16(40);
  crime.u16(50);
  crime.f32(1.5f);
  crime.u16(60);
  crime.u16(70);
  fo4::test::write_subrecord(out, "CRVA", crime.bytes);
  if (options.titles_before_rank) write_title(out, "MNAM", "Early", 0x77, options.localized);

  fo4::test::write_subrecord(out, "RNAM", u32_bytes(0));
  write_title(out, "MNAM", "Recruit", 0x101, options.localized);
  write_title(out, "FNAM", "Recruit F", 0x102, options.localized);
  fo4::test::write_subrecord(out, "RNAM", u32_bytes(1));
  write_title(out, "MNAM", "Sergeant", 0x103, options.localized);
  fo4::test::write_subrecord(out, "RNAM", u32_bytes(2));
  write_title(out, "FNAM", "Captain F", 0x104, options.localized);
  const std::array<std::byte, 2> insignia{std::byte{0x41}, std::byte{0x00}};
  fo4::test::write_subrecord(out, "INAM", insignia);

  fo4::test::ByteWriter vendor;
  vendor.u16(8);
  vendor.u16(20);
  vendor.u16(300);
  vendor.u16(0);
  vendor.u8(1);
  vendor.u8(0);
  vendor.u8(1);
  vendor.u8(0);
  fo4::test::write_subrecord(out, "VENV", vendor.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::FormKey key() { return fo4::FormKey::parse("000800:Patch.esp"); }

void expect_relation_eq(const fo4::forms::FactionRelation& actual,
                        const fo4::forms::FactionRelation& expected) {
  EXPECT_EQ(actual.faction, expected.faction);
  EXPECT_EQ(actual.modifier, expected.modifier);
  EXPECT_EQ(actual.reaction, expected.reaction);
}

fo4::forms::FactionRank make_rank(std::uint32_t number, std::string_view male) {
  fo4::forms::FactionRank rank;
  rank.number = number;
  fo4::schema::LocalizedString title;
  title.text = std::string(male);
  rank.male_title = title;
  return rank;
}

std::vector<std::byte> make_faction_plugin(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> zstring_bytes(std::string_view value) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::string> signature_names(const fo4::Record& record) {
  std::vector<std::string> names;
  for (const auto& subrecord : record.subrecords) names.push_back(subrecord.signature.to_string());
  return names;
}

}  // namespace

TEST(FactionSynthetic, DecodesRelationsInFileOrder) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto relations = fo4::forms::decode_faction_relations(plugin, key());
  ASSERT_EQ(relations.size(), 2u);
  EXPECT_EQ(relations[0].faction, fo4::FormKey::parse("000123:MagicMaster.esp"));
  EXPECT_EQ(relations[0].modifier, -5);
  EXPECT_EQ(relations[0].reaction, fo4::forms::FactionCombatReaction::enemy);
  EXPECT_EQ(relations[1].faction, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(relations[1].modifier, 20);
  EXPECT_EQ(relations[1].reaction, fo4::forms::FactionCombatReaction::ally);
}

TEST(FactionSynthetic, DecodesRanksWithTitlesAndRawInsignia) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto ranks = fo4::forms::decode_faction_ranks(plugin, key());
  ASSERT_EQ(ranks.size(), 3u);
  EXPECT_EQ(ranks[0].number, 0u);
  ASSERT_TRUE(ranks[0].male_title.has_value());
  EXPECT_EQ(ranks[0].male_title->inline_text(), "Recruit");
  ASSERT_TRUE(ranks[0].female_title.has_value());
  EXPECT_EQ(ranks[0].female_title->inline_text(), "Recruit F");
  EXPECT_EQ(ranks[1].number, 1u);
  EXPECT_FALSE(ranks[1].female_title.has_value());
  ASSERT_TRUE(ranks[1].male_title.has_value());
  EXPECT_EQ(ranks[1].male_title->inline_text(), "Sergeant");
  EXPECT_FALSE(ranks[2].male_title.has_value());
  EXPECT_EQ(ranks[2].female_title->inline_text(), "Captain F");
  ASSERT_TRUE(ranks[2].insignia.has_value());
  EXPECT_EQ(*ranks[2].insignia, (std::vector<std::byte>{std::byte{0x41}, std::byte{0x00}}));
}

TEST(FactionSynthetic, AddsSetsAndRemovesRelationsByIndex) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::forms::FactionRelation added;
  added.faction = fo4::FormKey::parse("000123:MagicMaster.esp");
  added.modifier = 7;
  added.reaction = fo4::forms::FactionCombatReaction::friendly;
  fo4::forms::add_faction_relation(plugin, key(), added);

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  auto relations = fo4::forms::decode_faction_relations(reread, key());
  ASSERT_EQ(relations.size(), 3u);
  EXPECT_EQ(relations[2].modifier, 7);
  EXPECT_EQ(relations[2].reaction, fo4::forms::FactionCombatReaction::friendly);

  fo4::forms::FactionRelation replaced = added;
  replaced.modifier = -1;
  fo4::forms::set_faction_relation(reread, key(), 0, replaced);
  fo4::forms::remove_faction_relation(reread, key(), 1);

  relations = fo4::forms::decode_faction_relations(reread, key());
  ASSERT_EQ(relations.size(), 2u);
  expect_relation_eq(relations[0], replaced);
  expect_relation_eq(relations[1], added);
}

TEST(FactionSynthetic, RelationTargetOfThisPluginMustBeFactOrRace) {
  fo4::test::ByteWriter out;
  out.bytes = make_plugin();
  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x02000900);
  fo4::test::end_record(out, npc);
  fo4::test::end_group(out, group);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  fo4::forms::FactionRelation to_npc;
  to_npc.faction = fo4::FormKey::parse("000900:Patch.esp");
  to_npc.reaction = fo4::forms::FactionCombatReaction::friendly;
  EXPECT_THROW(fo4::forms::add_faction_relation(plugin, key(), to_npc), fo4::forms::FactionError);
  EXPECT_THROW(fo4::forms::set_faction_relation(plugin, key(), 0, to_npc), fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);

  fo4::forms::FactionRelation to_self = to_npc;
  to_self.faction = key();
  fo4::forms::add_faction_relation(plugin, key(), to_self);
  EXPECT_EQ(fo4::forms::decode_faction_relations(plugin, key()).size(), 3u);
}

TEST(FactionSynthetic, AddsSetsAndRemovesRanksByIndex) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::forms::add_faction_rank(plugin, key(), make_rank(9, "Captain"));
  fo4::forms::set_faction_rank(plugin, key(), 1, make_rank(4, "Lieutenant"));
  fo4::forms::remove_faction_rank(plugin, key(), 0);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto ranks = fo4::forms::decode_faction_ranks(reread, key());
  ASSERT_EQ(ranks.size(), 3u);
  EXPECT_EQ(ranks[0].number, 4u);
  EXPECT_EQ(ranks[0].male_title->inline_text(), "Lieutenant");
  EXPECT_EQ(ranks[1].number, 2u);
  EXPECT_EQ(ranks[1].female_title->inline_text(), "Captain F");
  EXPECT_EQ(ranks[2].number, 9u);
  EXPECT_EQ(ranks[2].male_title->inline_text(), "Captain");
  ASSERT_TRUE(ranks[1].insignia.has_value());
}

TEST(FactionSynthetic, LocalizedTitlesKeepStringIds) {
  auto plugin = fo4::read_plugin(make_plugin({.localized = true}), "Patch.esp");
  const auto ranks = fo4::forms::decode_faction_ranks(plugin, key());
  ASSERT_EQ(ranks.size(), 3u);
  ASSERT_TRUE(ranks[0].male_title.has_value());
  EXPECT_EQ(ranks[0].male_title->id.value(), 0x101u);

  fo4::forms::FactionRank rank;
  rank.number = 5;
  fo4::schema::LocalizedString title;
  title.id = 0x2222u;
  rank.male_title = title;
  fo4::forms::set_faction_rank(plugin, key(), 0, rank);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto updated = fo4::forms::decode_faction_ranks(reread, key());
  EXPECT_EQ(updated[0].number, 5u);
  EXPECT_EQ(updated[0].male_title->id.value(), 0x2222u);
}

TEST(FactionSynthetic, RejectsInlineTitleInLocalizedPlugin) {
  auto plugin = fo4::read_plugin(make_plugin({.localized = true}), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  fo4::forms::FactionRank rank = make_rank(0, "Recruit");
  EXPECT_THROW(fo4::forms::set_faction_rank(plugin, key(), 0, rank),
               fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(FactionSynthetic, RejectsRankTitleWithEmbeddedNul) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const std::string_view nul_title{"A\0B", 3};
  EXPECT_THROW(fo4::forms::add_faction_rank(plugin, key(), make_rank(9, nul_title)),
               fo4::forms::FactionError);
  EXPECT_THROW(fo4::forms::set_faction_rank(plugin, key(), 0, make_rank(0, nul_title)),
               fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(FactionSynthetic, DecodeRejectsOnDiskReactionAboveThree) {
  const auto plugin = fo4::read_plugin(make_plugin({.first_reaction = 4}), "Patch.esp");
  EXPECT_THROW(fo4::forms::decode_faction_relations(plugin, key()), fo4::forms::FactionError);
}

TEST(FactionSynthetic, RejectsMalformedRelationWithoutTouchingPlugin) {
  const auto malformed = fo4::read_plugin(make_plugin({.short_relation = true}), "Patch.esp");
  EXPECT_THROW(fo4::forms::decode_faction_relations(malformed, key()),
               fo4::forms::FactionError);

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  fo4::forms::FactionRelation bad;
  bad.faction = fo4::FormKey::parse("000123:MagicMaster.esp");
  bad.reaction = static_cast<fo4::forms::FactionCombatReaction>(4);
  EXPECT_THROW(fo4::forms::add_faction_relation(plugin, key(), bad), fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);

  fo4::forms::FactionRelation unknown_master;
  unknown_master.faction = fo4::FormKey::parse("000123:NotAMaster.esp");
  EXPECT_THROW(fo4::forms::set_faction_relation(plugin, key(), 0, unknown_master),
               fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(FactionSynthetic, RejectsRankMemberOutsideItsGroup) {
  const auto plugin = fo4::read_plugin(make_plugin({.titles_before_rank = true}), "Patch.esp");
  EXPECT_THROW(fo4::forms::decode_faction_ranks(plugin, key()), fo4::forms::FactionError);
}

TEST(FactionSynthetic, RejectsOutOfRangeIndexAndLeavesPluginUntouched) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::forms::remove_faction_relation(plugin, key(), 2), fo4::forms::FactionError);
  EXPECT_THROW(fo4::forms::set_faction_rank(plugin, key(), 3, make_rank(0, "x")),
               fo4::forms::FactionError);
  EXPECT_THROW(fo4::forms::remove_faction_rank(plugin, key(), 3), fo4::forms::FactionError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(FactionSynthetic, CrimeStealMultiplierNeedsFullCrvaPayload) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("FACT"),
                                                          "FACT\\CrimeStealMultiplier");
  ASSERT_TRUE(field.has_value());
  const std::vector<std::byte> short_payload(12);
  const std::vector<std::byte> full_payload(20);
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(short_payload, field->scalar).empty());
  EXPECT_EQ(fo4::schema::scalar_field_bytes(full_payload, field->scalar).size(), 4u);
}

TEST(FactionCreation, CreatesNamedFactionAndReadsItBack) {
  auto plugin = fo4::read_plugin(make_faction_plugin(0), "Patch.esp");
  const auto key = fo4::forms::create_faction(plugin, std::string_view{"CreatedFaction"},
                                              std::string_view{"Created Faction"}, 0x81u);
  EXPECT_EQ(key, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "FACT");
  EXPECT_EQ(signature_names(*record),
            (std::vector<std::string>{"EDID", "FULL", "DATA", "CRVA", "VENV"}));
  EXPECT_EQ(record->subrecords[0].data, zstring_bytes("CreatedFaction"));
  EXPECT_EQ(record->subrecords[1].data, zstring_bytes("Created Faction"));
  EXPECT_EQ(record->subrecords[2].data,
            (std::vector<std::byte>{std::byte{0x81}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(record->subrecords[3].data.size(), 20u);
  EXPECT_EQ(record->subrecords[4].data.size(), 12u);

  EXPECT_TRUE(fo4::forms::decode_faction_relations(reread, key).empty());
  EXPECT_TRUE(fo4::forms::decode_faction_ranks(reread, key).empty());
}

TEST(FactionCreation, UnnamedFactionHasOnlyRequiredSubrecords) {
  auto plugin = fo4::read_plugin(make_faction_plugin(0), "Patch.esp");
  const auto key = fo4::forms::create_faction(plugin, std::nullopt, std::nullopt, 0u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signature_names(*record), (std::vector<std::string>{"DATA", "CRVA", "VENV"}));
  EXPECT_TRUE(fo4::forms::decode_faction_relations(reread, key).empty());
}

TEST(FactionCreation, RefusesUndefinedFlagsAndLocalizedFullName) {
  auto plugin = fo4::read_plugin(make_faction_plugin(0), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::nullopt, std::nullopt, 0x00020000u)),
               fo4::forms::FactionError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());

  auto localized = fo4::read_plugin(make_faction_plugin(0x80), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   localized, std::nullopt, std::string_view{"Named"}, 0u)),
               fo4::forms::FactionError);
  EXPECT_EQ(localized.header.next_form_id, 0x800u);
  EXPECT_NO_THROW(static_cast<void>(
      fo4::forms::create_faction(localized, std::nullopt, std::nullopt, 0u)));
}

TEST(FactionCreation, EmptyFullNameWritesNoFullSubrecordAndIsNotLocalizedRefused) {
  auto plugin = fo4::read_plugin(make_faction_plugin(0), "Patch.esp");
  const auto key = fo4::forms::create_faction(plugin, std::string_view{"NoNameFaction"},
                                              std::string_view{}, 0u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signature_names(*record), (std::vector<std::string>{"EDID", "DATA", "CRVA", "VENV"}));

  auto localized = fo4::read_plugin(make_faction_plugin(0x80), "Patch.esp");
  EXPECT_NO_THROW(static_cast<void>(
      fo4::forms::create_faction(localized, std::nullopt, std::string_view{}, 0u)));
}

TEST(FactionCreation, RefusesEmbeddedNulInEditorIdOrFullName) {
  auto plugin = fo4::read_plugin(make_faction_plugin(0), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::string_view{"A\0B", 3}, std::nullopt, 0u)),
               fo4::forms::FactionError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::nullopt, std::string_view{"A\0B", 3}, 0u)),
               fo4::forms::FactionError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}
