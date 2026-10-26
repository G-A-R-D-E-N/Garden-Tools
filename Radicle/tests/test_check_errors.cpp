#include "fo4/c_api_env.h"
#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t k_deleted_flag = 0x20;
constexpr std::uint32_t k_partial_form_flag = 0x4000;
constexpr std::uint32_t k_small_file_flag = 0x200;

void write_header(fo4::test::ByteWriter& out, std::string_view master, std::uint32_t flags = 0) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
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

void write_record_with_subrecords(fo4::test::ByteWriter& out, std::string_view signature,
                                  std::uint32_t flags, std::uint32_t form_id,
                                  std::initializer_list<std::string_view> subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  for (const auto subrecord : subrecords) {
    fo4::test::ByteWriter data;
    data.u32(0);
    fo4::test::write_subrecord(out, subrecord, data.bytes);
  }
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

std::vector<std::byte> duplicate_scalar_bytes() {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x000800);
  for (const std::uint32_t health : {50u, 75u}) {
    fo4::test::ByteWriter xhlt;
    xhlt.u32(health);
    fo4::test::write_subrecord(out, "XHLT", xhlt.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Record* find_record(const fo4::Plugin& plugin, std::string_view signature) {
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature == fo4::Signature::from_string(signature)) return record;
  }
  return nullptr;
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
  auto plugin = make_check_plugin();
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
  auto plugin = make_check_plugin();
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
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::branch_starting_topic_not_dial),
            "branch_starting_topic_not_dial");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::topic_children_not_dial),
            "topic_children_not_dial");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::deleted_record_content), "deleted_record_content");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::partial_form_content), "partial_form_content");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::light_object_id_overflow), "light_object_id_overflow");
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::internal_form_id_mismatch), "internal_form_id_mismatch");
}

TEST(CheckErrors, VanillaMasterHasNoErrorFindings) {
  const char* data = std::getenv("FO4_DATA_DIR");
  if (!data) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const std::filesystem::path dir{data};
  record_game_build(detect_game_build(dir));
  if (!std::filesystem::exists(dir / "Fallout4.esm")) GTEST_SKIP() << "Fallout4.esm not found";

  auto plugin = fo4::read_plugin_file(dir / "Fallout4.esm");
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
  auto plugin = fo4::read_plugin(out.bytes, "Owner.esp");
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
  auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
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
  fo4::test::write_subrecord(out, "WNAM", wnam.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 2u);
  for (const auto& finding : unresolved) {
    EXPECT_EQ(finding.severity, finding.subrecord.to_string() == "KWDA" ? fo4::CheckSeverity::error
                                                                          : fo4::CheckSeverity::warning)
        << finding.message;
  }
}

TEST(CheckErrors, MissingBranchStartingTopicIsReportedOnce) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "DLBR", 0);
  const auto record = fo4::test::begin_record(out, "DLBR", 0, 0x000820);
  fo4::test::ByteWriter snam;
  snam.u32(0x000830);
  fo4::test::write_subrecord(out, "SNAM", snam.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  const auto snam_findings = std::count_if(findings.begin(), findings.end(),
                                           [](const fo4::CheckFinding& finding) {
                                             return finding.subrecord.to_string() == "SNAM";
                                           });
  EXPECT_EQ(snam_findings, 1);
  const auto starting = of_code(findings, fo4::CheckCode::branch_starting_topic_not_dial);
  ASSERT_EQ(starting.size(), 1u);
  EXPECT_EQ(starting[0].target, fo4::FormKey::parse("000830:Mod.esp"));
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::unresolved_link).empty());
}

namespace {
fo4::Plugin rfgp_with_reference(const char* name, std::uint32_t rnam) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "RFGP", 0);
  const auto record = fo4::test::begin_record(out, "RFGP", 0, 0x000840);
  fo4::test::ByteWriter value;
  value.u32(rnam);
  fo4::test::write_subrecord(out, "RNAM", value.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}

fo4::Plugin regn_with_worldspace(const char* name, std::uint32_t wnam) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "REGN", 0);
  const auto record = fo4::test::begin_record(out, "REGN", 0, 0x000860);
  fo4::test::ByteWriter value;
  value.u32(wnam);
  fo4::test::write_subrecord(out, "WNAM", value.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}

fo4::Plugin plugin_with_link(const char* name, std::string_view signature,
                             std::string_view subrecord, std::uint32_t target,
                             std::string_view master = {},
                             std::uint32_t record_id = 0x000840) {
  fo4::test::ByteWriter out;
  write_header(out, master);
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, record_id);
  fo4::test::ByteWriter value;
  value.u32(target);
  fo4::test::write_subrecord(out, subrecord, value.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, name);
}
}  // namespace

TEST(CheckErrors, MissingGameMasterReferenceGroupTargetIsNotAnError) {
  auto plugin = rfgp_with_reference("Fallout4.esm", 0x000850);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrors, MissingModReferenceGroupTargetIsStillUnresolved) {
  auto plugin = rfgp_with_reference("Mod.esp", 0x000850);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000850:Mod.esp"));
  EXPECT_EQ(unresolved[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrors, MissingGameMasterRegionWorldspaceIsNotAnError) {
  auto plugin = regn_with_worldspace("Fallout4.esm", 0x000850);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrors, MissingModRegionWorldspaceIsStillUnresolved) {
  auto plugin = regn_with_worldspace("Mod.esp", 0x000850);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000850:Mod.esp"));
  EXPECT_EQ(unresolved[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrors, DanglingGameMasterLinksAreExemptOnlyForKnownPairs) {
  struct Exemption {
    std::string_view record;
    std::string_view subrecord;
  };
  constexpr std::array<Exemption, 5> exemptions{{
      {"REFR", "XRFG"},
      {"REFR", "XMSP"},
      {"REFR", "XLKR"},
      {"ACHR", "XLCN"},
      {"SCCO", "SNAM"},
  }};

  for (const auto& exemption : exemptions) {
    const auto master =
        plugin_with_link("Fallout4.esm", exemption.record, exemption.subrecord, 0x000850);
    const std::vector<const fo4::Plugin*> master_order{&master};
    EXPECT_TRUE(
        of_code(fo4::check_load_order(master_order), fo4::CheckCode::unresolved_link).empty())
        << exemption.record << " " << exemption.subrecord;

    const auto mod = plugin_with_link("Mod.esp", exemption.record, exemption.subrecord, 0x000850);
    const std::vector<const fo4::Plugin*> mod_order{&mod};
    const auto unresolved =
        of_code(fo4::check_load_order(mod_order), fo4::CheckCode::unresolved_link);
    ASSERT_EQ(unresolved.size(), 1u) << exemption.record << " " << exemption.subrecord;
    EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000850:Mod.esp"));
    EXPECT_EQ(unresolved[0].severity, fo4::CheckSeverity::error);
  }

  const auto other_slot = plugin_with_link("Fallout4.esm", "REFR", "XLCN", 0x000850);
  const std::vector<const fo4::Plugin*> other_order{&other_slot};
  const auto unresolved = of_code(fo4::check_load_order(other_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].subrecord.to_string(), "XLCN");
  EXPECT_EQ(unresolved[0].severity, fo4::CheckSeverity::error);

  const auto other_record = plugin_with_link("Fallout4.esm", "ACHR", "XLKR", 0x000850);
  const std::vector<const fo4::Plugin*> other_record_order{&other_record};
  const auto other_record_unresolved =
      of_code(fo4::check_load_order(other_record_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(other_record_unresolved.size(), 1u);
  EXPECT_EQ(other_record_unresolved[0].subrecord.to_string(), "XLKR");
  EXPECT_EQ(other_record_unresolved[0].severity, fo4::CheckSeverity::error);

  const auto mod_new =
      plugin_with_link("Mod.esp", "REFR", "XLKR", 0x000850, "Fallout4.esm", 0x01000840);
  const std::vector<const fo4::Plugin*> mod_new_order{&mod_new};
  const auto mod_new_unresolved =
      of_code(fo4::check_load_order(mod_new_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(mod_new_unresolved.size(), 1u);
  EXPECT_EQ(mod_new_unresolved[0].record, fo4::FormKey::parse("000840:Mod.esp"));
  EXPECT_EQ(mod_new_unresolved[0].target, fo4::FormKey::parse("000850:Fallout4.esm"));
  EXPECT_EQ(mod_new_unresolved[0].severity, fo4::CheckSeverity::error);

  const auto mod_override =
      plugin_with_link("Mod.esp", "REFR", "XLKR", 0x000850, "Fallout4.esm", 0x00000840);
  const std::vector<const fo4::Plugin*> mod_override_order{&mod_override};
  EXPECT_TRUE(of_code(fo4::check_load_order(mod_override_order), fo4::CheckCode::unresolved_link)
                  .empty());
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
  auto plugin = fo4::read_plugin(out.bytes, "Mod.esp");
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
  const auto* selected_record = find_record(selected, "EXPL");
  const auto* other_record = find_record(other, "EXPL");
  ASSERT_NE(selected_record, nullptr);
  ASSERT_NE(other_record, nullptr);

  const auto slot = fo4::schema::formlink_field_at(fields, *selected_record, subrecord, 0, 4);
  ASSERT_TRUE(slot.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*slot, fo4::Signature::from_string("LIGH")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*slot, fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::formlink_field_at(fields, *other_record, subrecord, 0, 4).has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_at(fields, *selected_record, subrecord, 0, 0).has_value());
}

TEST(CheckErrors, RepeatedVariantSubrecordUsesItsOwnSelector) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "PACK", 0);
  const auto record = fo4::test::begin_record(out, "PACK", 0, 0x000800);
  fo4::test::ByteWriter first;
  first.i32(3);
  first.u32(0x000801);
  first.i32(0);
  fo4::test::write_subrecord(out, "PTDA", first.bytes);
  fo4::test::ByteWriter second;
  second.i32(1);
  second.u32(0x000802);
  second.i32(0);
  fo4::test::write_subrecord(out, "PTDA", second.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  auto plugin = fo4::read_plugin(out.bytes, "Pack.esp");
  const auto* pack = find_record(plugin, "PACK");
  ASSERT_NE(pack, nullptr);

  const auto fields = fo4::schema::formlink_fields(fo4::Signature::from_string("PACK"));
  const auto ptda = fo4::Signature::from_string("PTDA");
  const auto first_slot = fo4::schema::formlink_field_at(fields, *pack, ptda, 0, 4);
  const auto second_slot = fo4::schema::formlink_field_at(fields, *pack, ptda, 1, 4);
  ASSERT_TRUE(first_slot.has_value());
  ASSERT_TRUE(second_slot.has_value());
  EXPECT_EQ(first_slot->label, "TargetKeyword");
  EXPECT_EQ(second_slot->label, "TargetObject");
  EXPECT_FALSE(fo4::schema::formlink_field_at(fields, *pack, ptda, 2, 4).has_value());
}

TEST(CheckErrors, RepeatedVariantSubrecordTargetsAreCheckedPerInstance) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x000800);
  write_empty_record(out, "MISC", 0x000801);
  const auto group = fo4::test::begin_group(out, "PACK", 0);
  const auto record = fo4::test::begin_record(out, "PACK", 0, 0x000802);
  fo4::test::ByteWriter first;
  first.i32(3);
  first.u32(0x000800);
  first.i32(0);
  fo4::test::write_subrecord(out, "PTDA", first.bytes);
  fo4::test::ByteWriter second;
  second.i32(1);
  second.u32(0x000801);
  second.i32(0);
  fo4::test::write_subrecord(out, "PTDA", second.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  auto plugin = fo4::read_plugin(out.bytes, "Pack.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order), fo4::CheckCode::wrong_target_type).empty());
}

TEST(CheckErrors, PreflightFindingCarriesIssueCodeAsDetail) {
  const auto bytes = duplicate_scalar_bytes();
  auto plugin = fo4::read_plugin(bytes, "Dup.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::schema_preflight);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].detail, "duplicate_scalar_subrecord");
  EXPECT_EQ(findings[0].message.find(" ("), std::string::npos) << findings[0].message;
}

TEST(CheckErrors, CApiRowEndsWithIssueCode) {
  const auto root = std::filesystem::temp_directory_path() / "radicle_check_errors_detail";
  std::filesystem::create_directories(root);
  const auto path = root / "Dup.esp";
  const auto bytes = duplicate_scalar_bytes();
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto text = path.u8string();
  const std::string path_text{text.begin(), text.end()};
  const char* paths[] = {path_text.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  char** strings = nullptr;
  size_t count = 0;
  const auto status = radicle_env_check_errors(env, nullptr, 0, &strings, &count);
  const std::vector<std::string> rows(strings, strings + count);
  radicle_free_string_array(strings, count);
  radicle_env_destroy(env);
  std::filesystem::remove_all(root);

  ASSERT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].ends_with(" (duplicate_scalar_subrecord)")) << rows[0];
}

TEST(CheckErrors, LowIdsInACustomFirstMasterAreNotTreatedAsHardcoded) {
  fo4::test::ByteWriter game;
  write_header(game, {});
  write_empty_record(game, "KYWD", 0x000800);
  const auto master = fo4::read_plugin(game.bytes, "CustomMaster.esm");
  fo4::test::ByteWriter out;
  write_header(out, "CustomMaster.esm");
  write_misc_with_keywords(out, 0x01000801, {0x00000014});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const std::vector<const fo4::Plugin*> load_order{&master, &plugin};
  const auto unresolved = of_code(fo4::check_load_order(load_order), fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000014:CustomMaster.esm"));
}

TEST(CheckErrors, DeletedRecordContentIsFlaggedExceptReferenceBaseRecord) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_record_with_subrecords(out, "REFR", k_deleted_flag, 0x000800, {"NAME", "DATA"});
  write_record_with_subrecords(out, "REFR", k_deleted_flag, 0x000801, {"NAME"});
  write_record_with_subrecords(out, "KYWD", k_deleted_flag, 0x000802, {"DATA"});
  auto plugin = fo4::read_plugin(out.bytes, "Deleted.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::deleted_record_content);
  ASSERT_EQ(findings.size(), 2u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Deleted.esp"));
  EXPECT_EQ(findings[0].message, "Record marked as deleted, but contains: DATA");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
  EXPECT_EQ(findings[1].record, fo4::FormKey::parse("000802:Deleted.esp"));
}

TEST(CheckErrors, PartialFormContentOtherThanEditorIdIsFlagged) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_record_with_subrecords(out, "QUST", k_partial_form_flag, 0x000800, {"EDID", "SCRI"});
  write_record_with_subrecords(out, "QUST", k_partial_form_flag, 0x000801, {"EDID"});
  auto plugin = fo4::read_plugin(out.bytes, "Partial.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::partial_form_content);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Partial.esp"));
  EXPECT_EQ(findings[0].message, "Record is Partial Form, but contains: SCRI");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrors, PartialDialogueInfoOrderIsNotContentButOtherSubrecordsAre) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_record_with_subrecords(out, "DIAL", k_partial_form_flag, 0x000800, {"EDID", "INOM"});
  write_record_with_subrecords(out, "DIAL", k_partial_form_flag, 0x000801, {"EDID", "FULL"});
  auto plugin = fo4::read_plugin(out.bytes, "PartialDial.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::partial_form_content);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000801:PartialDial.esp"));
  EXPECT_EQ(findings[0].message, "Record is Partial Form, but contains: FULL");
}

TEST(CheckErrors, LightPluginObjectIdAboveTwelveBitsIsInvalid) {
  fo4::test::ByteWriter out;
  write_header(out, "Base.esm", k_small_file_flag);
  write_empty_record(out, "KYWD", 0x01000FFF);
  write_empty_record(out, "KYWD", 0x01001000);
  auto plugin = fo4::read_plugin(out.bytes, "Light.esl");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::light_object_id_overflow);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("001000:Light.esl"));
  EXPECT_EQ(findings[0].message, "ObjectID 001000 is invalid for a light module");
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrors, MasterIndexPastMasterTableIsInternalMismatch) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  write_empty_record(out, "KYWD", 0x01000800);
  write_empty_record(out, "KYWD", 0x00000801);
  auto plugin = fo4::read_plugin(out.bytes, "Stray.esp");
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::internal_form_id_mismatch);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000800:Stray.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
}

TEST(CheckErrors, RecordOwnedByAMasterThatLacksItIsInjected) {
  fo4::test::ByteWriter game;
  write_header(game, {});
  write_empty_record(game, "KYWD", 0x000800);
  write_empty_record(game, "MISC", 0x000801);
  const auto master = fo4::read_plugin(game.bytes, "Fallout4.esm");

  fo4::test::ByteWriter out;
  write_header(out, "Fallout4.esm");
  write_empty_record(out, "KYWD", 0x00000900);
  write_empty_record(out, "MISC", 0x00000801);
  write_empty_record(out, "KYWD", 0x01000802);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");

  const std::vector<const fo4::Plugin*> load_order{&master, &plugin};
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::injected_record), "injected_record");
  const auto findings = of_code(fo4::check_load_order(load_order), fo4::CheckCode::injected_record);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].record, fo4::FormKey::parse("000900:Fallout4.esm"));
  EXPECT_EQ(findings[0].plugin, fo4::PluginKey::from_filename("Patch.esp"));
  EXPECT_EQ(findings[0].severity, fo4::CheckSeverity::warning);
  EXPECT_NE(findings[0].message.find("000900:Fallout4.esm"), std::string::npos);

  const fo4::CheckOptions only_master{std::vector<fo4::PluginKey>{
      fo4::PluginKey::from_filename("Fallout4.esm")}};
  EXPECT_TRUE(of_code(fo4::check_load_order(load_order, only_master), fo4::CheckCode::injected_record)
                  .empty());
}
