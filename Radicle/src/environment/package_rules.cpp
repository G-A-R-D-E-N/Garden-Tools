#include "fo4/environment/package_rules.hpp"

#include "fo4/schema/ctda.hpp"

#include <cstring>
#include <stdexcept>

namespace fo4::env {
namespace {

constexpr std::int32_t k_minutes_per_day = 1440;
constexpr std::uint8_t k_compare_shift = 5;
constexpr std::uint8_t k_compare_mask = 0x7u;
constexpr std::int8_t k_min_clock_month = 1;
constexpr std::int8_t k_max_month = 12;
constexpr std::int8_t k_max_weekday = 6;
constexpr std::int8_t k_max_date = 31;

bool stored_in_domain(std::int8_t value, std::int8_t highest) {
  return value == -1 || (value >= 0 && value <= highest);
}

bool clock_in_domain(std::int8_t value, std::int8_t lowest, std::int8_t highest) {
  return value >= lowest && value <= highest;
}

bool clock_valid(const ScheduleClock& clock) {
  return clock.minute_of_day >= 0 && clock.minute_of_day < k_minutes_per_day &&
         clock_in_domain(clock.month, k_min_clock_month, k_max_month) &&
         clock_in_domain(clock.weekday, 0, k_max_weekday) && clock_in_domain(clock.date, 0, k_max_date) &&
         clock_in_domain(clock.previous_month, k_min_clock_month, k_max_month) &&
         clock_in_domain(clock.previous_weekday, 0, k_max_weekday) &&
         clock_in_domain(clock.previous_date, 0, k_max_date);
}

bool field_matches(std::int8_t field, std::int8_t value) {
  return field == -1 || field == value;
}

bool zero_or_field_matches(std::int8_t field, std::int8_t value) {
  return field == 0 || field_matches(field, value);
}

bool calendar_matches(const PackageScheduleFields& schedule, std::int8_t month, std::int8_t weekday,
                      std::int8_t date) {
  return zero_or_field_matches(schedule.month, month) && field_matches(schedule.weekday, weekday) &&
         zero_or_field_matches(schedule.date, date);
}

bool calendar_is_wildcard(const PackageScheduleFields& schedule) {
  return schedule.month <= 0 && schedule.weekday == -1 && schedule.date <= 0;
}

double stored_float(std::uint32_t raw) {
  float value = 0.0f;
  static_assert(sizeof(value) == sizeof(raw));
  std::memcpy(&value, &raw, sizeof(value));
  return static_cast<double>(value);
}

ConditionVerdict verdict_of(bool value) {
  return value ? ConditionVerdict::true_value : ConditionVerdict::false_value;
}

ConditionVerdict compare_row(const ConditionRow& row, const ConditionValue& value) {
  if (!value.known) return ConditionVerdict::unevaluable;
  if (value.resolved_run_on != row.run_on || value.resolved_reference != row.reference) {
    return ConditionVerdict::unevaluable;
  }
  double rhs = stored_float(row.comparison_raw);
  if ((row.flags_byte & fo4::k_ctda_flag_use_global) != 0) {
    if (!value.global_known) return ConditionVerdict::unevaluable;
    rhs = value.global_value;
  }
  const double lhs = value.value;
  switch ((row.flags_byte >> k_compare_shift) & k_compare_mask) {
    case 0: return verdict_of(lhs == rhs);
    case 1: return verdict_of(lhs != rhs);
    case 2: return verdict_of(lhs > rhs);
    case 3: return verdict_of(lhs >= rhs);
    case 4: return verdict_of(lhs < rhs);
    case 5: return verdict_of(lhs <= rhs);
    default: return ConditionVerdict::unevaluable;
  }
}

}  // namespace

ScheduleWindow schedule_window(const PackageScheduleFields& schedule, const ScheduleClock& clock) {
  ScheduleWindow out;
  const bool calendar_ok = stored_in_domain(schedule.month, k_max_month) &&
                           stored_in_domain(schedule.weekday, k_max_weekday) &&
                           stored_in_domain(schedule.date, k_max_date);
  if (!calendar_ok) return out;
  if (schedule.hour == -1) {
    if (calendar_is_wildcard(schedule)) {
      out.evaluable = true;
      out.in_window = true;
      return out;
    }
    if (!clock_valid(clock)) return out;
    out.evaluable = true;
    out.in_window = calendar_matches(schedule, clock.month, clock.weekday, clock.date);
    return out;
  }
  const bool hour_ok = schedule.hour >= 0 && schedule.hour <= 23;
  const bool minute_ok = schedule.minute >= 0 && schedule.minute <= 59;
  const bool duration_ok =
      schedule.duration_minutes <= static_cast<std::uint32_t>(k_minutes_per_day);
  if (!hour_ok || !minute_ok || !duration_ok || !clock_valid(clock)) return out;
  out.evaluable = true;

  const std::int32_t start = schedule.hour * 60 + schedule.minute;
  const auto duration = static_cast<std::int32_t>(schedule.duration_minutes);
  out.start_minute = start;
  out.end_minute = (start + duration) % k_minutes_per_day;

  const std::int32_t today_offset = clock.minute_of_day - start;
  if (calendar_matches(schedule, clock.month, clock.weekday, clock.date) && today_offset >= 0 &&
      (today_offset < duration || today_offset == 0)) {
    out.in_window = true;
    out.minutes_remaining = duration - today_offset;
    return out;
  }
  const std::int32_t yesterday_offset = today_offset + k_minutes_per_day;
  if (calendar_matches(schedule, clock.previous_month, clock.previous_weekday, clock.previous_date) &&
      yesterday_offset >= 0 && yesterday_offset < duration) {
    out.in_window = true;
    out.minutes_remaining = duration - yesterday_offset;
  }
  return out;
}

ConditionEvaluation evaluate_conditions(std::span<const ConditionRow> rows, std::span<const ConditionValue> values) {
  ConditionEvaluation out;
  out.rows_evaluated = rows.size();
  const ConditionValue missing{};

  bool any_false_group = false;
  bool any_unknown_group = false;
  bool group_true = false;
  bool group_unknown = false;
  std::size_t group_first_unknown = 0;
  std::uint16_t group_function = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const ConditionValue& value = i < values.size() ? values[i] : missing;
    const ConditionVerdict verdict = compare_row(rows[i], value);
    if (verdict == ConditionVerdict::true_value) {
      group_true = true;
    } else if (verdict == ConditionVerdict::unevaluable && !group_unknown) {
      group_unknown = true;
      group_first_unknown = i;
      group_function = rows[i].function;
    }
    const bool closes_group = (rows[i].flags_byte & fo4::k_ctda_flag_or) == 0 || i + 1 == rows.size();
    if (closes_group) {
      if (!group_true) {
        if (group_unknown) {
          if (!any_unknown_group) {
            any_unknown_group = true;
            out.first_unevaluable_row = group_first_unknown;
            out.first_unevaluable_function = group_function;
          }
        } else {
          any_false_group = true;
        }
      }
      group_true = false;
      group_unknown = false;
    }
  }

  if (any_false_group) {
    out.verdict = ConditionVerdict::false_value;
  } else if (any_unknown_group) {
    out.verdict = ConditionVerdict::unevaluable;
  } else {
    out.verdict = ConditionVerdict::true_value;
  }
  if (out.verdict != ConditionVerdict::unevaluable) {
    out.first_unevaluable_row = 0;
    out.first_unevaluable_function = 0;
  }
  return out;
}

PackageSelection select_package(std::span<const PackageCandidate> candidates,
                                const ScheduleClock& clock,
                                std::int32_t forced_index) {
  if (forced_index < -1 || (forced_index >= 0 && static_cast<std::size_t>(forced_index) >= candidates.size())) {
    throw std::out_of_range("forced index is out of range");
  }
  PackageSelection out;
  out.reasons.assign(candidates.size(), CandidateReason::not_reached);
  if (forced_index >= 0) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      out.reasons[i] = static_cast<std::int32_t>(i) == forced_index ? CandidateReason::forced : CandidateReason::overridden;
    }
    out.status = SelectionStatus::selected;
    out.selected_index = forced_index;
    return out;
  }

  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const PackageCandidate& candidate = candidates[i];
    const ScheduleWindow window = schedule_window(candidate.schedule, clock);
    if (!window.evaluable) {
      out.reasons[i] = CandidateReason::schedule_unevaluable;
      out.status = SelectionStatus::undecided;
      out.blocking_index = static_cast<std::int32_t>(i);
      return out;
    }
    if (!window.in_window) {
      out.reasons[i] = CandidateReason::schedule_outside_window;
      continue;
    }
    const ConditionEvaluation evaluation = evaluate_conditions(candidate.conditions, candidate.values);
    if (evaluation.verdict == ConditionVerdict::unevaluable) {
      out.reasons[i] = CandidateReason::condition_unevaluable;
      out.status = SelectionStatus::undecided;
      out.blocking_index = static_cast<std::int32_t>(i);
      return out;
    }
    if (evaluation.verdict == ConditionVerdict::false_value) {
      out.reasons[i] = CandidateReason::condition_false;
      continue;
    }
    out.reasons[i] = CandidateReason::selected;
    out.status = SelectionStatus::selected;
    out.selected_index = static_cast<std::int32_t>(i);
    return out;
  }
  out.status = SelectionStatus::none_eligible;
  return out;
}

}  // namespace fo4::env
