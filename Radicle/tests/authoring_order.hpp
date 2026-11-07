#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/record_constraints.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace fo4::test {

struct AuthoringOrderViolation {
  Signature subrecord{};
  std::size_t index{0};
  std::size_t last_index{0};
};

// Constraint: a decrease is legal only at the first rule of a maximal contiguous run of rules whose maximum is above 1, and only after that rule was seen.
inline std::optional<AuthoringOrderViolation> authoring_order_violation(
    const Record& record, std::span<const schema::SubrecordAuthoringConstraint> rules) {
  std::size_t last_index = 0;
  bool run_open = false;
  for (const auto& sub : record.subrecords) {
    std::size_t index = rules.size();
    for (std::size_t candidate = 0; candidate < rules.size(); ++candidate) {
      if (rules[candidate].signature == sub.signature) {
        index = candidate;
        break;
      }
    }
    if (index == rules.size()) continue;
    const bool repeatable = rules[index].maximum > 1;
    const bool run_first = repeatable && (index == 0 || rules[index - 1].maximum <= 1);
    const bool restarts = index < last_index && run_first && run_open;
    if (index < last_index && !restarts) {
      return AuthoringOrderViolation{sub.signature, index, last_index};
    }
    last_index = restarts ? index : (index > last_index ? index : last_index);
    if (!repeatable) {
      run_open = false;
    } else if (run_first) {
      run_open = true;
    }
  }
  return std::nullopt;
}

}  // namespace fo4::test
