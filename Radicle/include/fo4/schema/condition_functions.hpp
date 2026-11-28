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

// Constraint: Every generated row in index order; the single place consumers enumerate the Condition.Function table instead of keeping a copy of it.
std::span<const ConditionFunction> condition_functions();

const ConditionRunOn* condition_run_on(std::uint32_t value);

std::span<const ConditionRunOn> condition_run_ons();

// Constraint: Indexes (ascending) of the condition functions an editor offers, i.e. the schema rows flagged "editor": true. Consumers that need the descriptors of that subset read them here instead of keeping their own copy of the table.
std::span<const std::uint16_t> condition_editor_function_indices();

}  // namespace fo4::schema
