#include "fo4/graph/check_errors.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t k_localized_flag = 0x80;

void write_header(fo4::test::ByteWriter& out, std::uint32_t flags) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> id_payload(std::uint32_t id) {
  fo4::test::ByteWriter bytes;
  bytes.u32(id);
  return bytes.bytes;
}

void write_misc_full(fo4::test::ByteWriter& out, std::uint32_t form_id,
                     const std::vector<std::byte>& full) {
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
  fo4::test::write_subrecord(out, "FULL", full);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

fo4::Plugin string_id_plugin(std::uint32_t flags, std::string_view name) {
  fo4::test::ByteWriter out;
  write_header(out, flags);
  write_misc_full(out, 0x000800, id_payload(5));
  write_misc_full(out, 0x000801, id_payload(0));
  write_misc_full(out, 0x000802, id_payload(1));
  return fo4::read_plugin(out.bytes, std::string{name});
}

fo4::Plugin string_id_plugin(std::uint32_t flags) { return string_id_plugin(flags, "Loc.esp"); }

fo4::localization::LocalizationTables sample_tables() {
  fo4::localization::LocalizationTables tables;
  fo4::localization::insert_string(tables.strings, 1, "Sword");
  return tables;
}

std::vector<fo4::CheckFinding> string_id_findings(
    const fo4::Plugin& plugin, const fo4::localization::LocalizationTables* tables) {
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  fo4::CheckOptions options;
  options.localization_tables = tables;
  const auto findings = fo4::check_load_order(load_order, options);
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [](const fo4::CheckFinding& finding) {
                 return finding.code == fo4::CheckCode::unresolved_string_id;
               });
  return out;
}

}  // namespace

TEST(CheckErrorsLocalized, NullTablesReportNothing) {
  const auto plugin = string_id_plugin(k_localized_flag);
  EXPECT_TRUE(string_id_findings(plugin, nullptr).empty());
}

TEST(CheckErrorsLocalized, UnresolvedStringIdIsAnErrorAndZeroIsEmpty) {
  const auto plugin = string_id_plugin(k_localized_flag);
  const auto tables = sample_tables();
  const auto findings = string_id_findings(plugin, &tables);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Loc.esp"));
  EXPECT_EQ(findings[0].subrecord, fo4::Signature::from_string("FULL"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
  EXPECT_EQ(findings[0].message, "FULL string ID 5 is not in the .STRINGS table");
}

TEST(CheckErrorsLocalized, NonLocalizedPluginIgnoresStringIds) {
  const auto plugin = string_id_plugin(0);
  const auto tables = sample_tables();
  EXPECT_TRUE(string_id_findings(plugin, &tables).empty());
}

// Detects: one table set must not be applied to several localized plugins.
TEST(CheckErrorsLocalized, OneTableSetIsNotAppliedToSeveralLocalizedPlugins) {
  const auto first = string_id_plugin(k_localized_flag, "LocA.esp");
  const auto second = string_id_plugin(k_localized_flag, "LocB.esp");
  const auto tables = sample_tables();
  const std::vector<const fo4::Plugin*> load_order{&first, &second};
  fo4::CheckOptions options;
  options.localization_tables = &tables;
  const auto findings = fo4::check_load_order(load_order, options);
  EXPECT_TRUE(std::none_of(findings.begin(), findings.end(), [](const fo4::CheckFinding& finding) {
    return finding.code == fo4::CheckCode::unresolved_string_id;
  }));
}

// Detects: narrowing the scope to one plugin must still run the string-ID check.
TEST(CheckErrorsLocalized, SingleSelectedLocalizedPluginStillChecksStringIds) {
  const auto plugin = string_id_plugin(k_localized_flag);
  const auto tables = sample_tables();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  fo4::CheckOptions options;
  options.plugins = {plugin.key};
  options.localization_tables = &tables;
  const auto findings = fo4::check_load_order(load_order, options);
  EXPECT_EQ(std::count_if(findings.begin(), findings.end(), [](const fo4::CheckFinding& finding) {
    return finding.code == fo4::CheckCode::unresolved_string_id;
  }), 1);
}
