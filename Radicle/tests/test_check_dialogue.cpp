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
  write_record(out, "QUST", 0x810);
  write_topic_children(out, 0x810, {{0x811, 0}});
  return fo4::read_plugin(out.bytes, "Orphan.esp");
}

fo4::Plugin make_branch_plugin(std::uint32_t starting_topic) {
  fo4::test::ByteWriter out;
  write_header(out);
  write_record(out, "DIAL", 0x800);
  write_record(out, "QUST", 0x821);
  const auto dlbr_top = fo4::test::begin_group(out, "DLBR", 0);
  write_record(out, "DLBR", 0x820, "SNAM", starting_topic);
  fo4::test::end_group(out, dlbr_top);
  return fo4::read_plugin(out.bytes, "Branch.esp");
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

}  // namespace

TEST(CheckDialogue, CodeNamesAreStable) {
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::previous_info_other_topic),
            "previous_info_other_topic");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::branch_starting_topic_not_dial),
            "branch_starting_topic_not_dial");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::topic_children_not_dial),
            "topic_children_not_dial");
}

TEST(CheckDialogue, PreviousInfoInOtherTopicIsReported) {
  const auto plugin = make_topic_plugin(0x801);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::previous_info_other_topic);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000804:Topics.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000801:Topics.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "PNAM");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckDialogue, PreviousInfoInSameTopicIsValid) {
  const auto plugin = make_topic_plugin(0x805);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::previous_info_other_topic).empty());
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order),
                      fo4::CheckCode::topic_children_not_dial).empty());
}

TEST(CheckDialogue, TopicChildrenLabelThatIsNotADialIsReported) {
  const auto plugin = make_orphan_topic_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000811:Orphan.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000810:Orphan.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
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
