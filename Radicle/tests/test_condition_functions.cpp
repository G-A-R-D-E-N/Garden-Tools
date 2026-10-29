#include "fo4/schema/condition_functions.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fo4::schema {
namespace {

struct CtdaFunctionRow {
  std::uint16_t index;
  std::uint8_t flags;
};

constexpr CtdaFunctionRow k_ctda_rows[] = {
#include "fo4/schema/ctda_functions.inc"
};

struct Lookup {
  std::uint16_t index;
  std::string_view name;
};

constexpr Lookup k_known_lookups[] = {
    {1, "GetDistance"},
    {14, "GetValue"},
    {58, "GetStage"},
    {72, "GetIsID"},
    {810, "GetVATSTargetLimbVisible"},
    {817, "IsInRobotWorkbench"},
};

struct RunOnLookup {
  std::uint32_t value;
  std::string_view name;
};

std::uint8_t expected_flags(const ConditionFunction& function) {
  std::uint8_t flags = 0;
  for (std::size_t slot = 0; slot < 2; ++slot) {
    const ConditionParameterType* parameter = function.parameters[slot];
    if (parameter != nullptr && parameter->kind == ConditionParameterKind::form_link) {
      flags = static_cast<std::uint8_t>(flags | (1u << slot));
    }
  }
  return flags;
}

std::uint8_t ctda_flags(std::uint16_t index) {
  for (const CtdaFunctionRow& row : k_ctda_rows) {
    if (row.index == index) {
      return row.flags;
    }
  }
  return 0;
}

TEST(ConditionFunctionsTest, KnownLookupsByIndex) {
  for (const Lookup& expected : k_known_lookups) {
    const ConditionFunction* function = condition_function(expected.index);
    ASSERT_NE(function, nullptr) << expected.index;
    EXPECT_EQ(function->index, expected.index);
    EXPECT_EQ(function->name, expected.name);
  }
}

TEST(ConditionFunctionsTest, NameLookupIgnoresAsciiCase) {
  const ConditionFunction* by_name = condition_function_by_name("getdistance");
  ASSERT_NE(by_name, nullptr);
  EXPECT_EQ(by_name->index, 1);
  EXPECT_EQ(by_name, condition_function(1));

  by_name = condition_function_by_name("GETVALUE");
  ASSERT_NE(by_name, nullptr);
  EXPECT_EQ(by_name->index, 14);

  by_name = condition_function_by_name("GetIsID");
  ASSERT_NE(by_name, nullptr);
  EXPECT_EQ(by_name->index, 72);
}

TEST(ConditionFunctionsTest, UnknownIndexAndNameReturnNull) {
  EXPECT_EQ(condition_function(65535), nullptr);
  EXPECT_EQ(condition_function_by_name("NotAConditionFunction"), nullptr);
  EXPECT_EQ(condition_function_by_name(""), nullptr);
}

TEST(ConditionFunctionsTest, ReferenceParameterTargetsInXEditOrder) {
  const ConditionFunction* function = condition_function_by_name("GetDistance");
  ASSERT_NE(function, nullptr);
  const ConditionParameterType* parameter = function->parameters[0];
  ASSERT_NE(parameter, nullptr);
  EXPECT_EQ(parameter->name, "reference");
  EXPECT_EQ(parameter->kind, ConditionParameterKind::form_link);
  ASSERT_EQ(parameter->targets.size(), 12u);
  EXPECT_EQ(parameter->targets.front(), Signature::from_string("ACHR"));
  EXPECT_EQ(parameter->targets[1], Signature::from_string("PARW"));
  EXPECT_EQ(parameter->targets.back(), Signature::from_string("TRGT"));
  EXPECT_EQ(function->parameters[1], nullptr);
}

TEST(ConditionFunctionsTest, CtdaFlagsAgreeWithTable) {
  for (const CtdaFunctionRow& row : k_ctda_rows) {
    const ConditionFunction* function = condition_function(row.index);
    ASSERT_NE(function, nullptr) << row.index;
    EXPECT_EQ(row.flags, expected_flags(*function)) << function->name;
  }
  for (std::uint32_t index = 0; index <= 0xFFFFu; ++index) {
    const ConditionFunction* function = condition_function(static_cast<std::uint16_t>(index));
    if (function == nullptr) {
      continue;
    }
    EXPECT_EQ(ctda_flags(function->index), expected_flags(*function)) << function->name;
  }
}

TEST(ConditionFunctionsTest, RunOnTableMatchesXEditEnum) {
  constexpr RunOnLookup k_expected[] = {
      {0, "Subject"},
      {1, "Target"},
      {2, "Reference"},
      {3, "Combat Target"},
      {4, "Linked Reference"},
      {5, "Quest Alias"},
      {6, "Package Data"},
      {7, "Event Data"},
      {8, "Command Target"},
      {9, "Event Camera Ref"},
      {10, "My Killer"},
  };
  for (const RunOnLookup& expected : k_expected) {
    const ConditionRunOn* row = condition_run_on(expected.value);
    ASSERT_NE(row, nullptr) << expected.value;
    EXPECT_EQ(row->value, expected.value);
    EXPECT_EQ(row->name, expected.name);
  }
  EXPECT_EQ(condition_run_on(11), nullptr);
  EXPECT_EQ(condition_run_on(65535), nullptr);
  EXPECT_EQ(condition_run_on(0xFFFFFFFFu), nullptr);
}

TEST(ConditionFunctionsTest, RunOnParameter3AndReferenceFollowXEditRules) {
  for (std::uint32_t value = 0; value <= 10; ++value) {
    const ConditionRunOn* row = condition_run_on(value);
    ASSERT_NE(row, nullptr) << value;
    ASSERT_NE(row->parameter3, nullptr) << value;
    std::string_view expected = "integer";
    if (value == 5) expected = "alias";
    if (value == 7) expected = "event_data_code";
    EXPECT_EQ(row->parameter3->name, expected) << row->name;
    EXPECT_EQ(row->reference_form_id, value == 2) << row->name;
  }
}

}  // namespace
}  // namespace fo4::schema
