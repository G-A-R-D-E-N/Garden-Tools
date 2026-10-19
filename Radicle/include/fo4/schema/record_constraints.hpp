#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fo4::schema {

enum class RecordPlacement {
  top_level,
  quest_children,
  topic_children,
  cell_children,
  cell_reference_children,
};

struct SubrecordAuthoringConstraint {
  Signature signature{};
  std::uint16_t minimum{0};
  std::uint16_t maximum{0};
  const std::uint8_t* default_data{nullptr};
  std::uint16_t default_size{0};
  bool uncounted{false};
  bool coupled{false};

  constexpr bool has_default() const noexcept { return default_data != nullptr; }
  std::span<const std::uint8_t> default_bytes() const noexcept {
    return {default_data, static_cast<std::size_t>(default_size)};
  }
};

struct RecordAuthoringConstraint {
  Signature record{};
  RecordPlacement placement{RecordPlacement::top_level};
  std::size_t first_subrecord{0};
  std::size_t subrecord_count{0};
};

// Constraint: both subrecords must occur the same number of times.
struct SubrecordPairConstraint {
  Signature record{};
  Signature subrecord{};
  Signature partner{};
};

#include "fo4/schema/generated_record_constraints.inc"

inline std::optional<RecordAuthoringConstraint> record_authoring_constraint(
    Signature record) noexcept {
  for (const auto& constraint : k_generated_record_authoring_constraints) {
    if (constraint.record == record) return constraint;
  }
  return std::nullopt;
}

inline std::span<const SubrecordAuthoringConstraint> authoring_subrecords(
    const RecordAuthoringConstraint& constraint) noexcept {
  return {k_generated_subrecord_authoring_constraints + constraint.first_subrecord,
          constraint.subrecord_count};
}

// Constraint: counts must be indexed like rules; returns the first pair whose counts differ.
inline std::optional<SubrecordPairConstraint> mismatched_subrecord_pair(
    Signature record,
    std::span<const SubrecordAuthoringConstraint> rules,
    std::span<const std::size_t> counts) noexcept {
  for (const auto& pair : k_generated_subrecord_pairs) {
    if (pair.record != record) continue;
    std::optional<std::size_t> first;
    std::optional<std::size_t> second;
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == pair.subrecord) first = index;
      if (rules[index].signature == pair.partner) second = index;
    }
    if (!first.has_value() || !second.has_value() || *first >= counts.size() ||
        *second >= counts.size()) {
      continue;
    }
    if (counts[*first] != counts[*second]) return pair;
  }
  return std::nullopt;
}

}  // namespace fo4::schema
