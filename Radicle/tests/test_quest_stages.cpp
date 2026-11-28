#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/ctda.hpp"

#include "plugin_bytes.hpp"

#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace {

void u16le(fo4::test::ByteWriter& out, std::uint16_t value) { out.u16(value); }

// INDX is u16 index, u8 flags, u8 unknown.
std::vector<std::byte> indx(std::uint16_t index, std::uint8_t flags, std::uint8_t unknown) {
  fo4::test::ByteWriter out;
  u16le(out, index);
  out.u8(flags);
  out.u8(unknown);
  return out.bytes;
}

std::vector<std::byte> byte_of(std::uint8_t value) {
  fo4::test::ByteWriter out;
  out.u8(value);
  return out.bytes;
}

std::vector<std::byte> u32_of(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> make_quest_plugin(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestQuest\0", 10});

  // Stage 10: two log entries, the second completing the quest.
  fo4::test::write_subrecord(out, "INDX", indx(10, 0, 7));
  fo4::test::write_subrecord(out, "QSDT", byte_of(0));
  fo4::test::write_subrecord(out, "CNAM", u32_of(0x1234));
  fo4::test::write_subrecord(out, "NAM2", std::string_view{"note text\0", 10});
  fo4::test::write_subrecord(out, "QSDT", byte_of(1));
  fo4::test::write_subrecord(out, "CTDA", std::string_view{"\0\0\0\0", 4});

  // Stage 20: no log entries at all -- 63 shipped stages look like this.
  fo4::test::write_subrecord(out, "INDX", indx(20, 2, 0));

  // The alias array terminates the stage walk.
  fo4::test::write_subrecord(out, "ANAM", u32_of(3));
  fo4::test::write_subrecord(out, "ALST", u32_of(0));
  fo4::test::write_subrecord(out, "INDX", indx(99, 0, 0));  // must not be read as a stage
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

const fo4::FormKey quest_key() { return fo4::FormKey::parse("000800:Quests.esp"); }

}  // namespace

TEST(QuestStages, DecodesStagesEntriesAndEdgeShapes) {
  auto plugin = fo4::read_plugin(make_quest_plugin(0x80), "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_stages(plugin, quest_key());

  ASSERT_EQ(decoded.stages.size(), 2u);

  const auto& first = decoded.stages[0];
  EXPECT_EQ(first.index, 10u);
  EXPECT_EQ(first.flags, 0u);
  // The fourth INDX byte is not always zero in shipped data.
  EXPECT_EQ(first.unknown, 7u);
  ASSERT_EQ(first.log_entries.size(), 2u);

  EXPECT_EQ(first.log_entries[0].flags, 0u);
  ASSERT_TRUE(first.log_entries[0].log_text.has_value());
  EXPECT_TRUE(first.log_entries[0].log_text->localized());
  EXPECT_EQ(*first.log_entries[0].log_text->id, 0x1234u);
  ASSERT_TRUE(first.log_entries[0].note.has_value());
  EXPECT_EQ(*first.log_entries[0].note, "note text");

  // QSDT bit 0 completes the quest.
  EXPECT_EQ(first.log_entries[1].flags, 1u);
  EXPECT_FALSE(first.log_entries[1].log_text.has_value());
  // Members are preserved verbatim, in file order, rather than modelled away.
  ASSERT_EQ(first.log_entries[1].members.size(), 1u);
  EXPECT_EQ(first.log_entries[1].members[0].signature.to_string(), "CTDA");

  const auto& second = decoded.stages[1];
  EXPECT_EQ(second.index, 20u);
  EXPECT_EQ(second.flags, 2u);
  EXPECT_TRUE(second.log_entries.empty());
}

// The alias array reuses INDX; the walk must stop at ANAM.
TEST(QuestStages, StopsAtTheAliasArray) {
  auto plugin = fo4::read_plugin(make_quest_plugin(0x80), "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_stages(plugin, quest_key());
  for (const auto& stage : decoded.stages) {
    EXPECT_NE(stage.index, 99u) << "read an alias-array INDX as a stage";
  }
}

TEST(QuestStages, NonLocalizedPluginKeepsInlineLogText) {
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
  fo4::test::write_subrecord(out, "INDX", indx(1, 0, 0));
  fo4::test::write_subrecord(out, "QSDT", byte_of(0));
  fo4::test::write_subrecord(out, "CNAM", std::string_view{"Find the thing\0", 15});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_stages(plugin, quest_key());
  ASSERT_EQ(decoded.stages.size(), 1u);
  ASSERT_EQ(decoded.stages[0].log_entries.size(), 1u);
  const auto& text = decoded.stages[0].log_entries[0].log_text;
  ASSERT_TRUE(text.has_value());
  EXPECT_FALSE(text->localized());
  EXPECT_EQ(text->inline_text(), "Find the thing");
}

static std::vector<std::byte> localized_cnam_plugin(const std::vector<std::byte>& cnam) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "INDX", indx(1, 0, 0));
  fo4::test::write_subrecord(out, "QSDT", byte_of(0));
  fo4::test::write_subrecord(out, "CNAM", cnam);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

TEST(QuestStages, LocalizedLogTextMustBeFourBytes) {
  const auto short_bytes = localized_cnam_plugin({std::byte{0x34}, std::byte{0x12}});
  const auto short_plugin = fo4::read_plugin(short_bytes, "Quests.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_quest_stages(short_plugin, quest_key())),
               fo4::forms::QuestError);

  const auto full_bytes = localized_cnam_plugin(u32_of(0x1234));
  const auto full_plugin = fo4::read_plugin(full_bytes, "Quests.esp");
  const auto decoded = fo4::forms::decode_quest_stages(full_plugin, quest_key());
  ASSERT_EQ(decoded.stages.size(), 1u);
  ASSERT_EQ(decoded.stages[0].log_entries.size(), 1u);
  const auto& text = decoded.stages[0].log_entries[0].log_text;
  ASSERT_TRUE(text.has_value());
  ASSERT_TRUE(text->localized());
  EXPECT_EQ(*text->id, 0x1234u);
}

TEST(QuestStages, RejectsMalformedMarkers) {
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
  fo4::test::write_subrecord(out, "INDX", std::string_view{"\0\0", 2});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Quests.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_quest_stages(plugin, quest_key())),
               fo4::forms::QuestError);
}

TEST(QuestStages, LosslessLogEntryConditionsRoundTripCtdaAndCis) {
  fo4::CtdaCondition condition;
  condition.function = 1;
  condition.unknown3 = -1;
  condition.param1 = 1;
  condition.parameter1_string = "KeywordOne";
  condition.parameter2_string = "KeywordTwo";
  auto members = fo4::serialize_ctda_run(std::span<const fo4::CtdaCondition>(&condition, 1));
  fo4::Subrecord nam2;
  nam2.signature = fo4::Signature::from_string("NAM2");
  nam2.data = {std::byte{'n'}, std::byte{0}};
  members.push_back(nam2);

  std::vector<fo4::CtdaCondition> decoded;
  std::size_t consumed = 0;
  ASSERT_TRUE(fo4::forms::lossless_quest_log_entry_conditions(members, decoded, consumed));
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_EQ(decoded[0].function, 1u);
  EXPECT_EQ(*decoded[0].parameter1_string, "KeywordOne");
  EXPECT_EQ(*decoded[0].parameter2_string, "KeywordTwo");
  EXPECT_EQ(consumed, 3u);

  fo4::Subrecord short_ctda;
  short_ctda.signature = fo4::Signature::from_string("CTDA");
  short_ctda.data = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  std::vector<fo4::Subrecord> bad{short_ctda};
  EXPECT_FALSE(fo4::forms::lossless_quest_log_entry_conditions(bad, decoded, consumed));
}

TEST(QuestStages, LosslessLogEntryConditionsRetainPaddingBytes) {
  fo4::CtdaCondition condition;
  condition.function = 74;
  auto members = fo4::serialize_ctda_run(std::span<const fo4::CtdaCondition>(&condition, 1));
  members[0].data[1] = std::byte{0xAC};
  members[0].data[2] = std::byte{0x93};
  members[0].data[3] = std::byte{0x13};
  std::vector<fo4::CtdaCondition> decoded;
  std::size_t consumed = 0;
  ASSERT_TRUE(fo4::forms::lossless_quest_log_entry_conditions(members, decoded, consumed));
  EXPECT_EQ(consumed, 1u);
  EXPECT_EQ(fo4::serialize_ctda_run(decoded)[0].data, members[0].data);
}
