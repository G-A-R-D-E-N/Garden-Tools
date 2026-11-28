#include "c_api_env_internal.hpp"
#include "fo4/c_api_behavior.h"
#include "fo4/c_api_conditions.h"
#include "fo4/environment/package_rules.hpp"

#include <stdexcept>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status invalid_argument(const char* message) { return fail(RADICLE_STATUS_INVALID_ARGUMENT, message); }

fo4::env::PackageScheduleFields core_schedule(const radicle_package_schedule_fields& in) {
  fo4::env::PackageScheduleFields out;
  out.month = in.month;
  out.weekday = in.weekday;
  out.date = in.date;
  out.hour = in.hour;
  out.minute = in.minute;
  out.duration_minutes = in.duration_minutes;
  return out;
}

fo4::env::ScheduleClock core_clock(const radicle_schedule_clock& in) {
  fo4::env::ScheduleClock out;
  out.minute_of_day = in.minute_of_day;
  out.month = in.month;
  out.weekday = in.weekday;
  out.date = in.date;
  out.previous_month = in.previous_month;
  out.previous_weekday = in.previous_weekday;
  out.previous_date = in.previous_date;
  return out;
}

fo4::env::ConditionRow core_row(const radicle_condition& in) {
  fo4::env::ConditionRow out;
  out.flags_byte = in.flags_byte;
  out.comparison_raw = in.comparison_raw;
  out.function = in.function;
  out.run_on = in.run_on;
  out.reference = in.reference;
  return out;
}

fo4::env::ConditionValue core_value(const radicle_condition_value& in) {
  fo4::env::ConditionValue out;
  out.known = in.known != 0;
  out.value = in.value;
  out.global_known = in.global_known != 0;
  out.global_value = in.global_value;
  out.resolved_run_on = in.resolved_run_on;
  out.resolved_reference = in.resolved_reference;
  return out;
}

radicle_condition_verdict c_verdict(fo4::env::ConditionVerdict in) {
  switch (in) {
    case fo4::env::ConditionVerdict::false_value: return RADICLE_CONDITION_VERDICT_FALSE;
    case fo4::env::ConditionVerdict::true_value: return RADICLE_CONDITION_VERDICT_TRUE;
    case fo4::env::ConditionVerdict::unevaluable: return RADICLE_CONDITION_VERDICT_UNEVALUABLE;
  }
  return RADICLE_CONDITION_VERDICT_UNEVALUABLE;
}

radicle_package_candidate_reason c_reason(fo4::env::CandidateReason in) {
  switch (in) {
    case fo4::env::CandidateReason::selected: return RADICLE_PACKAGE_REASON_SELECTED;
    case fo4::env::CandidateReason::forced: return RADICLE_PACKAGE_REASON_FORCED;
    case fo4::env::CandidateReason::overridden: return RADICLE_PACKAGE_REASON_OVERRIDDEN;
    case fo4::env::CandidateReason::not_reached: return RADICLE_PACKAGE_REASON_NOT_REACHED;
    case fo4::env::CandidateReason::schedule_outside_window: return RADICLE_PACKAGE_REASON_SCHEDULE_OUTSIDE_WINDOW;
    case fo4::env::CandidateReason::schedule_unevaluable: return RADICLE_PACKAGE_REASON_SCHEDULE_UNEVALUABLE;
    case fo4::env::CandidateReason::condition_false: return RADICLE_PACKAGE_REASON_CONDITION_FALSE;
    case fo4::env::CandidateReason::condition_unevaluable: return RADICLE_PACKAGE_REASON_CONDITION_UNEVALUABLE;
  }
  return RADICLE_PACKAGE_REASON_NOT_REACHED;
}

radicle_package_selection_status c_selection_status(fo4::env::SelectionStatus in) {
  switch (in) {
    case fo4::env::SelectionStatus::selected: return RADICLE_PACKAGE_SELECTION_SELECTED;
    case fo4::env::SelectionStatus::none_eligible: return RADICLE_PACKAGE_SELECTION_NONE_ELIGIBLE;
    case fo4::env::SelectionStatus::undecided: return RADICLE_PACKAGE_SELECTION_UNDECIDED;
  }
  return RADICLE_PACKAGE_SELECTION_UNDECIDED;
}

}  // namespace

extern "C" {

radicle_status radicle_schedule_window(const radicle_package_schedule_fields* schedule,
                                       const radicle_schedule_clock* clock,
                                       radicle_schedule_window_result* out) {
  clear_error();
  if (!schedule || !clock || !out) return invalid_argument("schedule, clock and output are required");
  const auto window = fo4::env::schedule_window(core_schedule(*schedule), core_clock(*clock));
  out->evaluable = window.evaluable ? 1 : 0;
  out->in_window = window.in_window ? 1 : 0;
  out->start_minute = window.start_minute;
  out->end_minute = window.end_minute;
  out->minutes_remaining = window.minutes_remaining;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_conditions_evaluate(const radicle_condition* rows,
                                           const radicle_condition_value* values,
                                           size_t count, radicle_condition_result* out) {
  clear_error();
  if (!out || (count > 0 && (!rows || !values))) return invalid_argument("rows, values and output are required");
  return guarded("unknown exception while evaluating conditions", [&] {
    std::vector<fo4::env::ConditionRow> core_rows;
    std::vector<fo4::env::ConditionValue> core_values;
    core_rows.reserve(count);
    core_values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      core_rows.push_back(core_row(rows[i]));
      core_values.push_back(core_value(values[i]));
    }
    const auto result = fo4::env::evaluate_conditions(core_rows, core_values);
    out->verdict = c_verdict(result.verdict);
    out->first_unevaluable_row = result.first_unevaluable_row;
    out->first_unevaluable_function = result.first_unevaluable_function;
    out->rows_evaluated = result.rows_evaluated;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_package_select(const radicle_package_candidate* candidates, size_t count,
                                      const radicle_schedule_clock* clock, int32_t forced_index,
                                      radicle_package_candidate_reason* reasons,
                                      radicle_package_selection* out) {
  clear_error();
  if (!clock || !out || (count > 0 && !candidates)) return invalid_argument("candidates, clock and output are required");
  return guarded("unknown exception while selecting a package", [&] {
    std::vector<std::vector<fo4::env::ConditionRow>> rows(count);
    std::vector<std::vector<fo4::env::ConditionValue>> values(count);
    std::vector<fo4::env::PackageCandidate> core;
    core.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      const radicle_package_candidate& in = candidates[i];
      if (in.condition_value_count != in.condition_count) {
        return invalid_argument("each candidate needs one condition value per condition");
      }
      if (in.condition_count > 0 && (!in.conditions || !in.condition_values)) {
        return invalid_argument("candidate conditions and values are required");
      }
      for (size_t j = 0; j < in.condition_count; ++j) {
        rows[i].push_back(core_row(in.conditions[j]));
        values[i].push_back(core_value(in.condition_values[j]));
      }
      fo4::env::PackageCandidate candidate;
      candidate.schedule = core_schedule(in.schedule);
      candidate.conditions = std::span<const fo4::env::ConditionRow>(rows[i]);
      candidate.values = std::span<const fo4::env::ConditionValue>(values[i]);
      core.push_back(candidate);
    }
    fo4::env::PackageSelection selection;
    try {
      selection = fo4::env::select_package(core, core_clock(*clock), forced_index);
    } catch (const std::out_of_range& error) {
      return invalid_argument(error.what());
    }
    if (reasons) {
      for (size_t i = 0; i < count; ++i) reasons[i] = c_reason(selection.reasons[i]);
    }
    out->status = c_selection_status(selection.status);
    out->selected_index = selection.selected_index;
    out->blocking_index = selection.blocking_index;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
