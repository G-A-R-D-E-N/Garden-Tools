#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::env {

struct PackageScheduleFields {
  std::int8_t month{-1};
  std::int8_t weekday{-1};
  std::int8_t date{-1};
  std::int8_t hour{-1};
  std::int8_t minute{-1};
  std::uint32_t duration_minutes{0};
};

struct ScheduleClock {
  std::int32_t minute_of_day{0};
  std::int8_t month{0};
  std::int8_t weekday{0};
  std::int8_t date{0};
  std::int8_t previous_month{0};
  std::int8_t previous_weekday{0};
  std::int8_t previous_date{0};
};

struct ScheduleWindow {
  bool evaluable{false};
  bool in_window{false};
  std::int32_t start_minute{0};
  std::int32_t end_minute{0};
  std::int32_t minutes_remaining{0};
};

ScheduleWindow schedule_window(const PackageScheduleFields& schedule, const ScheduleClock& clock);

struct ConditionRow {
  std::uint8_t flags_byte{0};
  std::uint32_t comparison_raw{0};
  std::uint16_t function{0};
  std::uint32_t run_on{0};
  std::uint32_t reference{0};
};

struct ConditionValue {
  bool known{false};
  double value{0.0};
  bool global_known{false};
  double global_value{0.0};
  std::uint32_t resolved_run_on{0};
  std::uint32_t resolved_reference{0};
};

enum class ConditionVerdict : std::uint8_t { false_value, true_value, unevaluable };

struct ConditionEvaluation {
  ConditionVerdict verdict{ConditionVerdict::true_value};
  std::size_t first_unevaluable_row{0};
  std::uint16_t first_unevaluable_function{0};
  std::size_t rows_evaluated{0};
};

ConditionEvaluation evaluate_conditions(std::span<const ConditionRow> rows, std::span<const ConditionValue> values);

struct PackageCandidate {
  PackageScheduleFields schedule{};
  std::span<const ConditionRow> conditions;
  std::span<const ConditionValue> values;
};

enum class CandidateReason : std::uint8_t {
  selected,
  forced,
  overridden,
  not_reached,
  schedule_outside_window,
  schedule_unevaluable,
  condition_false,
  condition_unevaluable,
};

enum class SelectionStatus : std::uint8_t { selected, none_eligible, undecided };

struct PackageSelection {
  SelectionStatus status{SelectionStatus::none_eligible};
  std::int32_t selected_index{-1};
  std::int32_t blocking_index{-1};
  std::vector<CandidateReason> reasons;
};

// forced_index must be a decision the caller already made; it is not an engine rule (re-answers actor-package-merge F9).
PackageSelection select_package(std::span<const PackageCandidate> candidates,
                                const ScheduleClock& clock,
                                std::int32_t forced_index);

}  // namespace fo4::env
