#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/record_constraints.hpp"

#include <cstddef>
#include <optional>

namespace fo4::forms::detail {

// Constraint: the record must hold only subrecords its authoring rules list, in rule order; otherwise no position is proven.
inline std::optional<std::size_t> authoring_position(const Record& record, Signature target_signature) {
  const auto constraint = schema::record_authoring_constraint(record.header.signature);
  if (!constraint.has_value()) return std::nullopt;
  const auto rules = schema::authoring_subrecords(*constraint);
  const auto rule_index = [&](Signature signature) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == signature) return index;
    }
    return std::nullopt;
  };
  const auto target = rule_index(target_signature);
  if (!target.has_value()) return std::nullopt;
  std::optional<std::size_t> last;
  std::size_t position = 0;
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    const auto index = rule_index(record.subrecords[at].signature);
    if (!index.has_value() || (last.has_value() && *index < *last)) return std::nullopt;
    last = index;
    if (*index <= *target) position = at + 1;
  }
  return position;
}

}  // namespace fo4::forms::detail
