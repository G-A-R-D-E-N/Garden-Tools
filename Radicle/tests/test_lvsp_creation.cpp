#include "fo4/forms/leveled_spell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

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
  ASSERT_EQ(record->subrecords.size(), 5u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "LVLD");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "LVLM");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "LVLF");
  EXPECT_EQ(record->subrecords[3].signature.to_string(), "LLCT");
  EXPECT_EQ(record->subrecords[4].signature.to_string(), "LVLO");
  ASSERT_EQ(record->subrecords[3].data.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(record->subrecords[3].data[0]), 1u);
  EXPECT_EQ(record->subrecords[4].data.size(), 12u);
}

TEST(LeveledSpellCreation, InvalidLocalReferenceDoesNotConsumeAFormId) {
  auto plugin = fo4::read_plugin(make_target("MISC"), "Patch.esp");
  const auto original = plugin.original_bytes;

  fo4::forms::LeveledSpellEntry entry;
  entry.spell = fo4::FormKey::parse("000800:Patch.esp");
  const std::vector entries{entry};

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_leveled_spell(
          plugin, 0u, 0u, 0u, entries)),
      fo4::forms::LeveledSpellError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  EXPECT_EQ(plugin.groups.size(), 1u);
}
