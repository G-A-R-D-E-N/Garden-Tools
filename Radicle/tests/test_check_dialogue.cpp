#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
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

}  // namespace

TEST(CheckDialogue, TopicChildrenLabelThatIsNotADialIsReported) {
  const auto plugin = make_orphan_topic_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000811:Orphan.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000810:Orphan.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckDialogue, TopicChildrenLabelOfADialIsValid) {
  const auto plugin = make_topic_plugin(0x805);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::topic_children_not_dial).empty());
}

TEST(CheckDialogue, BranchStartingTopicThatIsNotADialIsReported) {
  const auto plugin = make_branch_plugin(0x821);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000820:Branch.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000821:Branch.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "SNAM");
}

TEST(CheckDialogue, BranchStartingTopicThatIsADialIsValid) {
  const auto plugin = make_branch_plugin(0x800);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::branch_starting_topic_not_dial).empty());
}

TEST(CheckDialogue, BranchStartingTopicThatIsDeletedIsWarned) {
  const auto plugin = make_branch_plugin(0x800, 0x20);
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
  const auto plugin = make_bad_master_index_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto branch = of_code(findings, fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(branch.size(), 1u);
  const auto children = of_code(findings, fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(children.size(), 1u);
  const auto bad_index = of_code(findings, fo4::CheckCode::invalid_master_index);
  ASSERT_EQ(bad_index.size(), 1u);
}
