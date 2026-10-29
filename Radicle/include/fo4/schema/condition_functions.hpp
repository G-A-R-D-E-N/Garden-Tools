#pragma once

#include "fo4/signature.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace fo4::schema {

enum class ConditionParameterKind {
  none,
  integer,
  float_value,
  string,
  form_link,
  enum_value,
  other,
};

struct ConditionParameterType {
  std::string_view name;
  ConditionParameterKind kind;
  std::span<const Signature> targets;
};

struct ConditionFunction {
  std::uint16_t index;
  std::string_view name;
  std::array<const ConditionParameterType*, 3> parameters;
};

struct ConditionRunOn {
  std::uint32_t value;
  std::string_view name;
  const ConditionParameterType* parameter3;
  bool reference_form_id;
};

const ConditionFunction* condition_function(std::uint16_t index);

const ConditionFunction* condition_function_by_name(std::string_view name);

const ConditionRunOn* condition_run_on(std::uint32_t value);

std::span<const ConditionRunOn> condition_run_ons();

}  // namespace fo4::schema
