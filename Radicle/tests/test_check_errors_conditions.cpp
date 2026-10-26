#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint8_t k_use_aliases = 0x02;
constexpr std::uint8_t k_use_global = 0x04;
constexpr std::uint16_t k_get_distance = 1;
constexpr std::uint16_t k_get_seconds_passed = 12;
constexpr std::uint16_t k_get_in_faction = 71;
constexpr std::uint16_t k_get_event_data = 576;
constexpr std::uint32_t k_run_on_reference = 2;
constexpr std::uint32_t k_run_on_my_killer = 10;

struct Condition {
  std::uint16_t function{0};
  std::uint8_t flags{0};
  std::uint32_t comparison{0};
  std::uint32_t param1{0};
  std::uint32_t run_on{0};
  std::uint32_t reference{0};
};

Condition condition(std::uint16_t function, std::uint32_t param1 = 0) {
  Condition out;
  out.function = function;
  out.param1 = param1;
  return out;
}

void write_header(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

void write_empty_record(fo4::test::ByteWriter& out, std::string_view signature,
                        std::uint32_t form_id) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_condition(fo4::test::ByteWriter& out, const Condition& value) {
  fo4::test::ByteWriter data;
  data.u8(value.flags);
  data.u8(0);
  data.u8(0);
  data.u8(0);
  data.u32(value.comparison);
  data.u16(value.function);
  data.u16(0);
  data.u32(value.param1);
  data.u32(0);
  data.u32(value.run_on);
  data.u32(value.reference);
  data.u32(0xFFFFFFFFu);
  fo4::test::write_subrecord(out, "CTDA", data.bytes);
}

fo4::Plugin make_condition_plugin(const std::vector<Condition>& conditions) {
  fo4::test::ByteWriter out;
  write_header(out);
  write_empty_record(out, "FACT", 0x000800);
  write_empty_record(out, "WEAP", 0x000801);
  write_empty_record(out, "GLOB", 0x000802);
  const auto group = fo4::test::begin_group(out, "PERK", 0);
  const auto record = fo4::test::begin_record(out, "PERK", 0, 0x000803);
  for (const auto& value : conditions) write_condition(out, value);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

std::vector<fo4::CheckFinding> findings_for(const std::vector<Condition>& conditions) {
  auto plugin = make_condition_plugin(conditions);
  const std::vector<const fo4::Plugin*> load_order{&plugin};
  return fo4::check_load_order(load_order);
}

std::vector<fo4::CheckFinding> of_code(const std::vector<fo4::CheckFinding>& findings,
                                       fo4::CheckCode code) {
  std::vector<fo4::CheckFinding> out;
  std::copy_if(findings.begin(), findings.end(), std::back_inserter(out),
               [&](const fo4::CheckFinding& finding) { return finding.code == code; });
  return out;
}

}  // namespace

TEST(CheckErrorsConditions, ParameterOfWrongRecordTypeIsReported) {
  const auto wrong_type = of_code(findings_for({condition(k_get_in_faction, 0x000801)}),
                                  fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(wrong_type.size(), 1u);
  EXPECT_EQ(wrong_type[0].target, fo4::FormKey::parse("000801:Cond.esp"));
  EXPECT_EQ(wrong_type[0].record, fo4::FormKey::parse("000803:Cond.esp"));
  EXPECT_EQ(wrong_type[0].subrecord.to_string(), "CTDA");
  EXPECT_EQ(wrong_type[0].offset, 12u);
  EXPECT_EQ(wrong_type[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrorsConditions, ParameterOfExpectedRecordTypeIsNotReported) {
  const auto findings = findings_for({condition(k_get_in_faction, 0x000800)});
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::wrong_target_type).empty());
  EXPECT_TRUE(of_code(findings, fo4::CheckCode::unresolved_link).empty());
}

TEST(CheckErrorsConditions, UnresolvedParameterTargetIsAnError) {
  const auto unresolved = of_code(findings_for({condition(k_get_in_faction, 0x000999)}),
                                  fo4::CheckCode::unresolved_link);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].target, fo4::FormKey::parse("000999:Cond.esp"));
  EXPECT_EQ(unresolved[0].offset, 12u);
  EXPECT_EQ(unresolved[0].severity, fo4::CheckSeverity::error);
}

TEST(CheckErrorsConditions, AliasFlagSkipsReferenceParameterLink) {
  Condition aliased = condition(k_get_distance, 0x000999);
  aliased.flags = k_use_aliases;
  EXPECT_TRUE(of_code(findings_for({aliased}), fo4::CheckCode::unresolved_link).empty());

  const auto plain = condition(k_get_distance, 0x000999);
  EXPECT_EQ(of_code(findings_for({plain}), fo4::CheckCode::unresolved_link).size(), 1u);
}

TEST(CheckErrorsConditions, ComparisonGlobalMustBeGlob) {
  Condition wrong = condition(k_get_seconds_passed);
  wrong.flags = k_use_global;
  wrong.comparison = 0x000800;
  const auto wrong_type = of_code(findings_for({wrong}), fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(wrong_type.size(), 1u);
  EXPECT_EQ(wrong_type[0].target, fo4::FormKey::parse("000800:Cond.esp"));
  EXPECT_EQ(wrong_type[0].offset, 4u);

  Condition global = wrong;
  global.comparison = 0x000802;
  EXPECT_TRUE(of_code(findings_for({global}), fo4::CheckCode::wrong_target_type).empty());
}

TEST(CheckErrorsConditions, ReferenceRunOnChecksReferenceSignatures) {
  Condition wrong = condition(k_get_seconds_passed);
  wrong.run_on = k_run_on_reference;
  wrong.reference = 0x000800;
  const auto wrong_type = of_code(findings_for({wrong}), fo4::CheckCode::wrong_target_type);
  ASSERT_EQ(wrong_type.size(), 1u);
  EXPECT_EQ(wrong_type[0].offset, 24u);
}

TEST(CheckErrorsConditions, RunOnAboveMyKillerIsInvalid) {
  Condition bad = condition(k_get_seconds_passed);
  bad.run_on = k_run_on_my_killer + 1;
  const auto invalid = of_code(findings_for({bad}), fo4::CheckCode::invalid_condition);
  ASSERT_EQ(invalid.size(), 1u);
  EXPECT_EQ(invalid[0].severity, fo4::CheckSeverity::error);
  EXPECT_EQ(invalid[0].offset, 20u);

  Condition last = condition(k_get_seconds_passed);
  last.run_on = k_run_on_my_killer;
  EXPECT_TRUE(of_code(findings_for({last}), fo4::CheckCode::invalid_condition).empty());
}

TEST(CheckErrorsConditions, UnknownFunctionIndexIsInvalid) {
  const auto invalid = of_code(findings_for({condition(65535)}), fo4::CheckCode::invalid_condition);
  ASSERT_EQ(invalid.size(), 1u);
  EXPECT_EQ(invalid[0].offset, 8u);
  EXPECT_EQ(invalid[0].severity, fo4::CheckSeverity::error);

  const auto event = findings_for({condition(k_get_event_data)});
  EXPECT_TRUE(of_code(event, fo4::CheckCode::invalid_condition).empty());
}

TEST(CheckErrorsConditions, InvalidConditionCodeNameIsStable) {
  EXPECT_EQ(fo4::check_code_name(fo4::CheckCode::invalid_condition), "invalid_condition");
}
