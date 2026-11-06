#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  std::string_view link = {}, std::uint32_t target = 0) {
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  if (!link.empty()) {
    fo4::test::ByteWriter value;
    value.u32(target);
    fo4::test::write_subrecord(out, link, value.bytes);
  }
  fo4::test::end_record(out, record);
}

void write_topic_children(fo4::test::ByteWriter& out, std::uint32_t dial,
                          std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> infos) {
  const auto group = fo4::test::begin_group(out, "", 7);
  out.patch_u32(group + 8, dial);
  for (const auto& [form_id, previous] : infos) {
    write_record(out, "INFO", form_id, previous == 0 ? std::string_view{} : "PNAM", previous);
  }
  fo4::test::end_group(out, group);
}

fo4::Plugin make_topic_plugin(std::uint32_t late_previous) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  write_record(out, "DIAL", 0x800);
  write_record(out, "DIAL", 0x803);
  write_topic_children(out, 0x800, {{0x801, 0}, {0x802, 0x801}});
  write_topic_children(out, 0x803, {{0x804, late_previous}, {0x805, 0}});
  fo4::test::end_group(out, dial_top);
  return fo4::read_plugin(out.bytes, "Topics.esp");
}

fo4::Plugin make_orphan_topic_plugin() {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto qust_top = fo4::test::begin_group(out, "QUST", 0);
  write_record(out, "QUST", 0x810);
  fo4::test::end_group(out, qust_top);
  write_topic_children(out, 0x810, {{0x811, 0}});
  return fo4::read_plugin(out.bytes, "Orphan.esp");
}

fo4::Plugin make_branch_plugin(std::uint32_t starting_topic, std::uint32_t dial_flags = 0) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", dial_flags, 0x800);
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, dial_top);
  const auto qust_top = fo4::test::begin_group(out, "QUST", 0);
  write_record(out, "QUST", 0x821);
  fo4::test::end_group(out, qust_top);
  const auto dlbr_top = fo4::test::begin_group(out, "DLBR", 0);
  write_record(out, "DLBR", 0x820, "SNAM", starting_topic);
  fo4::test::end_group(out, dlbr_top);
  return fo4::read_plugin(out.bytes, "Branch.esp");
}

fo4::Plugin make_bad_master_index_plugin() {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dlbr_top = fo4::test::begin_group(out, "DLBR", 0);
  write_record(out, "DLBR", 0x820, "SNAM", 0x05000800);
  fo4::test::end_group(out, dlbr_top);
  write_topic_children(out, 0x05000810, {{0x811, 0}});
  return fo4::read_plugin(out.bytes, "BadMaster.esp");
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

void write_u32_subrecord(fo4::test::ByteWriter& out, std::string_view tag, std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, tag, data.bytes);
}

void write_i32_subrecord(fo4::test::ByteWriter& out, std::string_view tag, std::int32_t value) {
  fo4::test::ByteWriter data;
  data.i32(value);
  fo4::test::write_subrecord(out, tag, data.bytes);
}

void write_empty_subrecord(fo4::test::ByteWriter& out, std::string_view tag) {
  fo4::test::write_subrecord(out, tag, std::string_view{});
}

fo4::Plugin make_info_count_plugin(std::uint32_t declared, std::size_t stored) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x800);
  write_u32_subrecord(out, "TIFC", declared);
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, dial_top);
  const auto topics = fo4::test::begin_group(out, "", 7);
  out.patch_u32(topics + 8, 0x800);
  for (std::size_t index = 0; index < stored; ++index) {
    write_record(out, "INFO", 0x801 + static_cast<std::uint32_t>(index));
  }
  fo4::test::end_group(out, topics);
  return fo4::read_plugin(out.bytes, "InfoCount.esp");
}

fo4::Plugin make_previous_info_plugin(bool cyclic) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  write_record(out, "DIAL", 0x800);
  fo4::test::end_group(out, dial_top);
  write_topic_children(out, 0x800, {{0x801, 0x802}, {0x802, cyclic ? 0x801u : 0u}});
  return fo4::read_plugin(out.bytes, "PreviousInfo.esp");
}

fo4::Plugin make_previous_info_topics_plugin(bool same_topic) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  write_record(out, "DIAL", 0x800);
  write_record(out, "DIAL", 0x803);
  fo4::test::end_group(out, dial_top);
  if (same_topic) {
    write_topic_children(out, 0x800, {{0x801, 0x802}, {0x802, 0}});
  } else {
    write_topic_children(out, 0x800, {{0x801, 0x802}});
    write_topic_children(out, 0x803, {{0x802, 0}});
  }
  return fo4::read_plugin(out.bytes, "PreviousTopic.esp");
}

fo4::Plugin make_branch_quest_plugin(std::uint32_t dial_quest, std::uint32_t branch_quest,
                                     std::uint32_t category) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto qust_top = fo4::test::begin_group(out, "QUST", 0);
  write_record(out, "QUST", 0x821);
  write_record(out, "QUST", 0x822);
  fo4::test::end_group(out, qust_top);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x800);
  write_u32_subrecord(out, "BNAM", 0x820);
  write_u32_subrecord(out, "QNAM", dial_quest);
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, dial_top);
  const auto dlbr_top = fo4::test::begin_group(out, "DLBR", 0);
  const auto dlbr = fo4::test::begin_record(out, "DLBR", 0, 0x820);
  write_u32_subrecord(out, "QNAM", branch_quest);
  write_u32_subrecord(out, "TNAM", category);
  fo4::test::end_record(out, dlbr);
  fo4::test::end_group(out, dlbr_top);
  return fo4::read_plugin(out.bytes, "BranchQuest.esp");
}

struct SceneActionSpec {
  std::uint16_t type{0};
  std::int32_t alias{-1};
  std::optional<std::uint32_t> index{std::uint32_t{0}};
  std::optional<std::uint32_t> start;
  std::optional<std::uint32_t> end;
  std::vector<std::pair<std::string_view, std::uint32_t>> payload;
};

struct SceneSpec {
  std::size_t phases{0};
  std::vector<std::int32_t> actors;
  std::vector<SceneActionSpec> actions;
  bool has_parent{true};
  std::uint32_t parent{0x810};
  bool has_last_index{true};
  std::uint32_t last_index{0};
};

fo4::Plugin make_scene_plugin(const SceneSpec& spec) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto qust_top = fo4::test::begin_group(out, "QUST", 0);
  write_record(out, "QUST", 0x810);
  fo4::test::end_group(out, qust_top);
  const auto dial_top = fo4::test::begin_group(out, "DIAL", 0);
  write_record(out, "DIAL", 0x800);
  fo4::test::end_group(out, dial_top);
  const auto scen_top = fo4::test::begin_group(out, "SCEN", 0);
  const auto scene = fo4::test::begin_record(out, "SCEN", 0, 0x900);
  for (std::size_t phase = 0; phase < spec.phases; ++phase) {
    write_empty_subrecord(out, "HNAM");
    write_empty_subrecord(out, "NEXT");
    write_empty_subrecord(out, "NEXT");
    write_empty_subrecord(out, "HNAM");
  }
  for (const auto alias : spec.actors) {
    write_i32_subrecord(out, "ALID", alias);
    write_u32_subrecord(out, "LNAM", 0);
    write_u32_subrecord(out, "DNAM", 26);
  }
  for (const auto& action : spec.actions) {
    fo4::test::ByteWriter type;
    type.u16(action.type);
    fo4::test::write_subrecord(out, "ANAM", type.bytes);
    write_i32_subrecord(out, "ALID", action.alias);
    if (action.index) write_u32_subrecord(out, "INAM", *action.index);
    if (action.start) write_u32_subrecord(out, "SNAM", *action.start);
    if (action.end) write_u32_subrecord(out, "ENAM", *action.end);
    for (const auto& [tag, target] : action.payload) write_u32_subrecord(out, tag, target);
    write_empty_subrecord(out, "ANAM");
  }
  if (spec.has_parent) write_u32_subrecord(out, "PNAM", spec.parent);
  if (spec.has_last_index) write_u32_subrecord(out, "INAM", spec.last_index);
  fo4::test::end_record(out, scene);
  fo4::test::end_group(out, scen_top);
  return fo4::read_plugin(out.bytes, "Scene.esp");
}

}  // namespace

TEST(CheckDialogue, TopicChildrenLabelThatIsNotADialIsReported) {
  auto plugin = make_orphan_topic_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000811:Orphan.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000810:Orphan.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, TopicChildrenLabelOfADialIsValid) {
  auto plugin = make_topic_plugin(0x805);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::topic_children_not_dial).empty());
}

TEST(CheckDialogue, BranchStartingTopicThatIsNotADialIsReported) {
  auto plugin = make_branch_plugin(0x821);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000820:Branch.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000821:Branch.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "SNAM");
}

TEST(CheckDialogue, BranchStartingTopicThatIsADialIsValid) {
  auto plugin = make_branch_plugin(0x800);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::branch_starting_topic_not_dial).empty());
}

TEST(CheckDialogue, BranchStartingTopicThatIsDeletedIsWarned) {
  auto plugin = make_branch_plugin(0x800, 0x20);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::branch_starting_topic_not_dial).empty());
  const auto deleted = of_code(findings, fo4::CheckCode::deleted_target);
  ASSERT_EQ(deleted.size(), 1u);
  EXPECT_EQ(deleted[0].record, fo4::FormKey::parse("000820:Branch.esp"));
  EXPECT_EQ(deleted[0].target, fo4::FormKey::parse("000800:Branch.esp"));
  EXPECT_EQ(deleted[0].subrecord.to_string(), "SNAM");
  EXPECT_EQ(deleted[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, MasterIndexPastTheMasterTableIsReported) {
  auto plugin = make_bad_master_index_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::branch_starting_topic_not_dial).empty());
  const auto children = of_code(findings, fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(children.size(), 1u);
  const auto bad_index = of_code(findings, fo4::CheckCode::invalid_master_index);
  ASSERT_EQ(bad_index.size(), 1u);
  EXPECT_EQ(bad_index[0].subrecord.to_string(), "SNAM");
}

TEST(CheckDialogue, InfoCountMismatchIsWarned) {
  auto plugin = make_info_count_plugin(3, 2);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::dialogue_info_count_mismatch);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:InfoCount.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "TIFC");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
  EXPECT_NE(findings[0].message.find("3"), std::string::npos);
  EXPECT_NE(findings[0].message.find("2"), std::string::npos);
}

TEST(CheckDialogue, InfoCountThatMatchesIsValid) {
  auto plugin = make_info_count_plugin(2, 2);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::dialogue_info_count_mismatch).empty());
}

TEST(CheckDialogue, PreviousInfoCycleIsReported) {
  auto plugin = make_previous_info_plugin(true);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::dialogue_previous_info_cycle);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:PreviousInfo.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
  EXPECT_NE(findings[0].message.find("000801:PreviousInfo.esp"), std::string::npos);
  EXPECT_NE(findings[0].message.find("000802:PreviousInfo.esp"), std::string::npos);
}

TEST(CheckDialogue, PreviousInfoChainThatEndsIsValid) {
  auto plugin = make_previous_info_plugin(false);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::dialogue_previous_info_cycle).empty());
}

TEST(CheckDialogue, PreviousInfoFromAnotherTopicIsWarned) {
  auto plugin = make_previous_info_topics_plugin(false);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::dialogue_previous_info_topic_mismatch);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:PreviousTopic.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000802:PreviousTopic.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "PNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, PreviousInfoInTheSameTopicIsValid) {
  auto plugin = make_previous_info_topics_plugin(true);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::dialogue_previous_info_topic_mismatch).empty());
}

TEST(CheckDialogue, BranchQuestThatDisagreesIsWarned) {
  auto plugin = make_branch_quest_plugin(0x821, 0x822, 0);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::dialogue_branch_quest_mismatch);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:BranchQuest.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000820:BranchQuest.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "BNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
  EXPECT_NE(findings[0].message.find("000821:BranchQuest.esp"), std::string::npos);
  EXPECT_NE(findings[0].message.find("000822:BranchQuest.esp"), std::string::npos);
}

TEST(CheckDialogue, BranchQuestThatAgreesIsValid) {
  auto plugin = make_branch_quest_plugin(0x821, 0x821, 0);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::dialogue_branch_quest_mismatch).empty());
}

TEST(CheckDialogue, BranchCategoryOutsideZeroAndOneIsWarned) {
  auto plugin = make_branch_quest_plugin(0x821, 0x821, 2);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::dialogue_branch_category_invalid);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000820:BranchQuest.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "TNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, BranchCategoryZeroOrOneIsValid) {
  auto plugin = make_branch_quest_plugin(0x821, 0x821, 1);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::dialogue_branch_category_invalid).empty());
}

TEST(CheckDialogue, SceneWithoutParentQuestIsReported) {
  SceneSpec spec;
  spec.phases = 1;
  spec.has_parent = false;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_parent_quest_missing);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000900:Scene.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "PNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckDialogue, SceneParentQuestThatIsNotAQuestIsReported) {
  SceneSpec spec;
  spec.parent = 0x800;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_parent_quest_missing);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000800:Scene.esp"));
}

TEST(CheckDialogue, SceneParentQuestThatIsAQuestIsValid) {
  SceneSpec spec;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::scene_parent_quest_missing).empty());
}

TEST(CheckDialogue, SceneActionTopicThatIsNotADialIsReported) {
  SceneSpec spec;
  SceneActionSpec action;
  action.type = 3;
  action.payload = {{"PTOP", 0x810}};
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_action_topic_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000900:Scene.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000810:Scene.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "PTOP");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckDialogue, SceneActionTopicThatIsADialIsValid) {
  SceneSpec spec;
  SceneActionSpec action;
  action.type = 3;
  action.payload = {{"PTOP", 0x800}};
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::scene_action_topic_not_dial).empty());
}

TEST(CheckDialogue, SceneActionPhaseOutsideTheSceneIsWarned) {
  SceneSpec spec;
  spec.phases = 1;
  SceneActionSpec action;
  action.start = 2u;
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_action_phase_out_of_range);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].subrecord.to_string(), "SNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, SceneActionPhaseInsideTheSceneIsValid) {
  SceneSpec spec;
  spec.phases = 1;
  SceneActionSpec action;
  action.start = 0u;
  action.end = 0u;
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::scene_action_phase_out_of_range).empty());
}

TEST(CheckDialogue, SceneActionAliasThatMatchesNoActorIsWarned) {
  SceneSpec spec;
  spec.actors = {5};
  SceneActionSpec action;
  action.alias = 6;
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_action_alias_unknown);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].subrecord.to_string(), "ALID");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, SceneActionAliasThatMatchesAnActorIsValid) {
  SceneSpec spec;
  spec.actors = {5};
  SceneActionSpec action;
  action.alias = 5;
  spec.actions.push_back(action);
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::scene_action_alias_unknown).empty());
}

TEST(CheckDialogue, SceneLastActionIndexBelowActionIndexIsWarned) {
  SceneSpec spec;
  SceneActionSpec action;
  action.index = 3u;
  spec.actions.push_back(action);
  spec.last_index = 2;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::scene_action_index_order);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].subrecord.to_string(), "INAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, SceneMissingLastActionIndexIsWarned) {
  SceneSpec spec;
  SceneActionSpec action;
  spec.actions.push_back(action);
  spec.has_last_index = false;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_EQ(of_code(fo4::check_load_order(load_order),
                    fo4::CheckCode::scene_action_index_order).size(), 1u);
}

TEST(CheckDialogue, SceneLastActionIndexThatCoversTheActionsIsValid) {
  SceneSpec spec;
  SceneActionSpec action;
  action.index = 3u;
  spec.actions.push_back(action);
  spec.last_index = 3;
  auto plugin = make_scene_plugin(spec);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::scene_action_index_order).empty());
}
