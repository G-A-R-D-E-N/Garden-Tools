#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t k_deleted_flag = 0x20;

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
                        std::uint32_t form_id, std::uint32_t flags = 0) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
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

void write_lvli(fo4::test::ByteWriter& out, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> lists) {
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  for (const auto& [form_id, entry] : lists) {
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
}

fo4::Plugin make_check_plugin() {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  write_empty_record(out, "KYWD", 0x000806, k_deleted_flag);
  write_misc_with_keywords(out, 0x000801, {0x000800, 0x000802, 0x000803, 0x000806, 0x05000900});
  write_empty_record(out, "STAT", 0x000802);
  write_lvli(out, {{0x000810, 0x000811}, {0x000811, 0x000810}, {0x000812, 0x000800}});
  return fo4::read_plugin(out.bytes, "Check.esp");
}

fo4::Plugin make_dependent_plugin() {
  fo4::test::ByteWriter out;
  write_header(out, "Check.esp");
  write_misc_with_keywords(out, 0x01000820, {0x00000999});
  return fo4::read_plugin(out.bytes, "Other.esp");
}

fo4::Plugin make_expl_plugin(std::uint32_t selector) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "EXPL", 0);
  const auto record = fo4::test::begin_record(out, "EXPL", 0, 0x000800);
  fo4::test::ByteWriter data;
  data.u32(selector);
  data.u32(0x000800);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Variant.esp");
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

}  // namespace

TEST(CheckErrors, ReportsEachLinkProblemOnce) {
  const auto plugin = make_check_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);

  const auto wrong_type = of_code(findings, fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(wrong_type.size(), 1u);
  EXPECT_EQ(wrong_type[0].target, fo4::FormKey::parse("000802:Check.esp"));
  EXPECT_EQ(wrong_type[0].record, fo4::FormKey::parse("000801:Check.esp"));
  EXPECT_EQ(wrong_type[0].subrecord.to_string(), "KWDA");
  EXPECT_EQ(wrong_type[0].offset, 4u);

  const auto unresolved = of_code(findings, fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 2u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000803:Check.esp"));
  EXPECT_EQ(unresolved[1].target, fo4::FormKey::parse("000900:Check.esp"));
  EXPECT_EQ(unresolved[1].offset, 16u);

  const auto deleted = of_code(findings, fo4::CheckCode::deleted_target);
  ASSERT_EQ(deleted.size(), 1u);
  EXPECT_EQ(deleted[0].target, fo4::FormKey::parse("000806:Check.esp"));
  EXPECT_EQ(deleted[0].severity, fo4::CheckSeverity::warning);

  EXPECT_TRUE(of_code(findings, fo4::CheckCode::invalid_master_index).empty());
}

TEST(CheckErrors, ReportsLeveledListCycleOnce) {
  const auto plugin = make_check_plugin();
  const std::vector<const fo4::Plugin*> load_order{&plugin};

  const auto cycles = fo4::find_leveled_list_cycles(load_order);
  ASSERT_EQ(cycles.size(), 1u);
  EXPECT_EQ(cycles[0].front(), cycles[0].back());
  EXPECT_EQ(cycles[0].size(), 3u);

  const auto findings = of_code(fo4::check_load_order(load_order),
                                fo4::CheckCode::circular_leveled_list);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].plugin.filename(), "Check.esp");
}

TEST(CheckErrors, LaterOverrideBreaksCycle) {
  const auto base = make_check_plugin();
  fo4::test::ByteWriter out;
  write_header(out, "Check.esp");
  write_lvli(out, {{0x00000811, 0x00000800}});
  const auto patch = fo4::read_plugin(out.bytes, "Patch.esp");
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  EXPECT_TRUE(fo4::find_leveled_list_cycles(load_order).empty());
}

TEST(CheckErrors, FiltersByPluginAndHonoursLimit) {
  const auto base = make_check_plugin();
  const auto other = make_dependent_plugin();
  const std::vector<const fo4::Plugin*> load_order{&base, &other};

  fo4::CheckOptions only_other;
  only_other.plugins.push_back(fo4::PluginKey::from_filename("Other.esp"));
  const auto findings = fo4::check_load_order(load_order, only_other);
  ASSERT_FALSE(findings.empty());
  for (const auto& finding : findings) EXPECT_EQ(finding.plugin.filename(), "Other.esp");
  const auto unresolved = of_code(findings, fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000999:Check.esp"));

  fo4::CheckOptions limited;
  limited.limit = 2;
  EXPECT_EQ(fo4::check_load_order(load_order, limited).size(), 2u);
}

TEST(CheckErrors, CodeNamesAreStable) {
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::wrong_target_type), "wrong_target_type");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::circular_leveled_list), "circular_leveled_list");
  EXPECT_EQ(fo4::check_severity_name(fo4::CheckSeverity::warning), "warning");
}

TEST(CheckErrors, VanillaMasterHasNoErrorFindings) {
  const char* data = std::getenv("FO4_DATA_DIR");
  if (!data) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path dir{data};
  if (!std::filesystem::exists(dir / "Fallout4.esm")) GTEST_SKIP() << "Fallout4.esm not found";

  const auto plugin = fo4::read_plugin_file(dir / "Fallout4.esm");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto wrong_type = of_code(findings, fo4::CheckCode::wrong_target_type);
  for (std::size_t i = 0; i < std::min<std::size_t>(wrong_type.size(), 10); ++i) {
    ADD_FAILURE() << wrong_type[i].record.to_string() << " " << wrong_type[i].message;
  }
  EXPECT_TRUE(wrong_type.empty()) << wrong_type.size() << " wrong_target_type findings";
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::invalid_master_index).empty());
  for (const auto& finding : findings) EXPECT_NE(finding.severity, fo4::CheckSeverity::error) << finding.message;
}

TEST(CheckErrors, PastMasterTableLinkToOwnRecordIsValid) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  write_misc_with_keywords(out, 0x000801, {0x05000800});
  const auto plugin = fo4::read_plugin(out.bytes, "Owner.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(fo4::check_load_order(load_order).empty());
}

TEST(CheckErrors, HardcodedEngineFormsAreNotUnresolved) {
  fo4::test::ByteWriter game;
  write_header(game, {});
  write_empty_record(game, "KYWD", 0x000800);
  const auto master = fo4::read_plugin(game.bytes, "Fallout4.esm");
  fo4::test::ByteWriter out;
  write_header(out, "Fallout4.esm");
  write_misc_with_keywords(out, 0x01000801, {0x00000014, 0x00000900});
  const auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&master, &plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000900:Fallout4.esm"));
}

TEST(CheckErrors, UnresolvedLinkInUnprovenSlotIsOnlyAWarning) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_misc_with_keywords(out, 0x000801, {0x000901});
  const auto group = fo4::test::begin_group(out, "RFGP", 0);
  const auto record = fo4::test::begin_record(out, "RFGP", 0, 0x000802);
  fo4::test::ByteWriter wnam;
  wnam.u32(0x000900);
  fo4::test::write_subrecord(out, "RNAM", wnam.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 2u);
  for (const auto& finding : unresolved) {
    EXPECT_EQ(finding.severity, finding.subrecord.to_string() == "KWDA" ? fo4::CheckSeverity::error
                                                                          : fo4::CheckSeverity::warning)
        << finding.message;
  }
}

TEST(CheckErrors, NoneFormIdIsNotALink) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_misc_with_keywords(out, 0x000801, {0xFFFFFFFFu});
  const auto group = fo4::test::begin_group(out, "EQUP", 0);
  const auto record = fo4::test::begin_record(out, "EQUP", 0, 0x000802);
  fo4::test::ByteWriter anam;
  anam.u32(0xFFFFFFFFu);
  fo4::test::write_subrecord(out, "ANAM", anam.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  for (const auto& finding : fo4::check_load_order(load_order)) {
    EXPECT_NE(finding.code, fo4::CheckCode::unresolved_link) << finding.message;
    EXPECT_NE(finding.code, fo4::CheckCode::invalid_master_index) << finding.message;
  }
}

TEST(CheckErrors, VariantFormLinkSlotIsProvenOnlyForSelectedVariant) {
  auto field = *fo4::schema::formlink_field_descriptor("EXPL\\Light");
  field.offset = 4;
  field.address.when.count = 1;
  field.address.when.storage = fo4::schema::DiscriminatorStorage::u32;
  field.address.when.equals[0] = 1;
  const std::array<fo4::schema::FormLinkFieldDescriptor, 1> fields{field};
  const auto subrecord = fo4::Signature::from_string("DATA");
  const auto selected = make_expl_plugin(1);
  const auto other = make_expl_plugin(2);
  const auto selected_records = fo4::collect_records(selected);
  const auto other_records = fo4::collect_records(other);
  ASSERT_EQ(selected_records.size(), 1u);
  ASSERT_EQ(other_records.size(), 1u);

  const auto slot = fo4::schema::formlink_field_at(fields, *selected_records[0], subrecord, 4);
  ASSERT_TRUE(slot.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*slot, fo4::Signature::from_string("LIGH")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*slot, fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::formlink_field_at(fields, *other_records[0], subrecord, 4).has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_at(fields, *selected_records[0], subrecord, 0).has_value());
}

TEST(CheckErrors, LowIdsInACustomFirstMasterAreNotTreatedAsHardcoded) {
  fo4::test::ByteWriter game;
  write_header(game, {});
  write_empty_record(game, "KYWD", 0x000800);
  const auto master = fo4::read_plugin(game.bytes, "CustomMaster.esm");
  fo4::test::ByteWriter out;
  write_header(out, "CustomMaster.esm");
  write_misc_with_keywords(out, 0x01000801, {0x00000014});
  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const std::vector<const fo4::Plugin*> load_order{&master, &plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000014:CustomMaster.esm"));
}
