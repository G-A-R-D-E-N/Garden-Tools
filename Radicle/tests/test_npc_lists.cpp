#include "fo4/forms/npc.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Entry = std::pair<std::string, Bytes>;
using fo4::forms::NpcError;

fo4::FormKey key() { return fo4::FormKey::parse("000800:Patch.esp"); }

Bytes text_bytes(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(std::uint8_t{0});
  return out.bytes;
}

Bytes acbs() {
  fo4::test::ByteWriter out;
  out.u32(0x00000001);
  out.u16(static_cast<std::uint16_t>(-7));
  out.u16(12);
  out.u16(3);
  out.u16(40);
  out.u16(static_cast<std::uint16_t>(-20));
  out.u16(5);
  out.u16(60);
  out.u16(0);
  return out.bytes;
}

Bytes aidt() {
  fo4::test::ByteWriter out;
  out.u8(1);
  out.u8(2);
  out.u8(3);
  out.u8(4);
  out.u8(5);
  out.u8(6);
  out.u8(1);
  out.u8(0);
  out.u32(0x11223344);
  out.u32(7);
  out.u32(8);
  out.u8(1);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Bytes dnam() {
  fo4::test::ByteWriter out;
  out.u16(300);
  out.u16(120);
  out.u16(512);
  out.u8(2);
  out.u8(0);
  return out.bytes;
}

Bytes obnd() {
  fo4::test::ByteWriter out;
  for (const auto value : {-1, -2, -3, 4, 5, 6}) out.u16(static_cast<std::uint16_t>(value));
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes u32_value(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes snam(std::uint32_t form_id, std::uint8_t rank) {
  fo4::test::ByteWriter out;
  out.u32(form_id);
  out.u8(rank);
  return out.bytes;
}

Bytes prkr(std::uint32_t form_id, std::uint8_t rank) {
  fo4::test::ByteWriter out;
  out.u32(form_id);
  out.u8(rank);
  return out.bytes;
}

Bytes cnto(std::uint32_t form_id, std::uint32_t count) {
  fo4::test::ByteWriter out;
  out.u32(form_id);
  out.u32(count);
  return out.bytes;
}

Bytes coed(std::uint32_t owner, std::uint32_t global_or_rank, float condition) {
  fo4::test::ByteWriter out;
  out.u32(owner);
  out.u32(global_or_rank);
  out.f32(condition);
  return out.bytes;
}

std::vector<Entry> base() {
  return {
      {"EDID", text_bytes("RadicleNpc")},
      {"OBND", obnd()},
      {"ACBS", acbs()},
      {"SNAM", snam(0x00000123, 2)},
      {"SNAM", snam(0x01000810, 0xFF)},
      {"PRKZ", u32_value(2)},
      {"PRKR", prkr(0x00000456, 1)},
      {"PRKR", prkr(0x01000811, 3)},
      {"COCT", u32_value(2)},
      {"CNTO", cnto(0x00000ABC, 5)},
      {"COED", coed(0x00000123, 2, 0.5f)},
      {"CNTO", cnto(0x01000812, 0xFFFFFFFFu)},
      {"AIDT", aidt()},
      {"DNAM", dnam()},
      {"MWGT", floats({0.5f, 1.0f, 1.5f})},
      {"NAM6", floats({1.25f})},
      {"NAM4", floats({1.75f})},
      {"NAM7", floats({2.5f})},
      {"NAM8", u32_value(2)},
      {"FMIN", floats({0.25f})},
      {"QNAM", floats({0.1f, 0.2f, 0.3f, 0.4f})},
      {"MRSV", floats({0.5f, 0.6f, 0.7f, 0.8f, 0.9f})},
  };
}

Bytes build(const std::vector<Entry>& subrecords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x01000800);
  for (const auto& [signature, data] : subrecords) {
    fo4::test::write_subrecord(out, signature, data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Record& npc(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, key());
  EXPECT_NE(record, nullptr);
  static const fo4::Record empty{};
  return record == nullptr ? empty : *record;
}

std::vector<std::string> signatures(const fo4::Record& record) {
  std::vector<std::string> result;
  for (const auto& subrecord : record.subrecords) result.push_back(subrecord.signature.to_string());
  return result;
}

std::size_t count_of(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == wanted) ++count;
  }
  return count;
}

std::vector<Entry> subrecords_except(const fo4::Record& record, std::string_view signature) {
  std::vector<Entry> result;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature.to_string() == signature) continue;
    result.emplace_back(subrecord.signature.to_string(), subrecord.data);
  }
  return result;
}

std::uint32_t u32_of(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != wanted || subrecord.data.size() != 4) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, subrecord.data.data(), sizeof(value));
    return value;
  }
  return 0xFFFFFFFFu;
}

std::string scalar_text(const fo4::Record& record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field.has_value()) return "no descriptor";
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != field->scalar.subrecord) continue;
    const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field->scalar);
    return bytes.empty() ? std::string("absent") : fo4::schema::decode_scalar(bytes, field->scalar);
  }
  return "no subrecord";
}

fo4::Plugin reread(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

std::vector<Entry> replaced(std::size_t index, Entry entry) {
  auto entries = base();
  entries[index] = std::move(entry);
  return entries;
}

fo4::Plugin read_npc(const std::vector<Entry>& entries) {
  return fo4::read_plugin(build(entries), "Patch.esp");
}

}  // namespace

TEST(NpcScalars, DecodesFixedLayoutFieldsFromTheSchema) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  const auto& record = npc(plugin);

  EXPECT_EQ(scalar_text(record, "NPC_\\ConfigurationFlags"), "1");
  EXPECT_EQ(scalar_text(record, "NPC_\\XPValueOffset"), "-7");
  EXPECT_EQ(scalar_text(record, "NPC_\\Level"), "12");
  EXPECT_EQ(scalar_text(record, "NPC_\\CalcMinLevel"), "3");
  EXPECT_EQ(scalar_text(record, "NPC_\\CalcMaxLevel"), "40");
  EXPECT_EQ(scalar_text(record, "NPC_\\DispositionBase"), "-20");
  EXPECT_EQ(scalar_text(record, "NPC_\\TemplateFlags"), "5");
  EXPECT_EQ(scalar_text(record, "NPC_\\BleedoutOverride"), "60");
  EXPECT_EQ(scalar_text(record, "NPC_\\CalculatedHealth"), "300");
  EXPECT_EQ(scalar_text(record, "NPC_\\GearedUpWeapons"), "2");
  EXPECT_EQ(scalar_text(record, "NPC_\\AIAggression"), "1");
  EXPECT_EQ(scalar_text(record, "NPC_\\AIMood"), "5");
  EXPECT_EQ(scalar_text(record, "NPC_\\AIWarn"), "287454020");
  EXPECT_EQ(scalar_text(record, "NPC_\\AIAttack"), "8");
  EXPECT_EQ(scalar_text(record, "NPC_\\AINoSlowApproach"), "1");
  EXPECT_EQ(scalar_text(record, "NPC_\\ObjectBoundsMinX"), "-1");
  EXPECT_EQ(scalar_text(record, "NPC_\\ObjectBoundsMaxZ"), "6");
  EXPECT_EQ(scalar_text(record, "NPC_\\SoundLevel"), "2");
  EXPECT_NE(scalar_text(record, "NPC_\\MorphRegionLegs"), "absent");
  EXPECT_NE(scalar_text(record, "NPC_\\WeightThin"), "absent");
}

TEST(NpcScalars, WrongSizedAcbsPayloadHidesEveryAcbsField) {
  auto entries = base();
  entries[2] = {"ACBS", Bytes(19, std::byte{0x07})};
  auto plugin = fo4::read_plugin(build(entries), "Patch.esp");
  const auto& record = npc(plugin);

  EXPECT_EQ(scalar_text(record, "NPC_\\ConfigurationFlags"), "absent");
  EXPECT_EQ(scalar_text(record, "NPC_\\BleedoutOverride"), "absent");
  EXPECT_EQ(scalar_text(record, "NPC_\\Level"), "absent");
  EXPECT_EQ(scalar_text(record, "NPC_\\CalculatedHealth"), "300");
  EXPECT_NO_THROW(fo4::forms::decode_npc_factions(plugin, key()));
}

TEST(NpcLists, DecodesFactionsPerksAndInventoryInSubrecordOrder) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");

  const auto factions = fo4::forms::decode_npc_factions(plugin, key());
  ASSERT_EQ(factions.size(), 2u);
  EXPECT_EQ(factions[0].faction, fo4::FormKey::parse("000123:Fallout4.esm"));
  EXPECT_EQ(factions[0].rank, 2);
  EXPECT_EQ(factions[1].faction, fo4::FormKey::parse("000810:Patch.esp"));
  EXPECT_EQ(factions[1].rank, -1);

  const auto perks = fo4::forms::decode_npc_perks(plugin, key());
  ASSERT_EQ(perks.size(), 2u);
  EXPECT_EQ(perks[0].perk, fo4::FormKey::parse("000456:Fallout4.esm"));
  EXPECT_EQ(perks[0].rank, std::uint8_t{1});
  EXPECT_EQ(perks[1].perk, fo4::FormKey::parse("000811:Patch.esp"));
  EXPECT_EQ(perks[1].rank, std::uint8_t{3});

  const auto items = fo4::forms::decode_npc_inventory(plugin, key());
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].item, fo4::FormKey::parse("000ABC:Fallout4.esm"));
  EXPECT_EQ(items[0].count, 5);
  ASSERT_TRUE(items[0].extra_data.has_value());
  ASSERT_TRUE(items[0].extra_data->owner.has_value());
  EXPECT_EQ(*items[0].extra_data->owner, fo4::FormKey::parse("000123:Fallout4.esm"));
  EXPECT_EQ(items[0].extra_data->global_or_rank, 2u);
  EXPECT_EQ(items[0].extra_data->condition, 0.5f);
  EXPECT_EQ(items[1].item, fo4::FormKey::parse("000812:Patch.esp"));
  EXPECT_EQ(items[1].count, -1);
  EXPECT_FALSE(items[1].extra_data.has_value());
}

TEST(NpcLists, AddRemoveAndSetFactionsRoundTripInOrder) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  const fo4::forms::NpcFaction added{fo4::FormKey::parse("000777:Fallout4.esm"), 4};
  ASSERT_TRUE(fo4::forms::add_npc_faction(plugin, key(), added));
  EXPECT_EQ(fo4::forms::decode_npc_factions(plugin, key()).size(), 3u);

  ASSERT_TRUE(fo4::forms::set_npc_faction(plugin, key(), 0, {fo4::FormKey::parse("000123:Fallout4.esm"), 9}));
  ASSERT_TRUE(fo4::forms::remove_npc_faction(plugin, key(), 1));

  const auto after = fo4::forms::decode_npc_factions(reread(plugin), key());
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[0].rank, 9);
  EXPECT_EQ(after[1], added);
  EXPECT_EQ(count_of(npc(reread(plugin)), "SNAM"), 2u);
}

TEST(NpcLists, FactionEditsTouchOnlyTheSnamRun) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  const auto before = subrecords_except(npc(plugin), "SNAM");
  ASSERT_TRUE(fo4::forms::add_npc_faction(plugin, key(), {fo4::FormKey::parse("000777:Fallout4.esm"), 4}));
  ASSERT_TRUE(fo4::forms::set_npc_faction(plugin, key(), 0, {fo4::FormKey::parse("000123:Fallout4.esm"), 9}));
  ASSERT_TRUE(fo4::forms::remove_npc_faction(plugin, key(), 1));
  EXPECT_EQ(subrecords_except(npc(reread(plugin)), "SNAM"), before);
  EXPECT_EQ(count_of(npc(reread(plugin)), "SNAM"), 2u);
}

TEST(NpcLists, EightByteFactionSnamIsRefused) {
  const auto legacy = replaced(3, {"SNAM", Bytes(8, std::byte{0x11})});
  auto plugin = read_npc(legacy);
  EXPECT_THROW(fo4::forms::decode_npc_factions(plugin, key()), NpcError);
  EXPECT_THROW(fo4::forms::add_npc_faction(plugin, key(), {fo4::FormKey::parse("000777:Fallout4.esm"), 1}), NpcError);
}

TEST(NpcLists, InsertsMissingFactionRunAfterAcbs) {
  std::vector<Entry> entries;
  for (const auto& entry : base()) {
    if (entry.first != "SNAM") entries.push_back(entry);
  }
  auto plugin = fo4::read_plugin(build(entries), "Patch.esp");
  ASSERT_TRUE(fo4::forms::add_npc_faction(plugin, key(), {fo4::FormKey::parse("000777:Fallout4.esm"), 4}));
  const auto names = signatures(npc(plugin));
  const auto acbs = std::find(names.begin(), names.end(), std::string("ACBS"));
  ASSERT_NE(acbs, names.end());
  EXPECT_EQ(*(acbs + 1), std::string("SNAM"));
}

TEST(NpcLists, PerkEditsKeepPrkzEqualToEntries) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  ASSERT_TRUE(fo4::forms::add_npc_perk(plugin, key(), {fo4::FormKey::parse("000999:Fallout4.esm"), 2}));
  EXPECT_EQ(u32_of(npc(plugin), "PRKZ"), 3u);
  EXPECT_EQ(count_of(npc(plugin), "PRKR"), 3u);

  ASSERT_TRUE(fo4::forms::set_npc_perk(plugin, key(), 1, {fo4::FormKey::parse("000456:Fallout4.esm"), 7}));
  ASSERT_TRUE(fo4::forms::remove_npc_perk(plugin, key(), 0));
  const auto after = fo4::forms::decode_npc_perks(reread(plugin), key());
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[0], (fo4::forms::NpcPerk{fo4::FormKey::parse("000456:Fallout4.esm"), 7}));
  EXPECT_EQ(after[1], (fo4::forms::NpcPerk{fo4::FormKey::parse("000999:Fallout4.esm"), 2}));
  EXPECT_EQ(u32_of(npc(reread(plugin)), "PRKZ"), 2u);
}

TEST(NpcLists, InventoryEditsKeepCoctEqualAndCoedWithItsItem) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  fo4::forms::NpcInventoryItem added;
  added.item = fo4::FormKey::parse("000555:Fallout4.esm");
  added.count = 1;
  ASSERT_TRUE(fo4::forms::add_npc_inventory_item(plugin, key(), added));
  EXPECT_EQ(u32_of(npc(plugin), "COCT"), 3u);
  EXPECT_EQ(count_of(npc(plugin), "CNTO"), 3u);

  ASSERT_TRUE(fo4::forms::remove_npc_inventory_item(plugin, key(), 0));
  const auto after = fo4::forms::decode_npc_inventory(reread(plugin), key());
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(u32_of(npc(reread(plugin)), "COCT"), 2u);
  EXPECT_EQ(count_of(npc(reread(plugin)), "COED"), 0u);
  EXPECT_EQ(after[0].item, fo4::FormKey::parse("000812:Patch.esp"));
  EXPECT_EQ(after[1], added);

  fo4::forms::NpcInventoryItem with_extra;
  with_extra.item = fo4::FormKey::parse("000ABC:Fallout4.esm");
  with_extra.count = 2;
  with_extra.extra_data = fo4::forms::NpcItemExtraData{std::nullopt, 0xFFFFFFFFu, 1.5f};
  ASSERT_TRUE(fo4::forms::set_npc_inventory_item(plugin, key(), 1, with_extra));
  const auto final_items = fo4::forms::decode_npc_inventory(reread(plugin), key());
  ASSERT_EQ(final_items.size(), 2u);
  EXPECT_EQ(final_items[1], with_extra);
}

TEST(NpcLists, RemovingTheLastEntryKeepsItsCountSubrecordAtZero) {
  auto plugin = read_npc({
      {"EDID", text_bytes("RadicleNpc")},
      {"ACBS", acbs()},
      {"PRKZ", u32_value(1)},
      {"PRKR", prkr(0x00000456, 1)},
      {"COCT", u32_value(1)},
      {"CNTO", cnto(0x00000ABC, 5)},
      {"DNAM", dnam()},
  });
  ASSERT_TRUE(fo4::forms::remove_npc_perk(plugin, key(), 0));
  ASSERT_TRUE(fo4::forms::remove_npc_inventory_item(plugin, key(), 0));
  const auto after = reread(plugin);
  EXPECT_EQ(signatures(npc(after)),
            (std::vector<std::string>{"EDID", "ACBS", "PRKZ", "COCT", "DNAM"}));
  EXPECT_EQ(u32_of(npc(after), "PRKZ"), 0u);
  EXPECT_EQ(u32_of(npc(after), "COCT"), 0u);
  EXPECT_TRUE(fo4::forms::decode_npc_perks(after, key()).empty());
  EXPECT_TRUE(fo4::forms::decode_npc_inventory(after, key()).empty());
}

TEST(NpcLists, InsertedRunsStayOutsideTheDestAndAttackGroups) {
  auto plugin = read_npc({
      {"EDID", text_bytes("RadicleNpc")},
      {"ACBS", acbs()},
      {"DEST", Bytes(8, std::byte{0x01})},
      {"DAMC", Bytes(8, std::byte{0x02})},
      {"DSTD", Bytes(20, std::byte{0x03})},
      {"DMDL", text_bytes("Destroyed.nif")},
      {"DSTF", Bytes{}},
      {"ATKD", Bytes(52, std::byte{0x04})},
      {"ATKE", text_bytes("Attack")},
      {"DNAM", dnam()},
  });
  ASSERT_TRUE(fo4::forms::add_npc_perk(plugin, key(), {fo4::FormKey::parse("000456:Fallout4.esm"), 1}));
  ASSERT_TRUE(fo4::forms::add_npc_inventory_item(plugin, key(), {fo4::FormKey::parse("000ABC:Fallout4.esm"), 2, std::nullopt}));
  const auto names = signatures(npc(reread(plugin)));
  EXPECT_EQ(names, (std::vector<std::string>{"EDID", "ACBS", "DEST", "DAMC", "DSTD", "DMDL", "DSTF",
                                             "ATKD", "ATKE", "PRKZ", "PRKR", "COCT", "CNTO", "DNAM"}));
}

TEST(NpcLists, InventoryItemsMustBeBaseObjectsAndCoedOwnersNpcOrFact) {
  fo4::test::ByteWriter out;
  out.bytes = build(base());
  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  fo4::test::end_record(out, fo4::test::begin_record(out, "MISC", 0, 0x01000803));
  fo4::test::end_group(out, misc_group);
  const auto perk_group = fo4::test::begin_group(out, "PERK", 0);
  fo4::test::end_record(out, fo4::test::begin_record(out, "PERK", 0, 0x01000801));
  fo4::test::end_group(out, perk_group);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  const auto misc = fo4::FormKey::parse("000803:Patch.esp");
  const auto perk = fo4::FormKey::parse("000801:Patch.esp");
  const auto npc_owner = key();
  const auto to_item = [](const fo4::FormKey& item) {
    return fo4::forms::NpcInventoryItem{item, 1, std::nullopt};
  };
  const auto with_owner = [](const fo4::FormKey& owner) {
    return fo4::forms::NpcInventoryItem{
        fo4::FormKey::parse("000803:Patch.esp"), 1,
        fo4::forms::NpcItemExtraData{owner, 0, 1.0f}};
  };
  EXPECT_THROW(fo4::forms::add_npc_inventory_item(plugin, key(), to_item(perk)), NpcError);
  EXPECT_THROW(fo4::forms::set_npc_inventory_item(plugin, key(), 0, to_item(perk)), NpcError);
  EXPECT_THROW(fo4::forms::add_npc_inventory_item(plugin, key(), with_owner(misc)), NpcError);
  EXPECT_THROW(fo4::forms::set_npc_inventory_item(plugin, key(), 0, with_owner(perk)), NpcError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);

  EXPECT_TRUE(fo4::forms::add_npc_inventory_item(plugin, key(), to_item(misc)));
  EXPECT_TRUE(fo4::forms::add_npc_inventory_item(plugin, key(), with_owner(npc_owner)));
  EXPECT_TRUE(fo4::forms::set_npc_inventory_item(plugin, key(), 0, with_owner(fo4::FormKey::parse("000800:Patch.esp"))));
}

TEST(NpcLists, NoOpSetDoesNotDirtyAParsedPlugin) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  const auto current = fo4::forms::decode_npc_factions(plugin, key());
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_FALSE(fo4::forms::set_npc_faction(plugin, key(), 0, current[0]));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(NpcLists, FailsClosedOnMalformedSizesCountsAndNulls) {
  const auto short_snam = replaced(3, {"SNAM", Bytes(7, std::byte{0x11})});
  const auto short_prkr = replaced(6, {"PRKR", Bytes(4, std::byte{0x22})});
  const auto short_cnto = replaced(9, {"CNTO", Bytes(7, std::byte{0x33})});
  const auto long_coed = replaced(10, {"COED", Bytes(11, std::byte{0x44})});
  const auto wrong_prkz = replaced(5, {"PRKZ", u32_value(1)});
  const auto null_faction = replaced(3, {"SNAM", snam(0, 1)});
  const auto null_perk = replaced(6, {"PRKR", prkr(0, 1)});
  const auto null_item = replaced(9, {"CNTO", cnto(0, 1)});

  EXPECT_THROW(fo4::forms::decode_npc_factions(read_npc(short_snam), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_perks(read_npc(short_prkr), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_inventory(read_npc(short_cnto), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_inventory(read_npc(long_coed), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_perks(read_npc(wrong_prkz), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_factions(read_npc(null_faction), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_perks(read_npc(null_perk), key()), NpcError);
  EXPECT_THROW(fo4::forms::decode_npc_inventory(read_npc(null_item), key()), NpcError);

  auto orphan = base();
  orphan.insert(orphan.begin() + 13, Entry{"COED", coed(0, 0, 0.0f)});
  EXPECT_THROW(fo4::forms::decode_npc_inventory(read_npc(orphan), key()), NpcError);

  auto plugin = read_npc(short_snam);
  EXPECT_THROW(fo4::forms::add_npc_faction(plugin, key(), {fo4::FormKey::parse("000777:Fallout4.esm"), 1}), NpcError);
}

TEST(NpcLists, LocalFactionAndPerkTargetsMustHaveTheirSignature) {
  fo4::test::ByteWriter out;
  out.bytes = build(base());
  const auto perk_group = fo4::test::begin_group(out, "PERK", 0);
  const auto perk = fo4::test::begin_record(out, "PERK", 0, 0x01000801);
  fo4::test::end_record(out, perk);
  fo4::test::end_group(out, perk_group);
  const auto fact_group = fo4::test::begin_group(out, "FACT", 0);
  const auto fact = fo4::test::begin_record(out, "FACT", 0, 0x01000802);
  fo4::test::end_record(out, fact);
  fo4::test::end_group(out, fact_group);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  const auto to_perk = fo4::FormKey::parse("000801:Patch.esp");
  const auto to_fact = fo4::FormKey::parse("000802:Patch.esp");
  EXPECT_THROW(fo4::forms::add_npc_faction(plugin, key(), {to_perk, 1}), NpcError);
  EXPECT_THROW(fo4::forms::set_npc_faction(plugin, key(), 0, {to_perk, 1}), NpcError);
  EXPECT_THROW(fo4::forms::add_npc_perk(plugin, key(), {to_fact, 1}), NpcError);
  EXPECT_THROW(fo4::forms::set_npc_perk(plugin, key(), 0, {to_fact, 1}), NpcError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);

  EXPECT_TRUE(fo4::forms::add_npc_faction(plugin, key(), {to_fact, 1}));
  EXPECT_TRUE(fo4::forms::add_npc_perk(plugin, key(), {to_perk, 1}));
}

TEST(NpcLists, CoctMismatchIsTolerantAndEditsRewriteTheCount) {
  const auto wrong_coct = replaced(8, {"COCT", u32_value(3)});
  auto plugin = read_npc(wrong_coct);
  const auto items = fo4::forms::decode_npc_inventory(plugin, key());
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].count, 5);
  EXPECT_EQ(items[1].count, -1);

  auto item = items[0];
  item.count = 6;
  ASSERT_TRUE(fo4::forms::set_npc_inventory_item(plugin, key(), 0, item));
  EXPECT_EQ(u32_of(npc(reread(plugin)), "COCT"), 2u);
  EXPECT_EQ(fo4::forms::decode_npc_inventory(reread(plugin), key()).at(0).count, 6);
}

TEST(NpcLists, RefusesMissingRecordsOutOfRangeIndexesAndNullEntries) {
  auto plugin = fo4::read_plugin(build(base()), "Patch.esp");
  EXPECT_THROW(fo4::forms::decode_npc_factions(plugin, fo4::FormKey::parse("000999:Patch.esp")), NpcError);
  EXPECT_THROW(fo4::forms::remove_npc_faction(plugin, key(), 2), NpcError);
  EXPECT_THROW(fo4::forms::set_npc_perk(plugin, key(), 2, {fo4::FormKey::parse("000456:Fallout4.esm"), 1}), NpcError);
  EXPECT_THROW(fo4::forms::add_npc_faction(plugin, key(), fo4::forms::NpcFaction{}), NpcError);
  EXPECT_THROW(fo4::forms::add_npc_inventory_item(plugin, key(), fo4::forms::NpcInventoryItem{}), NpcError);
}

TEST(NpcLists, DuplicateCountSubrecordsFailClosed) {
  auto perks = base();
  perks.insert(perks.begin() + 6, Entry{"PRKZ", u32_value(2)});
  EXPECT_THROW(fo4::forms::decode_npc_perks(read_npc(perks), key()), NpcError);

  auto inventory = base();
  inventory.insert(inventory.begin() + 9, Entry{"COCT", u32_value(2)});
  EXPECT_THROW(fo4::forms::decode_npc_inventory(read_npc(inventory), key()), NpcError);
}

TEST(NpcLists, SplitSnamRunRefusesAddAndLeavesBytesUnchanged) {
  auto entries = base();
  entries.insert(entries.begin() + 13, Entry{"SNAM", snam(0x00000123, 2)});
  auto plugin = read_npc(entries);
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::forms::add_npc_faction(plugin, key(), {fo4::FormKey::parse("000124:Fallout4.esm"), 1}),
               NpcError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}
