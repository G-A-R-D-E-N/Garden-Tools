#pragma once

#include "fo4/signature.hpp"

#include <array>
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
  // Constraint: a CELL is a direct child of its matching top-level GRUP, a world children GRUP, or an interior/exterior cell block GRUP.
  cell_block,
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

// Constraint: counts must be indexed like rules; returns the rule indices of a pair.
inline std::optional<std::array<std::size_t, 2>> subrecord_pair_rule_indices(
    const SubrecordPairConstraint& pair,
    std::span<const SubrecordAuthoringConstraint> rules) noexcept {
  std::optional<std::size_t> first;
  std::optional<std::size_t> second;
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == pair.subrecord) first = index;
    if (rules[index].signature == pair.partner) second = index;
  }
  if (!first.has_value() || !second.has_value()) return std::nullopt;
  return std::array{*first, *second};
}

// Constraint: counts must be indexed like rules; returns the first pair whose counts differ.
inline std::optional<SubrecordPairConstraint> mismatched_subrecord_pair(
    Signature record,
    std::span<const SubrecordAuthoringConstraint> rules,
    std::span<const std::size_t> counts) noexcept {
  for (const auto& pair : k_generated_subrecord_pairs) {
    if (pair.record != record) continue;
    const auto indices = subrecord_pair_rule_indices(pair, rules);
    if (!indices.has_value() || (*indices)[0] >= counts.size() ||
        (*indices)[1] >= counts.size()) {
      continue;
    }
    if (counts[(*indices)[0]] != counts[(*indices)[1]]) return pair;
  }
  return std::nullopt;
}

// Constraint: before and after must be indexed like rules.
inline std::optional<SubrecordPairConstraint> worsened_subrecord_pair(
    Signature record,
    std::span<const SubrecordAuthoringConstraint> rules,
    std::span<const std::size_t> before,
    std::span<const std::size_t> after) noexcept {
  for (const auto& pair : k_generated_subrecord_pairs) {
    if (pair.record != record) continue;
    const auto indices = subrecord_pair_rule_indices(pair, rules);
    if (!indices.has_value() || (*indices)[0] >= after.size() ||
        (*indices)[1] >= after.size() || (*indices)[0] >= before.size() ||
        (*indices)[1] >= before.size()) {
      continue;
    }
    const auto distance = [](std::size_t lhs, std::size_t rhs) noexcept {
      return lhs > rhs ? lhs - rhs : rhs - lhs;
    };
    const auto before_distance = distance(before[(*indices)[0]], before[(*indices)[1]]);
    const auto after_distance = distance(after[(*indices)[0]], after[(*indices)[1]]);
    if (after_distance > before_distance) return pair;
  }
  return std::nullopt;
}

}  // namespace fo4::schema
