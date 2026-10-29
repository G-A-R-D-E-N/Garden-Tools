#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out, std::string_view master) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (!master.empty()) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

void write_empty_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_misc_with_keywords(fo4::test::ByteWriter& out, std::uint32_t form_id,
                              std::initializer_list<std::uint32_t> keywords) {
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
  fo4::test::ByteWriter ksiz;
  ksiz.u32(static_cast<std::uint32_t>(keywords.size()));
  fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
  fo4::test::ByteWriter kwda;
  for (const auto keyword : keywords) kwda.u32(keyword);
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_dlbr_with_snam(fo4::test::ByteWriter& out, std::uint32_t form_id, std::uint32_t snam) {
  const auto group = fo4::test::begin_group(out, "DLBR", 0);
  const auto record = fo4::test::begin_record(out, "DLBR", 0, form_id);
  fo4::test::ByteWriter value;
  value.u32(snam);
  fo4::test::write_subrecord(out, "SNAM", value.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::size_t child_group(fo4::test::ByteWriter& out, std::uint32_t parent, std::int32_t type) {
  const auto at = fo4::test::begin_group(out, "QUST", type);
  out.patch_u32(at + 8, parent);
  return at;
}

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

}  // namespace

TEST(CheckErrorsGroup1, CreatedFormIdIsAnInvalidMasterIndex) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  write_misc_with_keywords(out, 0x000801, {0xFF000800});
  const auto plugin = fo4::read_plugin(out.bytes, "Save.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::invalid_master_index);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
  EXPECT_EQ(findings[0].subrecord.to_string(), "KWDA");
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrorsGroup1, DlbrSnamPointingAtLoadedNonDialReportsOnce) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000830);
  write_dlbr_with_snam(out, 0x000820, 0x000830);
  const auto plugin = fo4::read_plugin(out.bytes, "Branch.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto starting = of_code(findings, fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(starting.size(), 1u);
  EXPECT_EQ(starting[0].target, fo4::FormKey::parse("000830:Branch.esp"));
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::wrong_target_type).empty());
}

TEST(CheckErrorsGroup1, NullDlbrSnamIsReported) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_dlbr_with_snam(out, 0x000820, 0x00000000);
  const auto plugin = fo4::read_plugin(out.bytes, "Branch.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
  EXPECT_EQ(findings[0].subrecord.to_string(), "SNAM");
}

TEST(CheckErrorsGroup1, TopicChildrenLabelPastMasterTableIsChecked) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = child_group(out, 0x05000850, 7);
  const auto record = fo4::test::begin_record(out, "INFO", 0, 0x000900);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Topics.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::topic_children_not_dial);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000850:Topics.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckErrorsGroup1, HardcodedPlayerRefIsTypeCheckedAgainstTheGameMaster) {
  fo4::test::ByteWriter game;
  write_header(game, {});
  write_empty_record(game, "PLYR", 0x000014);
  const auto master = fo4::read_plugin(game.bytes, "Fallout4.esm");
  fo4::test::ByteWriter middle;
  write_header(middle, {});
  const auto other = fo4::read_plugin(middle.bytes, "Other.esm");
  fo4::test::ByteWriter out;
  write_header(out, "Other.esm");
  write_misc_with_keywords(out, 0x01000801, {0x00000014});
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x01000802);
  fo4::test::ByteWriter xlkr;
  xlkr.u32(0x00000014);
  fo4::test::write_subrecord(out, "XLKR", xlkr.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&master, &other, &plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto wrong = of_code(findings, fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(wrong.size(), 1u);
  EXPECT_EQ(wrong[0].target, fo4::FormKey::parse("000014:Fallout4.esm"));
  EXPECT_EQ(wrong[0].record, fo4::FormKey::parse("000801:Mod.esp"));
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrorsGroup1, UnsupportedVmadPropertyKeepsEarlierLinksAndReports) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "DemoScript");
  vmad.u8(0);
  vmad.u16(2);
  wstring(vmad, "Target");
  vmad.u8(1);
  vmad.u8(1);
  vmad.u16(0);
  vmad.u16(0xFFFF);
  vmad.u32(0x00000800);
  wstring(vmad, "Anything");
  vmad.u8(6);
  vmad.u8(1);
  vmad.u32(1);
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x000801);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Scripted.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto unsupported = of_code(findings, fo4::CheckCode::unsupported_vmad_property);
  ASSERT_EQ(unsupported.size(), 1u);
  EXPECT_EQ(unsupported[0].severity, fo4::CheckSeverity::warning);
  EXPECT_EQ(unsupported[0].record, fo4::FormKey::parse("000801:Scripted.esp"));
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrorsGroup1, LimitTruncationFlagAndCycleSurvival) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  for (const auto& [form_id, entry] : {std::pair{0x810u, 0x811u}, {0x811u, 0x810u}}) {
    const auto record = fo4::test::begin_record(out, "LVLI", 0, form_id);
    fo4::test::ByteWriter lvlo;
    lvlo.u16(1);
    lvlo.u16(0);
    lvlo.u32(entry);
    lvlo.u16(1);
    lvlo.u8(0);
    lvlo.u8(0);
    fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  write_misc_with_keywords(out, 0x000801, {0x000803});
  const auto plugin = fo4::read_plugin(out.bytes, "Capped.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  fo4::CheckOptions limited;
  limited.limit = 1;
  fo4::CheckRunSummary summary;
  const auto findings = fo4::check_load_order(load_order, limited, &summary);
  ASSERT_EQ(findings.size(), 2u);
  EXPECT_EQ(findings[0].code, fo4::CheckCode::circular_leveled_list);
  EXPECT_EQ(findings[1].code, fo4::CheckCode::results_truncated);
  EXPECT_TRUE(summary.truncated);
}

TEST(CheckErrorsGroup1, NewCodeNamesAreStable) {
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::unsupported_vmad_property), "unsupported_vmad_property");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::results_truncated), "results_truncated");
  EXPECT_EQ(fo4::check_severity_name(fo4::CheckSeverity::warning), "warning");
}
