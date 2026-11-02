#include "fo4/schema/condition_functions.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace fo4::schema {
namespace {

#include "fo4/schema/generated_condition_functions.inc"

constexpr char ascii_lower(char value) {
  return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

bool equal_ignore_ascii_case(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char a, char b) { return ascii_lower(a) == ascii_lower(b); });
}

}  // namespace

const ConditionFunction* condition_function(std::uint16_t index) {
  const auto begin = std::begin(k_generated_condition_functions);
  const auto end = std::end(k_generated_condition_functions);
  const auto it = std::lower_bound(begin, end, index,
                                   [](const ConditionFunction& row, std::uint16_t value) {
                                     return row.index < value;
                                   });
  if (it == end || it->index != index) {
    return nullptr;
  }
  return &*it;
}

const ConditionFunction* condition_function_by_name(std::string_view name) {
  const auto end = std::end(k_generated_condition_functions);
  const auto it = std::find_if(std::begin(k_generated_condition_functions), end,
                               [name](const ConditionFunction& row) {
                                 return equal_ignore_ascii_case(row.name, name);
                               });
  return it == end ? nullptr : &*it;
}

const ConditionRunOn* condition_run_on(std::uint32_t value) {
  const auto begin = std::begin(k_generated_condition_run_on);
  const auto end = std::end(k_generated_condition_run_on);
  const auto it = std::lower_bound(begin, end, value,
                                   [](const ConditionRunOn& row, std::uint32_t wanted) {
                                     return row.value < wanted;
                                   });
  if (it == end || it->value != value) {
    return nullptr;
  }
  return &*it;
}

std::span<const ConditionRunOn> condition_run_ons() {
  return k_generated_condition_run_on;
}

}  // namespace fo4::schema
