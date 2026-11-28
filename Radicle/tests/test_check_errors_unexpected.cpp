#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
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

fo4::Plugin lvli_plugin(std::initializer_list<std::string_view> subrecords) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  const auto record = fo4::test::begin_record(out, "LVLI", 0, 0x000800);
  for (const auto signature : subrecords) {
    fo4::test::ByteWriter data;
    data.u32(0);
    fo4::test::write_subrecord(out, signature, data.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Unex.esp");
}

std::vector<fo4::CheckFinding> unexpected_findings(const fo4::Plugin& plugin, bool enabled) {
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  fo4::CheckOptions options;
  options.unexpected_subrecords = enabled;
  const auto findings = fo4::check_load_order(load_order, options);
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [](const fo4::CheckFinding& finding) {
                 return finding.code == fo4::CheckCode::unexpected_subrecord;
               });
  return out;
}

}  // namespace

TEST(CheckErrorsUnexpected, OptInFlagIsOffByDefault) {
  const auto plugin = lvli_plugin({"EDID", "ZZZZ"});
  EXPECT_TRUE(unexpected_findings(plugin, false).empty());
}

TEST(CheckErrorsUnexpected, SubrecordOutsideEveryAuthoringListIsAWarning) {
  const auto plugin = lvli_plugin({"EDID", "ZZZZ"});
  const auto findings = unexpected_findings(plugin, true);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Unex.esp"));
  EXPECT_EQ(findings[0].subrecord, fo4::Signature::from_string("ZZZZ"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
  EXPECT_EQ(findings[0].message, "ZZZZ is not in the authoring list for LVLI");
}

TEST(CheckErrorsUnexpected, ListedSubrecordsAreNotReported) {
  const auto plugin = lvli_plugin({"EDID", "OBND", "LVLD", "LVLF", "LVLO"});
  EXPECT_TRUE(unexpected_findings(plugin, true).empty());
}
