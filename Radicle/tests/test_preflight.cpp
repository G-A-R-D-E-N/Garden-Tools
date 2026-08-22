#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

fo4::Subrecord u32_subrecord(fo4::Signature signature, std::uint32_t value) {
  fo4::Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.resize(4);
  std::memcpy(subrecord.data.data(), &value, sizeof(value));
  return subrecord;
}

fo4::Subrecord f32_subrecord(fo4::Signature signature, float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&bits, &value, sizeof(bits));
  return u32_subrecord(signature, bits);
}

fo4::Record refr_record() {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("REFR");
  record.header.form_id = fo4::RawFormId{0x00000800u};
  record.form_key = fo4::FormKey::parse("000800:Patch.esp");
  return record;
}

}

TEST(SchemaPreflight, ValidGeneratedScalarsPass) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 50));
  record.subrecords.push_back(
      f32_subrecord(fo4::Signature::from_string("XSCL"), 1.25f));

  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
}

TEST(SchemaPreflight, MissingExistingOnlyScalarsAreLegal) {
  const auto record = refr_record();
  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());
}

TEST(SchemaPreflight, DuplicateScalarStorageIsAnError) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 50));
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 75));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].record, record.form_key);
  EXPECT_EQ(issues[0].path, "REFR\\HealthPercent");
  EXPECT_EQ(issues[0].code, "duplicate_scalar_subrecord");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::error);
}

TEST(SchemaPreflight, MalformedScalarWidthIsAnError) {
  auto record = refr_record();
  fo4::Subrecord scale;
  scale.signature = fo4::Signature::from_string("XSCL");
  scale.data = {std::byte{0}, std::byte{0}};
  record.subrecords.push_back(std::move(scale));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].path, "REFR\\Scale");
  EXPECT_EQ(issues[0].code, "invalid_scalar_storage");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::error);
}

TEST(SchemaPreflight, LegacyOutOfRangeValuesAreWarningsNotReadFailures) {
  auto record = refr_record();
  record.subrecords.push_back(
      u32_subrecord(fo4::Signature::from_string("XHLT"), 150));
  record.subrecords.push_back(
      f32_subrecord(fo4::Signature::from_string("XSCL"), 12.0f));

  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 2u);
  EXPECT_EQ(issues[0].code, "scalar_value_out_of_range");
  EXPECT_EQ(issues[0].severity, fo4::schema::PreflightSeverity::warning);
  EXPECT_EQ(issues[1].code, "scalar_value_out_of_range");
  EXPECT_EQ(issues[1].severity, fo4::schema::PreflightSeverity::warning);
}
