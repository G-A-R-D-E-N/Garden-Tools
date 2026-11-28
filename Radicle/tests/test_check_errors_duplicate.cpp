#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
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

void write_misc(fo4::test::ByteWriter& out, std::uint32_t form_id, std::uint32_t flags = 0) {
  const auto record = fo4::test::begin_record(out, "MISC", flags, form_id);
  fo4::test::end_record(out, record);
}

fo4::Plugin plugin_with_records(std::string_view name, std::uint32_t first_id,
                                std::uint32_t second_id, std::uint32_t second_flags = 0) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  write_misc(out, first_id);
  write_misc(out, second_id, second_flags);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, std::string{name});
}

std::vector<fo4::CheckFinding> duplicate_findings(
    const std::vector<const fo4::Plugin*>& load_order) {
  const auto findings = fo4::check_load_order(load_order);
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [](const fo4::CheckFinding& finding) {
                 return finding.code == fo4::CheckCode::duplicate_form_id;
               });
  return out;
}

}  // namespace

TEST(CheckErrorsDuplicate, SameLocalIdTwiceInOnePluginIsOneError) {
  const auto plugin = plugin_with_records("Dup.esp", 0x000800, 0x000800);
  const auto findings = duplicate_findings({&plugin});
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Dup.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
  EXPECT_EQ(fo4::check_code_name(findings[0].code), "duplicate_form_id");
  EXPECT_NE(findings[0].message.find("first record at byte"), std::string::npos);
}

TEST(CheckErrorsDuplicate, SameIdInTwoPluginsIsNotADuplicate) {
  const auto first = plugin_with_records("DupA.esp", 0x000800, 0x000801);
  const auto second = plugin_with_records("DupB.esp", 0x000800, 0x000801);
  EXPECT_TRUE(duplicate_findings({&first, &second}).empty());
}

TEST(CheckErrorsDuplicate, UniqueIdsInOnePluginAreClean) {
  const auto plugin = plugin_with_records("Dup.esp", 0x000800, 0x000801);
  EXPECT_TRUE(duplicate_findings({&plugin}).empty());
}

TEST(CheckErrorsDuplicate, ThreeRecordsWithOneIdAreTwoErrors) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  write_misc(out, 0x000800);
  write_misc(out, 0x000800);
  write_misc(out, 0x000800);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Dup.esp");
  EXPECT_EQ(duplicate_findings({&plugin}).size(), 2u);
}

TEST(CheckErrorsDuplicate, NestedGroupDuplicateIsFound) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto top = fo4::test::begin_group(out, "MISC", 0);
  write_misc(out, 0x000800);
  const auto nested = fo4::test::begin_group(out, "MISC", 1);
  write_misc(out, 0x000800);
  fo4::test::end_group(out, nested);
  fo4::test::end_group(out, top);
  const auto plugin = fo4::read_plugin(out.bytes, "Dup.esp");
  const auto findings = duplicate_findings({&plugin});
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Dup.esp"));
}

TEST(CheckErrorsDuplicate, DeletedFlagDoesNotHideDuplicate) {
  const auto plugin = plugin_with_records("Dup.esp", 0x000800, 0x000800, 0x20);
  EXPECT_EQ(duplicate_findings({&plugin}).size(), 1u);
}
