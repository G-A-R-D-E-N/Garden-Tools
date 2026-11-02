#include "fo4/forms/leveled_spell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

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

std::vector<std::byte> make_plugin(bool malformed_lvlo = false) {
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

  const auto group = fo4::test::begin_group(out, "LVSP", 0);
  const auto record = fo4::test::begin_record(out, "LVSP", 0, 0x02000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"RadicleTestList\0", 16});
  std::array<std::byte, 12> bounds{};
  bounds[0] = std::byte{0x44};
  bounds[11] = std::byte{0x55};
  fo4::test::write_subrecord(out, "OBND", bounds);
  const std::array<std::byte, 1> chance{std::byte{25}};
  const std::array<std::byte, 1> max_count{std::byte{0}};
  const std::array<std::byte, 1> flags{std::byte{5}};
  const std::array<std::byte, 1> list_count{std::byte{2}};
  fo4::test::write_subrecord(out, "LVLD", chance);
  fo4::test::write_subrecord(out, "LVLM", max_count);
  fo4::test::write_subrecord(out, "LVLF", flags);
  fo4::test::write_subrecord(out, "LLCT", list_count);
  auto first = lvlo(5, {0xAA, 0xBB}, 0x01000123, 2, 7, 0xCC);
  if (malformed_lvlo) first.pop_back();
  fo4::test::write_subrecord(out, "LVLO", first);
  fo4::test::write_subrecord(out, "LVLO", lvlo(10, {0x11, 0x22}, 0x02000810, 1, 0, 0x33));
  const std::array<std::byte, 3> opaque{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}};
  fo4::test::write_subrecord(out, "UNAM", opaque);
  fo4::test::end_record(out, record);

  const auto nested = fo4::test::begin_record(out, "LVSP", 0, 0x02000810);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NestedList\0", 11});
  const std::array<std::byte, 1> zero{std::byte{0}};
  fo4::test::write_subrecord(out, "LVLD", zero);
  fo4::test::write_subrecord(out, "LVLM", zero);
  fo4::test::write_subrecord(out, "LVLF", zero);
  fo4::test::write_subrecord(out, "LLCT", zero);
  fo4::test::end_record(out, nested);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record,
                                     std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  const auto found = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                  [&](const auto& subrecord) {
                                    return subrecord.signature == wanted;
                                  });
  return found == record.subrecords.end() ? nullptr : &*found;
}

std::vector<std::byte> make_target(std::string_view existing_signature = "SPEL") {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);  // existing top-level group + record
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, existing_signature, 0);
  const auto record =
      fo4::test::begin_record(out, existing_signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(LeveledSpell, DecodesFo4TwelveByteEntriesToStableFormKeys) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto list = fo4::forms::decode_leveled_spell(
      plugin, fo4::FormKey::parse("000800:Patch.esp"));

  EXPECT_EQ(list.chance_none, 25u);
  EXPECT_EQ(list.max_count, 0u);
  EXPECT_EQ(list.flags, 5u);
  ASSERT_EQ(list.entries.size(), 2u);

  EXPECT_EQ(list.entries[0].level, 5u);
  EXPECT_EQ(list.entries[0].spell,
            fo4::FormKey::parse("000123:MagicMaster.esp"));
  EXPECT_EQ(list.entries[0].count, 2u);
  EXPECT_EQ(list.entries[0].chance_none, 7u);
  EXPECT_EQ(list.entries[0].reserved_before_form_id[0], std::byte{0xAA});
  EXPECT_EQ(list.entries[0].reserved_before_form_id[1], std::byte{0xBB});
  EXPECT_EQ(list.entries[0].reserved_tail, std::byte{0xCC});

  EXPECT_EQ(list.entries[1].spell,
            fo4::FormKey::parse("000810:Patch.esp"));
}

TEST(LeveledSpell, ReplacesEntriesAndRoundTripsWithoutTouchingOpaqueSubrecords) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto* before_record = fo4::find_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  const auto* before_bounds = find_subrecord(*before_record, "OBND");
  const auto* before_opaque = find_subrecord(*before_record, "UNAM");
  ASSERT_NE(before_bounds, nullptr);
  ASSERT_NE(before_opaque, nullptr);
  const auto bounds = before_bounds->data;
  const auto opaque = before_opaque->data;

  std::vector<fo4::forms::LeveledSpellEntry> entries(2);
  entries[0].level = 20;
  entries[0].reserved_before_form_id = {std::byte{0xA1}, std::byte{0xA2}};
  entries[0].spell = fo4::FormKey::parse("000777:MagicMaster.esp");
  entries[0].count = 3;
  entries[0].chance_none = 4;
  entries[0].reserved_tail = std::byte{0xA3};
  entries[1].level = 30;
  entries[1].spell = fo4::FormKey::parse("000810:Patch.esp");
  entries[1].count = 1;

  ASSERT_TRUE(fo4::forms::replace_leveled_spell_entries(plugin, key, entries));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto decoded = fo4::forms::decode_leveled_spell(reread, key);
  EXPECT_EQ(decoded.entries, entries);

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* count = find_subrecord(*record, "LLCT");
  ASSERT_NE(count, nullptr);
  ASSERT_EQ(count->data.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(count->data[0]), 2u);
  ASSERT_NE(find_subrecord(*record, "OBND"), nullptr);
  ASSERT_NE(find_subrecord(*record, "UNAM"), nullptr);
  EXPECT_EQ(find_subrecord(*record, "OBND")->data, bounds);
  EXPECT_EQ(find_subrecord(*record, "UNAM")->data, opaque);

  const auto links = fo4::collect_form_links(reread);
  std::vector<fo4::FormKey> lvlo_links;
  for (const auto& link : links) {
    if (link.record == key && link.subrecord.to_string() == "LVLO") {
      lvlo_links.push_back(link.key);
    }
  }
  ASSERT_EQ(lvlo_links.size(), 2u);
  EXPECT_EQ(lvlo_links[0], entries[0].spell);
  EXPECT_EQ(lvlo_links[1], entries[1].spell);
}

TEST(LeveledSpell, EditsListSettingsAndPreservesEntryPayloads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto before = fo4::forms::decode_leveled_spell(plugin, key);

  ASSERT_TRUE(fo4::forms::set_leveled_spell_settings(plugin, key, 50, 0, 3));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto after = fo4::forms::decode_leveled_spell(reread, key);

  EXPECT_EQ(after.chance_none, 50u);
  EXPECT_EQ(after.max_count, 0u);
  EXPECT_EQ(after.flags, 3u);
  EXPECT_EQ(after.entries, before.entries);
}

TEST(LeveledSpell, NoOpEntryReplacementDoesNotDirtyAParsedPlugin) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto decoded = fo4::forms::decode_leveled_spell(plugin, key);
  const auto original = fo4::test::original_bytes(plugin);

  ASSERT_TRUE(fo4::forms::replace_leveled_spell_entries(plugin, key, decoded.entries));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledSpell, RejectsMalformedEntryAndLlctOverflow) {
  const auto malformed = fo4::read_plugin(make_plugin(true), "Patch.esp");
  EXPECT_THROW(
      fo4::forms::decode_leveled_spell(
          malformed, fo4::FormKey::parse("000800:Patch.esp")),
      fo4::forms::LeveledSpellError);

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  std::vector<fo4::forms::LeveledSpellEntry> too_many(256);
  for (auto& item : too_many) {
    item.spell = fo4::FormKey::parse("000810:Patch.esp");
  }
  EXPECT_THROW(
      fo4::forms::replace_leveled_spell_entries(
          plugin, fo4::FormKey::parse("000800:Patch.esp"), too_many),
      fo4::forms::LeveledSpellError);
}

TEST(LeveledSpell, MissingMasterFailureLeavesPluginUntouched) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  auto entries = fo4::forms::decode_leveled_spell(plugin, key).entries;
  entries[0].spell = fo4::FormKey::parse("000123:NotAMaster.esp");

  EXPECT_THROW(fo4::forms::replace_leveled_spell_entries(plugin, key, entries),
               fo4::forms::LeveledSpellError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledSpellCreation, CreatesRequiredStructureAndInitialEntries) {
  auto plugin = fo4::read_plugin(make_target(), "Patch.esp");

  fo4::forms::LeveledSpellEntry entry;
  entry.level = 12;
  entry.spell = fo4::FormKey::parse("000800:Patch.esp");
  entry.count = 2;
  entry.chance_none = 5;
  const std::vector entries{entry};

  const auto key =
      fo4::forms::create_leveled_spell(plugin, 25u, 3u, 0x05u, entries);
  EXPECT_EQ(key, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Patch.esp");
  EXPECT_EQ(reread.header.next_form_id, 0x802u);
  EXPECT_EQ(reread.header.record_count, 4u);  // SPEL GRUP/record + LVSP GRUP/record.

  ASSERT_EQ(reread.groups.size(), 2u);
  EXPECT_EQ(reread.groups[1].header.group_type, 0);
  EXPECT_EQ(reread.groups[1].header.top_label().to_string(), "LVSP");

  const auto decoded = fo4::forms::decode_leveled_spell(reread, key);
  EXPECT_EQ(decoded.chance_none, 25u);
  EXPECT_EQ(decoded.max_count, 3u);
  EXPECT_EQ(decoded.flags, 0x05u);
  ASSERT_EQ(decoded.entries.size(), 1u);
  EXPECT_EQ(decoded.entries[0], entry);

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 6u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "OBND");
  EXPECT_EQ(record->subrecords[0].data, std::vector<std::byte>(12));
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "LVLD");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "LVLM");
  EXPECT_EQ(record->subrecords[3].signature.to_string(), "LVLF");
  EXPECT_EQ(record->subrecords[4].signature.to_string(), "LLCT");
  EXPECT_EQ(record->subrecords[5].signature.to_string(), "LVLO");
  ASSERT_EQ(record->subrecords[4].data.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(record->subrecords[4].data[0]), 1u);
  EXPECT_EQ(record->subrecords[5].data.size(), 12u);
}

TEST(LeveledSpellCreation, InvalidLocalReferenceDoesNotConsumeAFormId) {
  auto plugin = fo4::read_plugin(make_target("MISC"), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  fo4::forms::LeveledSpellEntry entry;
  entry.spell = fo4::FormKey::parse("000800:Patch.esp");
  const std::vector entries{entry};

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_leveled_spell(
          plugin, 0u, 0u, 0u, entries)),
      fo4::forms::LeveledSpellError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  EXPECT_EQ(plugin.groups.size(), 1u);
}
