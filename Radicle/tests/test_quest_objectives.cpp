#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"

#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

namespace {

std::vector<std::byte> u16_of(std::uint16_t value) {
  fo4::test::ByteWriter out;
  out.u16(value);
  return out.bytes;
}

std::vector<std::byte> u32_of(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

// QSTA: int32 alias at 0, uint32 flags at 4, optional keyword FormID at 8.
std::vector<std::byte> qsta(std::int32_t alias, std::uint32_t flags, std::uint32_t keyword) {
  fo4::test::ByteWriter out;
  out.i32(alias);
  out.u32(flags);
  out.u32(keyword);
  return out.bytes;
}

std::vector<std::byte> make_plugin(std::uint32_t keyword_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Alpha.esm\0", 10});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Beta.esm\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x02000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Q\0", 2});

  // Objective 1: two targets, the second carrying a keyword FormLink.
  fo4::test::write_subrecord(out, "QOBJ", u16_of(1));
  fo4::test::write_subrecord(out, "FNAM", u32_of(3));
  fo4::test::write_subrecord(out, "NNAM", u32_of(0xABCD));
  fo4::test::write_subrecord(out, "QSTA", qsta(4, 0, 0));
  fo4::test::write_subrecord(out, "CTDA", std::string_view{"\0\0\0\0", 4});
  fo4::test::write_subrecord(out, "QSTA", qsta(5, 2, keyword_form_id));

  // Objective 2: no targets at all -- 66 shipped objectives look like this.
  fo4::test::write_subrecord(out, "QOBJ", u16_of(2));
  fo4::test::write_subrecord(out, "FNAM", u32_of(0));
  fo4::test::write_subrecord(out, "NNAM", u32_of(0x1111));

  // The alias array terminates the objective walk and reuses NNAM.
  fo4::test::write_subrecord(out, "ANAM", u32_of(1));
  fo4::test::write_subrecord(out, "ALST", u32_of(0));
  fo4::test::write_subrecord(out, "QOBJ", u16_of(99));
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

fo4::FormKey quest_key() { return fo4::FormKey::parse("000800:Quests.esp"); }

}  // namespace

TEST(QuestObjectives, DecodesObjectivesTargetsAndEdgeShapes) {
  // Keyword owned by Beta.esm, which is master index 1.
  auto plugin = fo4::read_plugin(make_plugin(0x01000123), "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_objectives(plugin, quest_key());

  ASSERT_EQ(decoded.objectives.size(), 2u);

  const auto& first = decoded.objectives[0];
  EXPECT_EQ(first.index, 1u);
  EXPECT_EQ(first.flags, 3u);
  ASSERT_TRUE(first.display_text.has_value());
  EXPECT_TRUE(first.display_text->localized());
  EXPECT_EQ(*first.display_text->id, 0xABCDu);
  ASSERT_EQ(first.targets.size(), 2u);

  EXPECT_EQ(first.targets[0].alias, 4);
  EXPECT_EQ(first.targets[0].flags, 0u);
  EXPECT_FALSE(first.targets[0].keyword.has_value());
  ASSERT_EQ(first.targets[0].subrecords.size(), 1u);
  EXPECT_EQ(first.targets[0].subrecords[0].signature.to_string(), "CTDA");

  EXPECT_EQ(first.targets[1].alias, 5);
  EXPECT_EQ(first.targets[1].flags, 2u);
  ASSERT_TRUE(first.targets[1].keyword.has_value());
  EXPECT_EQ(first.targets[1].keyword->to_string(), "000123:Beta.esm");

  const auto& second = decoded.objectives[1];
  EXPECT_EQ(second.index, 2u);
  EXPECT_TRUE(second.targets.empty());
}

TEST(QuestObjectives, StopsAtTheAliasArray) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_objectives(plugin, quest_key());
  for (const auto& objective : decoded.objectives) {
    EXPECT_NE(objective.index, 99u) << "read past ANAM into the alias array";
  }
}

// QSTA's keyword sits at offset 8 inside a mixed-type payload. Without a
// structured FormID slot it would survive a master remap unchanged and end up
// pointing at the wrong plugin -- and only 2 of 1,500 shipped targets carry
// one, so the corruption would be easy to miss.
TEST(QuestObjectives, TargetKeywordIsRemappedWithTheMasterTable) {
  auto plugin = fo4::read_plugin(make_plugin(0x01000123), "Quests.esp");

  auto remapped = fo4::remap_plugin_masters(
      plugin, std::vector<fo4::PluginKey>{
                  fo4::PluginKey::from_filename("Beta.esm"),
                  fo4::PluginKey::from_filename("Alpha.esm"),
              });

  const auto decoded = fo4::forms::decode_quest_objectives(
      remapped, fo4::FormKey::parse("000800:Quests.esp"));
  ASSERT_EQ(decoded.objectives.size(), 2u);
  ASSERT_EQ(decoded.objectives[0].targets.size(), 2u);
  const auto& keyword = decoded.objectives[0].targets[1].keyword;
  ASSERT_TRUE(keyword.has_value());
  // Beta.esm moved to index 0; the logical identity must be unchanged.
  EXPECT_EQ(keyword->to_string(), "000123:Beta.esm");

  const auto* record = fo4::find_record(remapped, fo4::FormKey::parse("000800:Quests.esp"));
  ASSERT_NE(record, nullptr);
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != "QSTA") continue;
    std::uint32_t raw = 0;
    std::memcpy(&raw, sub.data.data() + 8, 4);
    if (raw == 0) continue;
    // The on-disk index byte must have followed the master reorder.
    EXPECT_EQ(raw >> 24, 0u) << "QSTA keyword kept a stale master index";
  }
}

TEST(QuestObjectives, RejectsMalformedMarkers) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "QOBJ", u16_of(1));
  fo4::test::write_subrecord(out, "FNAM", u32_of(0));
  fo4::test::write_subrecord(out, "QSTA", std::string_view{"\0\0\0\0", 4});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Quests.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_quest_objectives(plugin, quest_key())),
               fo4::forms::QuestError);
}
