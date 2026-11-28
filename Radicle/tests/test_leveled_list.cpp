#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Layouts and sources: Radicle/docs/LEVELED_LISTS.md (TODO-verify until LeveledListReal runs on CI).
namespace {

struct Sub {
  std::string_view signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string_view signature;
  std::uint32_t form_id;
  std::vector<Sub> subs;
  std::uint16_t form_version{131};
};

constexpr std::uint32_t k_item_list = 0x02000800;
constexpr std::uint32_t k_npc_list = 0x02000801;
constexpr std::uint32_t k_empty_list = 0x02000802;
constexpr std::uint32_t k_plain_record = 0x02000803;
constexpr std::uint32_t k_npc_target = 0x02000810;
constexpr std::uint32_t k_spell_list = 0x02000804;

std::vector<std::byte> text(std::string_view value) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> one(std::uint8_t value) { return {std::byte{value}}; }

std::vector<std::byte> four(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> lvlo(std::uint16_t level,
                            std::array<std::uint8_t, 2> reserved,
                            std::uint32_t form_id,
                            std::uint16_t count,
                            std::uint8_t chance_none,
                            std::uint8_t tail) {
  fo4::test::ByteWriter out;
  out.u16(level);
  out.u8(reserved[0]);
  out.u8(reserved[1]);
  out.u32(form_id);
  out.u16(count);
  out.u8(chance_none);
  out.u8(tail);
  return out.bytes;
}

std::vector<std::byte> coed(std::uint32_t owner, std::uint32_t global_or_rank, float condition) {
  fo4::test::ByteWriter out;
  out.u32(owner);
  out.u32(global_or_rank);
  out.f32(condition);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
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
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MagicMaster.esp\0", 16});
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  for (const auto& rec : records) {
    const auto group = fo4::test::begin_group(out, rec.signature, 0);
    const auto record = fo4::test::begin_record(out, rec.signature, 0, rec.form_id, rec.form_version);
    for (const auto& sub : rec.subs) {
      fo4::test::write_subrecord(out, sub.signature, sub.data);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<Sub> item_list_subs() {
  auto keyword = four(0x01000123);
  const auto chance = four(50);
  keyword.insert(keyword.end(), chance.begin(), chance.end());
  return {
      {"EDID", text("RadicleItemList")},
      {"OBND", std::vector<std::byte>(12, std::byte{0x44})},
      {"LVLD", one(25)},
      {"LVLM", one(0)},
      {"LVLF", one(5)},
      {"LVLG", four(0x01000456)},
      {"LLCT", one(2)},
      {"LVLO", lvlo(5, {0xAA, 0xBB}, 0x01000123, 2, 7, 0xCC)},
      {"COED", coed(0, 0x12345678, 0.5f)},
      {"LVLO", lvlo(10, {0x11, 0x22}, k_empty_list, 1, 0, 0x33)},
      {"LLKC", keyword},
      {"LVSG", four(0x01000456)},
      {"ONAM", text("Override")},
      {"UNAM", {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}}},
  };
}

std::vector<Sub> npc_list_subs() {
  return {
      {"EDID", text("RadicleNpcList")},
      {"OBND", std::vector<std::byte>(12, std::byte{0x55})},
      {"LVLD", one(10)},
      {"LVLM", one(0)},
      {"LVLF", one(4)},
      {"LLCT", one(1)},
      {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 10, 0)},
  };
}

std::vector<Sub> empty_list_subs() {
  return {
      {"EDID", text("RadicleEmptyList")},
      {"LVLD", one(0)},
      {"LVLM", one(0)},
      {"LVLF", one(0)},
      {"LLCT", one(0)},
  };
}

std::vector<std::byte> standard_plugin() {
  return make_plugin({
      {"LVLI", k_item_list, item_list_subs()},
      {"LVLN", k_npc_list, npc_list_subs()},
      {"LVLI", k_empty_list, empty_list_subs()},
      {"NPC_", k_npc_target, {{"EDID", text("RadicleNpcTarget")}}},
  });
}

std::vector<std::byte> item_plugin(std::vector<Sub> subs) {
  return make_plugin({{"LVLI", k_item_list, std::move(subs)}});
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == wanted) return &subrecord;
  }
  return nullptr;
}

fo4::FormKey item_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

}  // namespace

TEST(LeveledList, DecodesItemHeaderEntriesAndExtraData) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto list = fo4::forms::decode_leveled_list(plugin, item_key());

  EXPECT_EQ(list.chance_none, 25u);
  EXPECT_EQ(list.max_count, 0u);
  EXPECT_EQ(list.flags, 5u);
  ASSERT_TRUE(list.use_global.has_value());
  EXPECT_EQ(*list.use_global, fo4::FormKey::parse("000456:MagicMaster.esp"));
  ASSERT_TRUE(list.epic_loot_chance.has_value());
  EXPECT_EQ(*list.epic_loot_chance, fo4::FormKey::parse("000456:MagicMaster.esp"));
  ASSERT_TRUE(list.override_name.has_value());
  EXPECT_EQ(list.override_name->inline_text(), "Override");

  ASSERT_EQ(list.entries.size(), 2u);
  const auto& first = list.entries[0];
  EXPECT_EQ(first.level, 5u);
  EXPECT_EQ(first.item, fo4::FormKey::parse("000123:MagicMaster.esp"));
  EXPECT_EQ(first.count, 2u);
  EXPECT_EQ(first.chance_none, 7u);
  EXPECT_EQ(first.reserved_before_form_id[0], std::byte{0xAA});
  EXPECT_EQ(first.reserved_before_form_id[1], std::byte{0xBB});
  EXPECT_EQ(first.reserved_tail, std::byte{0xCC});
  ASSERT_TRUE(first.extra.has_value());
  EXPECT_TRUE(first.extra->owner.owner().empty());
  EXPECT_EQ(first.extra->global_or_rank, 0x12345678u);
  EXPECT_FLOAT_EQ(first.extra->item_condition, 0.5f);

  EXPECT_EQ(list.entries[1].item, fo4::FormKey::parse("000802:Patch.esp"));
  EXPECT_FALSE(list.entries[1].extra.has_value());
}

TEST(LeveledList, DecodesLeveledNpcAndEmptyList) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto npc = fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(npc.chance_none, 10u);
  EXPECT_EQ(npc.flags, 4u);
  EXPECT_FALSE(npc.use_global.has_value());
  EXPECT_FALSE(npc.override_name.has_value());
  ASSERT_EQ(npc.entries.size(), 1u);
  EXPECT_EQ(npc.entries[0].item, fo4::FormKey::parse("000123:MagicMaster.esp"));
  EXPECT_FALSE(npc.entries[0].extra.has_value());

  const auto empty = fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000802:Patch.esp"));
  EXPECT_TRUE(empty.entries.empty());
}

TEST(LeveledList, ReplaceKeepsOpaqueSubrecordsAndWritesLlctEqualToEntries) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  const auto* before = fo4::find_record(plugin, key);
  ASSERT_NE(before, nullptr);
  const auto opaque = find_subrecord(*before, "UNAM")->data;
  const auto keywords = find_subrecord(*before, "LLKC")->data;

  std::vector<fo4::forms::LeveledListEntry> entries(3);
  entries[0].item = fo4::FormKey::parse("000123:MagicMaster.esp");
  entries[0].level = 20;
  entries[0].count = 3;
  entries[0].extra = fo4::forms::LeveledListExtraData{fo4::FormKey::parse("000810:Patch.esp"), 7u, 1.25f};
  entries[1].item = fo4::FormKey::parse("000777:MagicMaster.esp");
  entries[1].level = 30;
  entries[2].item = fo4::FormKey::parse("000810:Patch.esp");

  ASSERT_TRUE(fo4::forms::replace_leveled_list_entries(plugin, key, entries));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto decoded = fo4::forms::decode_leveled_list(reread, key);
  const std::vector<fo4::forms::LeveledListEntry> sorted{entries[2], entries[0], entries[1]};
  EXPECT_EQ(decoded.entries, sorted);

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* count = find_subrecord(*record, "LLCT");
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(std::to_integer<std::uint8_t>(count->data[0]), 3u);
  EXPECT_EQ(find_subrecord(*record, "UNAM")->data, opaque);
  EXPECT_EQ(find_subrecord(*record, "LLKC")->data, keywords);
  EXPECT_NE(find_subrecord(*record, "ONAM"), nullptr);
}

TEST(LeveledList, InsertRemoveAndSetKeepEntriesOrderedAndLlctCurrent) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  const auto before = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(before.size(), 2u);

  fo4::forms::LeveledListEntry added;
  added.level = 20;
  added.item = fo4::FormKey::parse("000123:MagicMaster.esp");
  added.count = 3;
  added.chance_none = 4;

  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 1, added));
  auto decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_EQ(decoded[0], before[0]);
  EXPECT_EQ(decoded[1], before[1]);
  EXPECT_EQ(decoded[2], added);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, key).entries.size(), 3u);

  ASSERT_TRUE(fo4::forms::remove_leveled_list_entry(plugin, key, 0));
  decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(decoded.size(), 2u);
  EXPECT_EQ(decoded[0], before[1]);
  EXPECT_EQ(decoded[1], added);

  ASSERT_TRUE(fo4::forms::set_leveled_list_entry(plugin, key, 0, before[0]));
  decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  EXPECT_EQ(decoded[0], before[0]);
  EXPECT_EQ(decoded[1], added);

  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, key, 3, added),
               fo4::forms::LeveledListError);
  EXPECT_THROW(fo4::forms::remove_leveled_list_entry(plugin, key, 2),
               fo4::forms::LeveledListError);
  EXPECT_THROW(fo4::forms::set_leveled_list_entry(plugin, key, 2, added),
               fo4::forms::LeveledListError);
}

TEST(LeveledList, EditsOnLeveledNpcUseTheSameEntryPath) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000801:Patch.esp");
  fo4::forms::LeveledListEntry added;
  added.item = fo4::FormKey::parse("000810:Patch.esp");
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 1, added));
  ASSERT_TRUE(fo4::forms::set_leveled_list_settings(plugin, key, 60, 0, 1));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto npc = fo4::forms::decode_leveled_list(reread, key);
  EXPECT_EQ(npc.chance_none, 60u);
  EXPECT_EQ(npc.flags, 1u);
  ASSERT_EQ(npc.entries.size(), 2u);
  EXPECT_EQ(npc.entries[1].item, added.item);
}

TEST(LeveledList, SettingsEditKeepsEntriesAndOpaqueSubrecords) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  const auto before = fo4::forms::decode_leveled_list(plugin, key);

  ASSERT_TRUE(fo4::forms::set_leveled_list_settings(plugin, key, 50, 0, 3));
  const auto after = fo4::forms::decode_leveled_list(
      fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp"), key);
  EXPECT_EQ(after.chance_none, 50u);
  EXPECT_EQ(after.flags, 3u);
  EXPECT_EQ(after.entries, before.entries);
  EXPECT_EQ(after.use_global, before.use_global);
}

TEST(LeveledList, SettingsRejectFlagBitsOutsideTheDefinedMask) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::forms::set_leveled_list_settings(plugin, item_key(), 50, 0, 0x08),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledList, NoOpReplacementDoesNotDirtyAParsedPlugin) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  const auto decoded = fo4::forms::decode_leveled_list(plugin, key);
  const auto original = fo4::test::original_bytes(plugin);

  ASSERT_TRUE(fo4::forms::replace_leveled_list_entries(plugin, key, decoded.entries));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledList, MalformedRecordsFailClosed) {
  const auto with = [](std::initializer_list<Sub> extra) {
    std::vector<Sub> subs = {{"EDID", text("Bad")}, {"LVLD", one(0)}, {"LVLM", one(0)},
                             {"LVLF", one(0)}};
    subs.insert(subs.end(), extra);
    return subs;
  };

  struct Case {
    const char* name;
    std::vector<Sub> subs;
  };
  const std::vector<Case> cases = {
      {"short LVLO", with({{"LLCT", one(1)}, {"LVLO", std::vector<std::byte>(11)}})},
      {"COED before any LVLO", with({{"LLCT", one(0)}, {"COED", coed(0, 0, 1.0f)}})},
      {"two COED for one LVLO",
       with({{"LLCT", one(1)}, {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 0, 0)},
             {"COED", coed(0, 0, 1.0f)}, {"COED", coed(0, 0, 1.0f)}})},
      {"short COED",
       with({{"LLCT", one(1)}, {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 0, 0)},
             {"COED", std::vector<std::byte>(11)}})},
      {"LLCT below entry count",
       with({{"LLCT", one(0)}, {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 0, 0)}})},
      {"LLCT above entry count",
       with({{"LLCT", one(2)}, {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 0, 0)}})},
      {"entries without LLCT", with({{"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 0, 0)}})},
      {"duplicate LLCT", with({{"LLCT", one(0)}, {"LLCT", one(0)}})},
      {"duplicate LVLD", with({{"LVLD", one(0)}, {"LLCT", one(0)}})},
      {"short LVLG", with({{"LVLG", std::vector<std::byte>(3)}, {"LLCT", one(0)}})},
      {"null LVLO FormID", with({{"LLCT", one(1)}, {"LVLO", lvlo(1, {0, 0}, 0, 1, 0, 0)}})},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto plugin = fo4::read_plugin(item_plugin(test_case.subs), "Patch.esp");
    EXPECT_THROW(fo4::forms::decode_leveled_list(plugin, item_key()), fo4::forms::LeveledListError);
  }
}

TEST(LeveledList, RejectsNonLeveledAndMissingRecords) {
  auto plugin = fo4::read_plugin(
      make_plugin({{"MISC", k_plain_record, {{"EDID", text("Plain")}}}}), "Patch.esp");
  EXPECT_THROW(fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000803:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_THROW(fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000999:Patch.esp")),
               fo4::forms::LeveledListError);
}

TEST(LeveledList, RejectsOverlongEntryReplacement) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  std::vector<fo4::forms::LeveledListEntry> too_many(256);
  for (auto& entry : too_many) {
    entry.item = fo4::FormKey::parse("000810:Patch.esp");
  }
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::forms::replace_leveled_list_entries(plugin, item_key(), too_many),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledList, ChangingAnExistingEntryDoesNotRecheckItsMissingTarget) {
  auto plugin = fo4::read_plugin(
      item_plugin({{"EDID", text("RadicleItemList")},
                   {"LLCT", one(1)},
                   {"LVLO", lvlo(1, {0, 0}, 0x02000900, 1, 0, 0)}}),
      "Patch.esp");
  auto entry = fo4::forms::decode_leveled_list(plugin, item_key()).entries.front();
  entry.level = 7;
  ASSERT_TRUE(fo4::forms::set_leveled_list_entry(plugin, item_key(), 0, entry));
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, item_key()).entries.front().level, 7u);

  fo4::forms::LeveledListEntry added;
  added.item = fo4::FormKey::parse("000901:Patch.esp");
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, item_key(), 1, added),
               fo4::forms::LeveledListError);
}

namespace {

constexpr std::uint32_t k_modern_item_list = 0x02000805;

fo4::forms::LeveledListEntry make_entry(std::uint16_t level,
                                        std::string_view form_key,
                                        std::uint16_t count = 1,
                                        std::uint8_t chance_none = 0) {
  fo4::forms::LeveledListEntry entry;
  entry.level = level;
  entry.item = fo4::FormKey::parse(form_key);
  entry.count = count;
  entry.chance_none = chance_none;
  return entry;
}

std::vector<Sub> legacy_list_subs() {
  return {
      {"EDID", text("Legacy")},
      {"LVLD", one(0)},
      {"LVLM", one(0)},
      {"LVLF", one(0)},
      {"LLCT", one(1)},
      {"LVLO", lvlo(1, {0, 0}, 0x01000123, 1, 9, 0)},
  };
}

}  // namespace

TEST(LeveledList, EntriesSortByLevelThenReferenceAfterTypedEdits) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 2, make_entry(10, "000123:MagicMaster.esp")));
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 0, make_entry(1, "000802:Patch.esp")));

  const auto decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(decoded.size(), 4u);
  EXPECT_EQ(decoded[0].level, 1u);
  EXPECT_EQ(decoded[1].level, 5u);
  EXPECT_EQ(decoded[2].item, fo4::FormKey::parse("000123:MagicMaster.esp"));
  EXPECT_EQ(decoded[2].level, 10u);
  EXPECT_EQ(decoded[3].item, fo4::FormKey::parse("000802:Patch.esp"));
}

TEST(LeveledList, EqualSortKeysKeepTheirOrderAndSetResortsTheEdit) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto key = item_key();
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 1, make_entry(10, "000802:Patch.esp", 1, 9)));
  auto decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_EQ(decoded[1].chance_none, 9u);
  EXPECT_EQ(decoded[2].chance_none, 0u);

  ASSERT_TRUE(fo4::forms::set_leveled_list_entry(plugin, key, 0, make_entry(20, "000123:MagicMaster.esp", 2, 7)));
  decoded = fo4::forms::decode_leveled_list(plugin, key).entries;
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_EQ(decoded[0].level, 10u);
  EXPECT_EQ(decoded[2].level, 20u);
  EXPECT_EQ(decoded[2].count, 2u);
}

TEST(LeveledList, LegacyFormVersionReportsZeroChanceNoneForLvliAndLvln) {
  auto plugin = fo4::read_plugin(
      make_plugin({{"LVLI", k_item_list, legacy_list_subs(), 68},
                   {"LVLN", k_npc_list, legacy_list_subs(), 68},
                   {"LVLI", k_modern_item_list, legacy_list_subs(), 69},
                   {"LVSP", k_spell_list, legacy_list_subs(), 68}}),
      "Patch.esp");
  const auto legacy_item = fo4::forms::decode_leveled_list(plugin, item_key());
  EXPECT_EQ(legacy_item.entries.at(0).chance_none, 0u);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000801:Patch.esp"))
                .entries.at(0)
                .chance_none,
            0u);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000805:Patch.esp"))
                .entries.at(0)
                .chance_none,
            9u);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, fo4::FormKey::parse("000804:Patch.esp"))
                .entries.at(0)
                .chance_none,
            9u);
}

TEST(LeveledList, LegacyFormVersionEditsWriteZeroChanceNoneAndNoOpStaysIdentical) {
  auto plugin = fo4::read_plugin(make_plugin({{"LVLI", k_item_list, legacy_list_subs(), 68}}), "Patch.esp");
  const auto key = item_key();
  const auto original = fo4::test::original_bytes(plugin);
  ASSERT_TRUE(fo4::forms::replace_leveled_list_entries(plugin, key, fo4::forms::decode_leveled_list(plugin, key).entries));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);

  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 1, make_entry(5, "000123:MagicMaster.esp")));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* lvlo = find_subrecord(*record, "LVLO");
  ASSERT_NE(lvlo, nullptr);
  EXPECT_EQ(lvlo->data[10], std::byte{0});
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, key).entries.size(), 2u);
}

TEST(LeveledList, SpellListEntriesRefuseExtraData) {
  auto plugin = fo4::read_plugin(make_plugin({{"LVSP", k_spell_list, legacy_list_subs()}}), "Patch.esp");
  const auto key = fo4::FormKey::parse("000804:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  auto entry = make_entry(2, "000123:MagicMaster.esp");
  entry.extra = fo4::forms::LeveledListExtraData{};
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, key, 1, entry), fo4::forms::LeveledListError);
  EXPECT_THROW(fo4::forms::set_leveled_list_entry(plugin, key, 0, entry), fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, key, 1, make_entry(2, "000123:MagicMaster.esp")));
}

TEST(LeveledList, SettingsNoOpLeavesTheRecordUnchanged) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_TRUE(fo4::forms::set_leveled_list_settings(plugin, item_key(), 25, 0, 5));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledList, ReplaceAcceptsExactly255EntriesAndRoundTrips) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  std::vector<fo4::forms::LeveledListEntry> entries(255);
  for (auto& entry : entries) {
    entry.item = fo4::FormKey::parse("000810:Patch.esp");
  }
  ASSERT_TRUE(fo4::forms::replace_leveled_list_entries(plugin, item_key(), entries));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, item_key()).entries.size(), 255u);
  const auto* record = fo4::find_record(reread, item_key());
  ASSERT_NE(record, nullptr);
  const auto* count = find_subrecord(*record, "LLCT");
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(std::to_integer<std::uint8_t>(count->data[0]), 0xFFu);
}

TEST(LeveledList, InsertIntoA255EntryListIsRefusedWithoutMutation) {
  auto plugin = fo4::read_plugin(standard_plugin(), "Patch.esp");
  std::vector<fo4::forms::LeveledListEntry> entries(255);
  for (auto& entry : entries) {
    entry.item = fo4::FormKey::parse("000810:Patch.esp");
  }
  ASSERT_TRUE(fo4::forms::replace_leveled_list_entries(plugin, item_key(), entries));
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(
                   plugin, item_key(), 255, make_entry(1, "000810:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, item_key()).entries.size(), 255u);
}
