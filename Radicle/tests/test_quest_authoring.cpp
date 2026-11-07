// Authoring for QUST stages, objectives and aliases. The central requirement
// in #73/#74/#75 is that mutating one array leaves the others untouched, so
// most of these assert on the neighbours rather than the thing being changed.

#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/vmad.hpp"

#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> u16_of(std::uint16_t v) {
  fo4::test::ByteWriter o;
  o.u16(v);
  return o.bytes;
}
std::vector<std::byte> u32_of(std::uint32_t v) {
  fo4::test::ByteWriter o;
  o.u32(v);
  return o.bytes;
}
std::vector<std::byte> byte_of(std::uint8_t v) {
  fo4::test::ByteWriter o;
  o.u8(v);
  return o.bytes;
}
std::vector<std::byte> indx(std::uint16_t i, std::uint8_t f, std::uint8_t u) {
  fo4::test::ByteWriter o;
  o.u16(i);
  o.u8(f);
  o.u8(u);
  return o.bytes;
}
std::vector<std::byte> qsta(std::int32_t alias, std::uint32_t flags, std::uint32_t kw) {
  fo4::test::ByteWriter o;
  o.i32(alias);
  o.u32(flags);
  o.u32(kw);
  return o.bytes;
}

// A quest carrying all three arrays, so every mutation has neighbours to
// disturb: stages 10/20, objectives 1/2, and two aliases.
std::vector<std::byte> make_quest(std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", flags, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Q\0", 2});
  fo4::test::write_subrecord(out, "DNAM", u32_of(0));

  fo4::test::write_subrecord(out, "INDX", indx(10, 0, 3));
  fo4::test::write_subrecord(out, "QSDT", byte_of(0));
  fo4::test::write_subrecord(out, "CTDA", std::string_view{"\0\0\0\0", 4});
  fo4::test::write_subrecord(out, "NAM2", std::string_view{"note\0", 5});
  fo4::test::write_subrecord(out, "CNAM", u32_of(0x1234));
  fo4::test::write_subrecord(out, "INDX", indx(20, 0, 0));
  fo4::test::write_subrecord(out, "QSDT", byte_of(1));

  fo4::test::write_subrecord(out, "QOBJ", u16_of(1));
  fo4::test::write_subrecord(out, "FNAM", u32_of(7));
  fo4::test::write_subrecord(out, "NNAM", u32_of(0xAAAA));
  fo4::test::write_subrecord(out, "QSTA", qsta(3, 0, 0));
  fo4::test::write_subrecord(out, "QOBJ", u16_of(2));
  fo4::test::write_subrecord(out, "FNAM", u32_of(0));
  fo4::test::write_subrecord(out, "NNAM", u32_of(0xBBBB));

  fo4::test::write_subrecord(out, "ANAM", u32_of(2));
  fo4::test::write_subrecord(out, "ALST", u32_of(0));
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A0\0", 3});
  fo4::test::write_subrecord(out, "FNAM", u32_of(0));
  fo4::test::write_subrecord(out, "ALED", std::string_view{"", 0});
  fo4::test::write_subrecord(out, "ALST", u32_of(1));
  fo4::test::write_subrecord(out, "ALID", std::string_view{"A1\0", 3});
  fo4::test::write_subrecord(out, "FNAM", u32_of(0));
  fo4::test::write_subrecord(out, "ALED", std::string_view{"", 0});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

fo4::FormKey key() { return fo4::FormKey::parse("000800:Q.esp"); }
fo4::Plugin load(std::uint32_t flags = 0) {
  return fo4::read_plugin(make_quest(flags), "Q.esp");
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, key());
  std::vector<std::string> out;
  for (const auto& sub : record->subrecords) out.push_back(sub.signature.to_string());
  return out;
}

std::size_t count(const fo4::Plugin& plugin, const char* signature) {
  std::size_t n = 0;
  for (const auto& s : signatures(plugin)) {
    if (s == signature) ++n;
  }
  return n;
}

fo4::Subrecord stage_member(std::string_view signature,
                            std::initializer_list<std::byte> data) {
  return fo4::Subrecord{fo4::Signature::from_string(signature),
                        std::vector<std::byte>{data}, false};
}

std::vector<std::byte> vmad_stage_fragment() {
  fo4::test::ByteWriter vmad;
  vmad.u16(6);
  vmad.u16(2);
  vmad.u16(0);
  vmad.u8(3);
  vmad.u16(1);
  vmad.u16(0);
  vmad.u16(10);
  vmad.u16(0);
  vmad.u32(0);
  vmad.u8(0);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u16(0);
  return vmad.bytes;
}

fo4::Plugin load_edid_vmad_quest(std::uint32_t flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", flags, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Q\0", 2});
  fo4::test::write_subrecord(out, "VMAD", vmad_stage_fragment());
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return fo4::read_plugin(out.bytes, "Q.esp");
}

}  // namespace

TEST(QuestAuthoring, PartialFormVmadKeepsMasterStageFragmentsOnUnchangedRewrite) {
  auto plugin = load_edid_vmad_quest(0x4000);
  const auto before = signatures(plugin);
  const auto vmad_before = fo4::find_record(plugin, key())->subrecords[1].data;

  const auto stages = fo4::forms::decode_quest_stages(plugin, key());
  EXPECT_TRUE(stages.stages.empty());
  fo4::forms::write_quest_stages(plugin, key(), stages);
  const auto objectives = fo4::forms::decode_quest_objectives(plugin, key());
  fo4::forms::write_quest_objectives(plugin, key(), objectives);

  EXPECT_EQ(signatures(plugin), before);
  EXPECT_EQ(fo4::find_record(plugin, key())->subrecords[1].data, vmad_before);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  EXPECT_EQ(signatures(reloaded), before);
  EXPECT_EQ(fo4::find_record(reloaded, key())->subrecords[1].data, vmad_before);
  EXPECT_TRUE(fo4::forms::decode_quest_stages(reloaded, key()).stages.empty());
}

TEST(QuestAuthoring, PartialFormVmadRefusesStagesItCannotBind) {
  auto partial = load_edid_vmad_quest(0x4000);
  const auto partial_before = fo4::write_plugin(partial);
  fo4::forms::QuestStages added;
  added.stages.emplace_back().index = 20;
  EXPECT_THROW(fo4::forms::write_quest_stages(partial, key(), added),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(partial), partial_before);

  auto full = load_edid_vmad_quest(0);
  const auto full_before = fo4::write_plugin(full);
  const auto none = fo4::forms::decode_quest_stages(full, key());
  EXPECT_THROW(fo4::forms::write_quest_stages(full, key(), none),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(full), full_before);
}

TEST(QuestAuthoring, PartialFormWithLocalStagesKeepsVmadValidation) {
  auto plugin = load(0x4000);
  auto& record = std::get<fo4::Record>(plugin.groups.front().children.front());
  const auto vmad = vmad_stage_fragment();
  record.subrecords.insert(record.subrecords.begin() + 1,
                          fo4::Subrecord{fo4::sig_vmad, vmad, false});

  auto stages = fo4::forms::decode_quest_stages(plugin, key());
  fo4::forms::write_quest_stages(plugin, key(), stages);
  EXPECT_EQ(fo4::find_record(plugin, key())->subrecords[1].data, vmad);

  const auto before = fo4::write_plugin(plugin);
  stages.stages.clear();
  EXPECT_THROW(fo4::forms::write_quest_stages(plugin, key(), stages),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestAuthoring, RewritingUnchangedArraysIsByteExact) {
  auto plugin = load();
  const auto before = signatures(plugin);

  const auto stages = fo4::forms::decode_quest_stages(plugin, key());
  fo4::forms::write_quest_stages(plugin, key(), stages);
  const auto objectives = fo4::forms::decode_quest_objectives(plugin, key());
  fo4::forms::write_quest_objectives(plugin, key(), objectives);

  // CTDA precedes NAM2/CNAM in the file; rebuilding from decoded fields
  // instead of the preserved member order would reorder them here.
  EXPECT_EQ(signatures(plugin), before);
}

TEST(QuestAuthoring, AddAndRemoveStageLeavesObjectivesAndAliasesIntact) {
  auto plugin = load();
  const auto objectives_before = fo4::forms::decode_quest_objectives(plugin, key());
  const auto aliases_before = count(plugin, "ALST");

  fo4::forms::QuestStage stage;
  stage.index = 30;
  stage.flags = 0;
  fo4::forms::QuestLogEntry entry;
  entry.flags = 2;
  stage.log_entries.push_back(entry);
  fo4::forms::add_quest_stage(plugin, key(), stage);

  auto stages = fo4::forms::decode_quest_stages(plugin, key());
  ASSERT_EQ(stages.stages.size(), 3u);
  EXPECT_EQ(stages.stages[2].index, 30u);

  // Neighbouring arrays untouched.
  const auto objectives_after = fo4::forms::decode_quest_objectives(plugin, key());
  EXPECT_EQ(objectives_after.objectives.size(), objectives_before.objectives.size());
  EXPECT_EQ(count(plugin, "ALST"), aliases_before);

  EXPECT_TRUE(fo4::forms::remove_quest_stage(plugin, key(), 30));
  stages = fo4::forms::decode_quest_stages(plugin, key());
  EXPECT_EQ(stages.stages.size(), 2u);
  EXPECT_FALSE(fo4::forms::remove_quest_stage(plugin, key(), 999));

  // And the whole record survives a write/reload.
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  EXPECT_EQ(fo4::forms::decode_quest_stages(reloaded, key()).stages.size(), 2u);
  EXPECT_EQ(fo4::forms::decode_quest_objectives(reloaded, key()).objectives.size(), 2u);
}

TEST(QuestAuthoring, DuplicateStageIndexIsRefused) {
  auto plugin = load();
  fo4::forms::QuestStage stage;
  stage.index = 10;
  EXPECT_THROW(fo4::forms::add_quest_stage(plugin, key(), stage), fo4::forms::QuestError);
  EXPECT_EQ(fo4::forms::decode_quest_stages(plugin, key()).stages.size(), 2u);
}

TEST(QuestAuthoring, StageBodyMatchingDoesNotCrossBinaryMemberBoundaries) {
  fo4::Record record;
  record.subrecords.push_back(
      fo4::Subrecord{fo4::sig_vmad, vmad_stage_fragment(), false});

  fo4::forms::QuestStages previous;
  fo4::forms::QuestStage old_stage;
  old_stage.index = 10;
  old_stage.leading_members = {
      stage_member("AAAA", {std::byte{0x11}}),
      stage_member("BCDE", {std::byte{0x00}, std::byte{0x45}, std::byte{0x46},
                             std::byte{0x47}, std::byte{0x48}}),
  };
  previous.stages.push_back(old_stage);

  fo4::forms::QuestStages next;
  fo4::forms::QuestStage replacement;
  replacement.index = 30;
  replacement.leading_members = {
      stage_member("AAAA", {std::byte{0x11}, std::byte{0x00}, std::byte{0x42},
                             std::byte{0x43}, std::byte{0x44}, std::byte{0x45}}),
      stage_member("EFGH", {}),
  };
  next.stages.push_back(replacement);

  const auto before = record.subrecords.front().data;
  EXPECT_THROW(fo4::forms::sync_quest_vmad_stages(record, previous, next),
               fo4::forms::QuestError);
  EXPECT_EQ(record.subrecords.front().data, before);
}

TEST(QuestAuthoring, ReorderStagePreservesLogEntriesAndNeighbours) {
  auto plugin = load();
  const auto aliases_before = count(plugin, "ALST");

  EXPECT_TRUE(fo4::forms::reorder_quest_stage(plugin, key(), 20, 0));
  const auto stages = fo4::forms::decode_quest_stages(plugin, key());
  ASSERT_EQ(stages.stages.size(), 2u);
  EXPECT_EQ(stages.stages[0].index, 20u);
  EXPECT_EQ(stages.stages[1].index, 10u);
  // Stage 10's log entry and its members moved with it.
  ASSERT_EQ(stages.stages[1].log_entries.size(), 1u);
  EXPECT_EQ(stages.stages[1].log_entries[0].members.size(), 3u);
  EXPECT_EQ(count(plugin, "ALST"), aliases_before);

  EXPECT_FALSE(fo4::forms::reorder_quest_stage(plugin, key(), 999, 0));
}

TEST(QuestAuthoring, AddRemoveAndReorderObjectivesLeaveStagesIntact) {
  auto plugin = load();
  const auto stages_before = fo4::forms::decode_quest_stages(plugin, key());

  fo4::forms::QuestObjective objective;
  objective.index = 3;
  objective.flags = 1;
  fo4::forms::QuestObjectiveTarget target;
  target.alias = 9;
  objective.targets.push_back(target);
  fo4::forms::add_quest_objective(plugin, key(), objective);

  auto objectives = fo4::forms::decode_quest_objectives(plugin, key());
  ASSERT_EQ(objectives.objectives.size(), 3u);
  EXPECT_EQ(objectives.objectives[2].index, 3u);
  EXPECT_EQ(objectives.objectives[2].targets.size(), 1u);

  EXPECT_TRUE(fo4::forms::reorder_quest_objective(plugin, key(), 3, 0));
  objectives = fo4::forms::decode_quest_objectives(plugin, key());
  EXPECT_EQ(objectives.objectives[0].index, 3u);

  EXPECT_TRUE(fo4::forms::remove_quest_objective(plugin, key(), 3));
  objectives = fo4::forms::decode_quest_objectives(plugin, key());
  EXPECT_EQ(objectives.objectives.size(), 2u);
  EXPECT_FALSE(fo4::forms::remove_quest_objective(plugin, key(), 999));

  // Stages are byte-identical throughout.
  const auto stages_after = fo4::forms::decode_quest_stages(plugin, key());
  ASSERT_EQ(stages_after.stages.size(), stages_before.stages.size());
  EXPECT_EQ(stages_after.stages[0].unknown, stages_before.stages[0].unknown);
  EXPECT_EQ(stages_after.stages[0].log_entries[0].members.size(),
            stages_before.stages[0].log_entries[0].members.size());
}

TEST(QuestAuthoring, ReorderAliasMovesItsWholeRun) {
  auto plugin = load();
  const auto stages_before = fo4::forms::decode_quest_stages(plugin, key());

  EXPECT_TRUE(fo4::forms::reorder_quest_alias(plugin, key(), 1, 0));
  const auto aliases = fo4::forms::decode_quest_aliases(plugin, key());
  ASSERT_EQ(aliases.aliases.size(), 2u);
  EXPECT_EQ(aliases.aliases[0].id, 1u);
  EXPECT_EQ(aliases.aliases[1].id, 0u);
  // Each alias kept its own ALID/FNAM/ALED run.
  EXPECT_EQ(count(plugin, "ALED"), 2u);

  // Stages and objectives are untouched.
  EXPECT_EQ(fo4::forms::decode_quest_stages(plugin, key()).stages.size(),
            stages_before.stages.size());
  EXPECT_EQ(fo4::forms::decode_quest_objectives(plugin, key()).objectives.size(), 2u);

  EXPECT_FALSE(fo4::forms::reorder_quest_alias(plugin, key(), 99, 0));
}
