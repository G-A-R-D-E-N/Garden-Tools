#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
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

void write_empty_record(fo4::test::ByteWriter& out, std::string_view signature,
                        std::uint32_t form_id) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_lvlo(fo4::test::ByteWriter& out, std::uint32_t target) {
  fo4::test::ByteWriter lvlo;
  lvlo.u16(1);
  lvlo.u16(0);
  lvlo.u32(target);
  lvlo.u16(1);
  lvlo.u8(0);
  lvlo.u8(0);
  fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
}

void write_llct(fo4::test::ByteWriter& out, std::uint8_t count) {
  fo4::test::ByteWriter llct;
  llct.u8(count);
  fo4::test::write_subrecord(out, "LLCT", llct.bytes);
}

fo4::Plugin leveled_list_plugin(const char* name, std::uint32_t form_id, bool with_count,
                                std::uint8_t declared,
                                std::initializer_list<std::uint32_t> entries) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  const auto record = fo4::test::begin_record(out, "LVLI", 0, form_id);
  if (with_count) write_llct(out, declared);
  for (const auto entry : entries) write_lvlo(out, entry);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}

fo4::Plugin form_list_plugin(const char* name, std::initializer_list<std::uint32_t> members) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  const auto group = fo4::test::begin_group(out, "FLST", 0);
  const auto record = fo4::test::begin_record(out, "FLST", 0, 0x000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view("DupList\0", 8));
  for (const auto member : members) {
    fo4::test::ByteWriter lnam;
    lnam.u32(member);
    fo4::test::write_subrecord(out, "LNAM", lnam.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}

fo4::Plugin outfit_plugin(const char* name, std::uint32_t entry) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "WEAP", 0x000800);
  const auto group = fo4::test::begin_group(out, "OTFT", 0);
  const auto record = fo4::test::begin_record(out, "OTFT", 0, 0x000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Outfit\0", 7));
  fo4::test::ByteWriter inam;
  inam.u32(entry);
  fo4::test::write_subrecord(out, "INAM", inam.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

std::vector<fo4::CheckFinding> of_detail(const std::vector<fo4::CheckFinding>& findings,
                                         fo4::CheckCode code, std::string_view detail) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) {
                 return finding.code == code && finding.detail == detail;
               });
  return out;
}

}  // namespace

TEST(CheckErrorsRecordLists, StaleLeveledListCountIsAnError) {
  const auto plugin = leveled_list_plugin("Stale.esp", 0x000801, true, 2, {0x000800});
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_detail(fo4::check_load_order(load_order),
                                  fo4::CheckCode::schema_preflight, "leveled_list_count_mismatch");
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:Stale.esp"));
  EXPECT_EQ(findings[0].message, "LVLI LLCT declares 2 but holds 1 LVLO entries");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrorsRecordLists, MissingLeveledListCountIsAnError) {
  const auto plugin = leveled_list_plugin("NoCount.esp", 0x000801, false, 0, {0x000800});
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_detail(fo4::check_load_order(load_order),
                                  fo4::CheckCode::schema_preflight, "leveled_list_count_mismatch");
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:NoCount.esp"));
  EXPECT_EQ(findings[0].message, "LVLI holds 1 LVLO entries but has no LLCT subrecord");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrorsRecordLists, MatchingLeveledListCountIsValid) {
  const auto plugin = leveled_list_plugin("Clean.esp", 0x000801, true, 1, {0x000800});
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_detail(fo4::check_load_order(load_order), fo4::CheckCode::schema_preflight,
                        "leveled_list_count_mismatch")
                  .empty());
  EXPECT_TRUE(of_detail(fo4::check_load_order(load_order), fo4::CheckCode::schema_preflight,
                        "leveled_list_null_entry")
                  .empty());
}

TEST(CheckErrorsRecordLists, NullLeveledListEntryIsAnError) {
  const auto plugin = leveled_list_plugin("NullEntry.esp", 0x000801, true, 1, {0});
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_detail(fo4::check_load_order(load_order),
                                  fo4::CheckCode::schema_preflight, "leveled_list_null_entry");
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:NullEntry.esp"));
  EXPECT_EQ(findings[0].message, "LVLI LVLO entry 0 names no item");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrorsRecordLists, DuplicateFormListEntryIsAWarning) {
  const auto plugin = form_list_plugin("Dup.esp", {0x000800, 0x000800});
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_detail(fo4::check_load_order(load_order),
                                  fo4::CheckCode::schema_preflight, "duplicate_formlink_list_entry");
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:Dup.esp"));
  EXPECT_EQ(findings[0].message, "FLST repeats LNAM entry 00000800, which the game counts twice");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);

  const auto clean = form_list_plugin("Clean.esp", {0x000800});
  const std::vector<const fo4::Plugin*> clean_order{&clean};
  EXPECT_TRUE(of_detail(fo4::check_load_order(clean_order), fo4::CheckCode::schema_preflight,
                        "duplicate_formlink_list_entry")
                  .empty());
}

TEST(CheckErrorsRecordLists, OutfitItemOfTheWrongTypeIsAlreadyReported) {
  const auto plugin = outfit_plugin("WrongType.esp", 0x000800);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:WrongType.esp"));
  EXPECT_EQ(findings[0].target, fo4::FormKey::parse("000800:WrongType.esp"));
  EXPECT_EQ(findings[0].subrecord.to_string(), "INAM");
  EXPECT_EQ(findings[0].message,
            "INAM target 000800:WrongType.esp is WEAP, expected one of ARMO, LVLI");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}
