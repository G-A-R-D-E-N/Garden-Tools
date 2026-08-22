#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fo4::schema {

enum class RecordPlacement {
  top_level,
};

struct SubrecordAuthoringConstraint {
  Signature signature{};
  std::uint16_t minimum{0};
  std::uint16_t maximum{0};
};

struct RecordAuthoringConstraint {
  Signature record{};
  RecordPlacement placement{RecordPlacement::top_level};
  std::size_t first_subrecord{0};
  std::size_t subrecord_count{0};
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

}  // namespace fo4::schema
