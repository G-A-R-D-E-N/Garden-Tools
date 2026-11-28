#include "fo4/c_api.h"
#include "fo4/c_api_behavior.h"
#include "fo4/c_api_conditions.h"
#include "fo4/environment/package_rules.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using fo4::env::CandidateReason;
using fo4::env::ConditionRow;
using fo4::env::ConditionValue;
using fo4::env::ConditionVerdict;
using fo4::env::PackageCandidate;
using fo4::env::PackageScheduleFields;
using fo4::env::ScheduleClock;
using fo4::env::SelectionStatus;

constexpr std::uint8_t kOr = 0x01;
constexpr std::uint8_t kUseGlobal = 0x04;
constexpr std::uint8_t kEqual = 0x00;
constexpr std::uint8_t kGreater = 0x40;
constexpr std::uint8_t kLess = 0x80;
constexpr std::uint8_t kInvalidOperator = 0xC0;
constexpr std::uint32_t kOneFloat = 0x3F800000u;

ConditionRow row(std::uint8_t flags, std::uint32_t comparison, std::uint16_t function,
                 std::uint32_t run_on = 0, std::uint32_t reference = 0) {
  return ConditionRow{flags, comparison, function, run_on, reference};
}

ConditionValue known(double value) {
  ConditionValue out;
  out.known = true;
  out.value = value;
  return out;
}

ConditionValue echoed(double value, std::uint32_t run_on, std::uint32_t reference) {
  ConditionValue out = known(value);
  out.resolved_run_on = run_on;
  out.resolved_reference = reference;
  return out;
}

ConditionValue unknown() { return ConditionValue{}; }

PackageScheduleFields window(std::int8_t hour, std::int8_t minute, std::uint32_t duration) {
  PackageScheduleFields out;
  out.hour = hour;
  out.minute = minute;
  out.duration_minutes = duration;
  return out;
}

ScheduleClock clock_at(std::int32_t minute_of_day) {
  ScheduleClock out;
  out.minute_of_day = minute_of_day;
  out.month = 3;
  out.weekday = 2;
  out.date = 14;
  out.previous_month = 3;
  out.previous_weekday = 1;
  out.previous_date = 13;
  return out;
}

bool in_window(const PackageScheduleFields& schedule, std::int32_t minute_of_day) {
  return fo4::env::schedule_window(schedule, clock_at(minute_of_day)).in_window;
}

TEST(PackageRulesSchedule, WildcardCalendarCoversTheWindowOnAnyDay) {
  const auto result = fo4::env::schedule_window(window(8, 0, 120), clock_at(8 * 60 + 30));
  EXPECT_TRUE(result.evaluable);
  EXPECT_TRUE(result.in_window);
  EXPECT_EQ(result.minutes_remaining, 90);
  EXPECT_EQ(result.start_minute, 480);
  EXPECT_EQ(result.end_minute, 600);
}

TEST(PackageRulesSchedule, StartIsInclusiveAndEndIsExclusive) {
  const auto schedule = window(8, 0, 120);
  EXPECT_FALSE(in_window(schedule, 479));
  EXPECT_TRUE(in_window(schedule, 480));
  EXPECT_TRUE(in_window(schedule, 599));
  EXPECT_FALSE(in_window(schedule, 600));
}

TEST(PackageRulesSchedule, FixedWeekdayMustMatchTheCurrentDay) {
  auto schedule = window(8, 0, 120);
  schedule.weekday = 5;
  EXPECT_FALSE(in_window(schedule, 490));
  schedule.weekday = 2;
  EXPECT_TRUE(in_window(schedule, 490));
}

TEST(PackageRulesSchedule, FixedMonthMustMatchTheCurrentMonth) {
  auto schedule = window(8, 0, 120);
  schedule.month = 2;
  EXPECT_FALSE(in_window(schedule, 490));
  schedule.month = 3;
  EXPECT_TRUE(in_window(schedule, 490));
  schedule.month = 12;
  EXPECT_FALSE(in_window(schedule, 490));
  ScheduleClock december = clock_at(490);
  december.month = 12;
  EXPECT_TRUE(fo4::env::schedule_window(schedule, december).in_window);
  schedule.month = 0;
  EXPECT_TRUE(in_window(schedule, 490));
}

TEST(PackageRulesSchedule, FixedDateMustMatchTheCurrentDate) {
  auto schedule = window(8, 0, 120);
  schedule.date = 13;
  EXPECT_FALSE(in_window(schedule, 490));
  schedule.date = 14;
  EXPECT_TRUE(in_window(schedule, 490));
  schedule.date = 0;
  EXPECT_TRUE(in_window(schedule, 490));
}

TEST(PackageRulesSchedule, WindowCrossingMidnightUsesYesterdaysCalendar) {
  auto schedule = window(23, 30, 90);
  schedule.weekday = 1;
  const auto result = fo4::env::schedule_window(schedule, clock_at(30));
  EXPECT_TRUE(result.in_window);
  EXPECT_EQ(result.minutes_remaining, 30);
  EXPECT_EQ(result.start_minute, 1410);
  EXPECT_EQ(result.end_minute, 60);

  schedule.weekday = 2;
  EXPECT_FALSE(in_window(schedule, 30));
}

TEST(PackageRulesSchedule, WindowCrossingMidnightChecksYesterdaysDate) {
  auto schedule = window(23, 30, 90);
  schedule.date = 13;
  EXPECT_TRUE(in_window(schedule, 30));
  schedule.date = 14;
  EXPECT_FALSE(in_window(schedule, 30));
}

TEST(PackageRulesSchedule, FullDayWindowIsEvaluable) {
  const auto result = fo4::env::schedule_window(window(0, 0, 1440), clock_at(900));
  EXPECT_TRUE(result.evaluable);
  EXPECT_TRUE(result.in_window);
  EXPECT_EQ(result.minutes_remaining, 540);
}

TEST(PackageRulesSchedule, ZeroDurationMatchesOnlyTheStartMinute) {
  const auto at_start = fo4::env::schedule_window(window(8, 0, 0), clock_at(480));
  EXPECT_TRUE(at_start.evaluable);
  EXPECT_TRUE(at_start.in_window);
  EXPECT_EQ(at_start.minutes_remaining, 0);
  EXPECT_FALSE(in_window(window(8, 0, 0), 479));
  EXPECT_FALSE(in_window(window(8, 0, 0), 481));
  EXPECT_TRUE(fo4::env::schedule_window(window(8, 0, 0), clock_at(479)).evaluable);
  EXPECT_FALSE(in_window(window(0, 0, 0), 1439));
}

TEST(PackageRulesSchedule, OutOfRangeHourOrOverlongDurationIsUnevaluable) {
  EXPECT_FALSE(fo4::env::schedule_window(window(24, 0, 60), clock_at(0)).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(window(8, 60, 60), clock_at(0)).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(window(8, 0, 1441), clock_at(0)).evaluable);
}

TEST(PackageRulesSchedule, PartialScheduleIsUnevaluable) {
  EXPECT_FALSE(fo4::env::schedule_window(window(8, -1, 60), clock_at(480)).evaluable);
}

TEST(PackageRulesSchedule, OutOfDomainStoredCalendarIsUnevaluable) {
  auto month = window(8, 0, 120);
  month.month = 13;
  auto weekday = window(8, 0, 120);
  weekday.weekday = 7;
  auto date = window(8, 0, 120);
  date.date = 32;
  EXPECT_FALSE(fo4::env::schedule_window(month, clock_at(490)).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(weekday, clock_at(490)).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(date, clock_at(490)).evaluable);
}

TEST(PackageRulesSchedule, DateZeroIsTheAnyDayWildcard) {
  auto schedule = window(23, 30, 90);
  schedule.date = 0;
  const auto today = fo4::env::schedule_window(schedule, clock_at(23 * 60 + 40));
  EXPECT_TRUE(today.evaluable);
  EXPECT_TRUE(today.in_window);
  const auto after_midnight = fo4::env::schedule_window(schedule, clock_at(30));
  EXPECT_TRUE(after_midnight.evaluable);
  EXPECT_TRUE(after_midnight.in_window);
}

TEST(PackageRulesSchedule, HourUnsetIsNoWindowSoOnlyTheCalendarDecides) {
  PackageScheduleFields all_wildcard;
  auto every_minute = fo4::env::schedule_window(all_wildcard, clock_at(0));
  EXPECT_TRUE(every_minute.evaluable);
  EXPECT_TRUE(every_minute.in_window);
  EXPECT_TRUE(fo4::env::schedule_window(all_wildcard, clock_at(1439)).in_window);

  PackageScheduleFields matching_weekday;
  matching_weekday.weekday = 2;
  const auto match = fo4::env::schedule_window(matching_weekday, clock_at(900));
  EXPECT_TRUE(match.evaluable);
  EXPECT_TRUE(match.in_window);
  EXPECT_TRUE(fo4::env::schedule_window(matching_weekday, clock_at(0)).in_window);

  PackageScheduleFields other_weekday;
  other_weekday.weekday = 5;
  const auto miss = fo4::env::schedule_window(other_weekday, clock_at(900));
  EXPECT_TRUE(miss.evaluable);
  EXPECT_FALSE(miss.in_window);

  PackageScheduleFields other_month;
  other_month.month = 4;
  EXPECT_FALSE(fo4::env::schedule_window(other_month, clock_at(900)).in_window);
  PackageScheduleFields other_date;
  other_date.date = 15;
  EXPECT_FALSE(fo4::env::schedule_window(other_date, clock_at(900)).in_window);
}

TEST(PackageRulesSchedule, HourUnsetIgnoresMinuteAndDuration) {
  PackageScheduleFields schedule;
  schedule.minute = 30;
  schedule.duration_minutes = 60;
  EXPECT_TRUE(fo4::env::schedule_window(schedule, clock_at(1000)).in_window);
}

TEST(PackageRulesSchedule, WindowlessWildcardCalendarIgnoresAnUnsetClock) {
  const auto result = fo4::env::schedule_window(PackageScheduleFields{}, ScheduleClock{});
  EXPECT_TRUE(result.evaluable);
  EXPECT_TRUE(result.in_window);
}

TEST(PackageRulesSchedule, CalendarOnlyNeedsAValidClock) {
  PackageScheduleFields schedule;
  schedule.weekday = 2;
  ScheduleClock clock = clock_at(900);
  clock.month = 0;
  EXPECT_FALSE(fo4::env::schedule_window(schedule, clock).evaluable);
}

TEST(PackageRulesSchedule, OutOfDomainClockIsUnevaluable) {
  const auto schedule = window(8, 0, 120);
  ScheduleClock month = clock_at(490);
  month.month = 13;
  ScheduleClock previous = clock_at(490);
  previous.previous_weekday = 7;
  ScheduleClock month_zero = clock_at(490);
  month_zero.month = 0;
  EXPECT_FALSE(fo4::env::schedule_window(schedule, month).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(schedule, month_zero).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(schedule, previous).evaluable);
  EXPECT_FALSE(fo4::env::schedule_window(schedule, clock_at(1440)).evaluable);
}

TEST(PackageRulesGrammar, EmptyListPasses) {
  const auto result = fo4::env::evaluate_conditions({}, {});
  EXPECT_EQ(result.verdict, ConditionVerdict::true_value);
  EXPECT_EQ(result.rows_evaluated, 0u);
}

TEST(PackageRulesGrammar, OperatorComesFromTheHighFlagBits) {
  const std::vector<ConditionRow> greater{row(kGreater, kOneFloat, 1)};
  const std::vector<ConditionValue> two{known(2.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(greater, two).verdict, ConditionVerdict::true_value);

  const std::vector<ConditionRow> less{row(kLess, kOneFloat, 1)};
  EXPECT_EQ(fo4::env::evaluate_conditions(less, two).verdict, ConditionVerdict::false_value);

  const std::vector<ConditionRow> equal{row(kEqual, kOneFloat, 1)};
  EXPECT_EQ(fo4::env::evaluate_conditions(equal, two).verdict, ConditionVerdict::false_value);
  const std::vector<ConditionValue> one{known(1.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(equal, one).verdict, ConditionVerdict::true_value);
}

TEST(PackageRulesGrammar, UnknownOperatorCodeIsUnevaluable) {
  const std::vector<ConditionRow> rows{row(kInvalidOperator, kOneFloat, 42)};
  const std::vector<ConditionValue> values{known(1.0)};
  const auto result = fo4::env::evaluate_conditions(rows, values);
  EXPECT_EQ(result.verdict, ConditionVerdict::unevaluable);
  EXPECT_EQ(result.first_unevaluable_function, 42);
}

TEST(PackageRulesGrammar, UseGlobalComparesAgainstTheGlobalValue) {
  const std::vector<ConditionRow> rows{row(static_cast<std::uint8_t>(kEqual | kUseGlobal), 0x00001234u, 3)};
  ConditionValue value = known(5.0);
  value.global_known = true;
  value.global_value = 5.0;
  const std::vector<ConditionValue> values{value};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, values).verdict, ConditionVerdict::true_value);

  const std::vector<ConditionValue> missing_global{known(5.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, missing_global).verdict, ConditionVerdict::unevaluable);
}

TEST(PackageRulesGrammar, OrChainOfThreeIsTrueWhenAnyLinkIsTrue) {
  const std::vector<ConditionRow> rows{row(kOr | kEqual, kOneFloat, 1), row(kOr | kEqual, kOneFloat, 2),
                                       row(kEqual, kOneFloat, 3)};
  const std::vector<ConditionValue> middle_true{known(0.0), known(1.0), known(0.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, middle_true).verdict, ConditionVerdict::true_value);
  const std::vector<ConditionValue> last_true{known(0.0), known(0.0), known(1.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, last_true).verdict, ConditionVerdict::true_value);
  const std::vector<ConditionValue> none_true{known(0.0), known(0.0), known(0.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, none_true).verdict, ConditionVerdict::false_value);
}

TEST(PackageRulesGrammar, UnknownOrTrueIsTrue) {
  const std::vector<ConditionRow> rows{row(kOr | kEqual, kOneFloat, 7), row(kEqual, kOneFloat, 9)};
  const std::vector<ConditionValue> values{unknown(), known(1.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, values).verdict, ConditionVerdict::true_value);
}

TEST(PackageRulesGrammar, UnknownOrFalseIsUnevaluableNamingTheFunction) {
  const std::vector<ConditionRow> rows{row(kOr | kEqual, kOneFloat, 7), row(kEqual, kOneFloat, 9)};
  const std::vector<ConditionValue> values{unknown(), known(0.0)};
  const auto result = fo4::env::evaluate_conditions(rows, values);
  EXPECT_EQ(result.verdict, ConditionVerdict::unevaluable);
  EXPECT_EQ(result.first_unevaluable_row, 0u);
  EXPECT_EQ(result.first_unevaluable_function, 7);
}

TEST(PackageRulesGrammar, FalseAndUnknownIsFalse) {
  const std::vector<ConditionRow> rows{row(kEqual, kOneFloat, 9), row(kEqual, kOneFloat, 7)};
  const std::vector<ConditionValue> values{known(0.0), unknown()};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, values).verdict, ConditionVerdict::false_value);
}

TEST(PackageRulesGrammar, UnknownInAndIsUnevaluableNamingTheRow) {
  const std::vector<ConditionRow> rows{row(kEqual, kOneFloat, 9), row(kEqual, kOneFloat, 7)};
  const std::vector<ConditionValue> values{known(1.0), unknown()};
  const auto result = fo4::env::evaluate_conditions(rows, values);
  EXPECT_EQ(result.verdict, ConditionVerdict::unevaluable);
  EXPECT_EQ(result.first_unevaluable_row, 1u);
  EXPECT_EQ(result.first_unevaluable_function, 7);
}

TEST(PackageRulesGrammar, UnknownOrGroupStaysUnevaluableBeforeATrueAndGroup) {
  const std::vector<ConditionRow> rows{row(kOr | kEqual, kOneFloat, 7), row(kEqual, kOneFloat, 9),
                                       row(kEqual, kOneFloat, 3)};
  const std::vector<ConditionValue> values{unknown(), known(0.0), known(1.0)};
  const auto result = fo4::env::evaluate_conditions(rows, values);
  EXPECT_EQ(result.verdict, ConditionVerdict::unevaluable);
  EXPECT_EQ(result.first_unevaluable_row, 0u);
  EXPECT_EQ(result.first_unevaluable_function, 7);
}

TEST(PackageRulesGrammar, TrueOrGroupIsAndedWithAFalseGroup) {
  const std::vector<ConditionRow> rows{row(kOr | kEqual, kOneFloat, 7), row(kEqual, kOneFloat, 9),
                                       row(kEqual, kOneFloat, 3)};
  const std::vector<ConditionValue> values{unknown(), known(1.0), known(0.0)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, values).verdict, ConditionVerdict::false_value);
}

TEST(PackageRulesGrammar, ValuesShorterThanRowsAreUnknown) {
  const std::vector<ConditionRow> rows{row(kEqual, kOneFloat, 9), row(kEqual, kOneFloat, 7)};
  const std::vector<ConditionValue> values{known(1.0)};
  const auto result = fo4::env::evaluate_conditions(rows, values);
  EXPECT_EQ(result.verdict, ConditionVerdict::unevaluable);
  EXPECT_EQ(result.first_unevaluable_row, 1u);
  EXPECT_EQ(result.first_unevaluable_function, 7);
}

TEST(PackageRulesGrammar, RunOnAndReferenceEchoMustMatchTheRow) {
  const std::vector<ConditionRow> rows{row(kEqual, kOneFloat, 7, 1, 5)};
  const std::vector<ConditionValue> matching{echoed(1.0, 1, 5)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, matching).verdict, ConditionVerdict::true_value);
  const std::vector<ConditionValue> wrong_run_on{echoed(1.0, 0, 5)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, wrong_run_on).verdict, ConditionVerdict::unevaluable);
  const std::vector<ConditionValue> wrong_reference{echoed(1.0, 1, 4)};
  EXPECT_EQ(fo4::env::evaluate_conditions(rows, wrong_reference).verdict, ConditionVerdict::unevaluable);
}

TEST(PackageRulesSelection, FirstEligibleInCallerOrderIsSelected) {
  const std::vector<ConditionRow> none;
  std::vector<PackageCandidate> candidates(3);
  candidates[0].schedule = window(20, 0, 60);
  candidates[1].conditions = none;
  candidates[2].conditions = none;
  const auto selection = fo4::env::select_package(candidates, clock_at(480), -1);
  EXPECT_EQ(selection.status, SelectionStatus::selected);
  EXPECT_EQ(selection.selected_index, 1);
  ASSERT_EQ(selection.reasons.size(), 3u);
  EXPECT_EQ(selection.reasons[0], CandidateReason::schedule_outside_window);
  EXPECT_EQ(selection.reasons[1], CandidateReason::selected);
  EXPECT_EQ(selection.reasons[2], CandidateReason::not_reached);
}

TEST(PackageRulesSelection, UnevaluableCandidateBlocksLaterCandidates) {
  const std::vector<ConditionRow> unknown_rows{row(kEqual, kOneFloat, 7)};
  const std::vector<ConditionValue> unknown_values{unknown()};
  const std::vector<ConditionRow> none;
  std::vector<PackageCandidate> candidates(2);
  candidates[0].conditions = unknown_rows;
  candidates[0].values = unknown_values;
  candidates[1].conditions = none;
  const auto selection = fo4::env::select_package(candidates, clock_at(0), -1);
  EXPECT_EQ(selection.status, SelectionStatus::undecided);
  EXPECT_EQ(selection.blocking_index, 0);
  EXPECT_EQ(selection.reasons[0], CandidateReason::condition_unevaluable);
  EXPECT_EQ(selection.reasons[1], CandidateReason::not_reached);
}

void expect_schedule_blocks_later_candidates(const PackageScheduleFields& schedule) {
  const std::vector<ConditionRow> none;
  std::vector<PackageCandidate> candidates(2);
  candidates[0].schedule = schedule;
  candidates[1].conditions = none;
  const auto selection = fo4::env::select_package(candidates, clock_at(480), -1);
  EXPECT_EQ(selection.status, SelectionStatus::undecided);
  EXPECT_EQ(selection.blocking_index, 0);
  EXPECT_EQ(selection.reasons[0], CandidateReason::schedule_unevaluable);
  EXPECT_EQ(selection.reasons[1], CandidateReason::not_reached);
}

TEST(PackageRulesSelection, ZeroDurationScheduleIsSelectedOnlyAtItsStartMinute) {
  std::vector<PackageCandidate> candidates(2);
  candidates[0].schedule = window(8, 0, 0);
  const auto at_start = fo4::env::select_package(candidates, clock_at(480), -1);
  EXPECT_EQ(at_start.status, SelectionStatus::selected);
  EXPECT_EQ(at_start.selected_index, 0);
  const auto after = fo4::env::select_package(candidates, clock_at(481), -1);
  EXPECT_EQ(after.status, SelectionStatus::selected);
  EXPECT_EQ(after.selected_index, 1);
  EXPECT_EQ(after.reasons[0], CandidateReason::schedule_outside_window);
}

TEST(PackageRulesSelection, PartialScheduleBlocksLaterCandidates) {
  expect_schedule_blocks_later_candidates(window(8, -1, 60));
}

TEST(PackageRulesSelection, CalendarOnlyScheduleIsDecidedByTheCalendar) {
  PackageScheduleFields matching;
  matching.weekday = 2;
  matching.month = 3;
  matching.date = 14;
  PackageScheduleFields other_weekday;
  other_weekday.weekday = 5;
  std::vector<PackageCandidate> candidates(3);
  candidates[0].schedule = other_weekday;
  candidates[1].schedule = matching;
  candidates[2].schedule = PackageScheduleFields{};
  const auto selection = fo4::env::select_package(candidates, clock_at(480), -1);
  EXPECT_EQ(selection.status, SelectionStatus::selected);
  EXPECT_EQ(selection.selected_index, 1);
  EXPECT_EQ(selection.reasons[0], CandidateReason::schedule_outside_window);
  EXPECT_EQ(selection.reasons[1], CandidateReason::selected);
  EXPECT_EQ(selection.reasons[2], CandidateReason::not_reached);
}

TEST(PackageRulesSelection, DateZeroWindowIsEligibleOnAnyDay) {
  auto schedule = window(8, 0, 120);
  schedule.date = 0;
  std::vector<PackageCandidate> candidates(2);
  candidates[0].schedule = schedule;
  const auto selection = fo4::env::select_package(candidates, clock_at(490), -1);
  EXPECT_EQ(selection.status, SelectionStatus::selected);
  EXPECT_EQ(selection.selected_index, 0);
}

TEST(PackageRulesSelection, DefaultScheduleSentinelIsAlwaysEligible) {
  PackageScheduleFields sentinel;
  sentinel.month = 0;
  sentinel.date = 0;
  std::vector<PackageCandidate> candidates(2);
  candidates[0].schedule = sentinel;
  candidates[1].schedule = PackageScheduleFields{};
  for (const std::int32_t minute : {0, 480, 1439}) {
    const auto selection = fo4::env::select_package(candidates, clock_at(minute), -1);
    EXPECT_EQ(selection.status, SelectionStatus::selected);
    EXPECT_EQ(selection.selected_index, 0);
    EXPECT_EQ(selection.reasons[0], CandidateReason::selected);
    EXPECT_EQ(selection.reasons[1], CandidateReason::not_reached);
  }
}

TEST(PackageRulesSelection, ForcedCandidateWinsWithoutEvaluatingOthers) {
  const std::vector<ConditionRow> unknown_rows{row(kEqual, kOneFloat, 7)};
  const std::vector<ConditionValue> unknown_values{unknown()};
  const std::vector<ConditionRow> false_rows{row(kEqual, kOneFloat, 9)};
  const std::vector<ConditionValue> false_values{known(0.0)};
  std::vector<PackageCandidate> candidates(2);
  candidates[0].conditions = false_rows;
  candidates[0].values = false_values;
  candidates[1].conditions = unknown_rows;
  candidates[1].values = unknown_values;
  const auto selection = fo4::env::select_package(candidates, clock_at(0), 1);
  EXPECT_EQ(selection.status, SelectionStatus::selected);
  EXPECT_EQ(selection.selected_index, 1);
  EXPECT_EQ(selection.reasons[0], CandidateReason::overridden);
  EXPECT_EQ(selection.reasons[1], CandidateReason::forced);
}

TEST(PackageRulesSelection, ForcedIndexOutsideTheCandidateListThrows) {
  const std::vector<PackageCandidate> candidates(2);
  EXPECT_THROW(fo4::env::select_package(candidates, clock_at(0), -2), std::out_of_range);
  EXPECT_THROW(fo4::env::select_package(candidates, clock_at(0), 2), std::out_of_range);
}

TEST(PackageRulesSelection, EmptyCandidateListIsNoneEligible) {
  const std::vector<PackageCandidate> none;
  const auto selection = fo4::env::select_package(none, clock_at(0), -1);
  EXPECT_EQ(selection.status, SelectionStatus::none_eligible);
  EXPECT_EQ(selection.selected_index, -1);
  EXPECT_TRUE(selection.reasons.empty());
}

TEST(PackageRulesSelection, NoEligibleCandidateReportsNoneEligible) {
  const std::vector<ConditionRow> false_rows{row(kEqual, kOneFloat, 9)};
  const std::vector<ConditionValue> false_values{known(0.0)};
  std::vector<PackageCandidate> candidates(1);
  candidates[0].conditions = false_rows;
  candidates[0].values = false_values;
  const auto selection = fo4::env::select_package(candidates, clock_at(0), -1);
  EXPECT_EQ(selection.status, SelectionStatus::none_eligible);
  EXPECT_EQ(selection.selected_index, -1);
  EXPECT_EQ(selection.reasons[0], CandidateReason::condition_false);
}

radicle_condition c_row(std::uint8_t flags, std::uint32_t comparison, std::uint16_t function) {
  radicle_condition out{};
  out.flags_byte = flags;
  out.comparison_raw = comparison;
  out.function = function;
  return out;
}

radicle_package_schedule_fields c_no_schedule() {
  radicle_package_schedule_fields out{};
  out.month = -1;
  out.weekday = -1;
  out.date = -1;
  out.hour = -1;
  out.minute = -1;
  return out;
}

TEST(PackageRulesCApi, ConditionsEvaluateRejectsNullArraysWithACount) {
  radicle_condition_result result{};
  EXPECT_EQ(radicle_conditions_evaluate(nullptr, nullptr, 1, &result), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(PackageRulesCApi, ConditionsEvaluateZeroCountIsTrue) {
  radicle_condition_result result{};
  ASSERT_EQ(radicle_conditions_evaluate(nullptr, nullptr, 0, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_TRUE);
  EXPECT_EQ(result.rows_evaluated, 0u);
}

TEST(PackageRulesCApi, ConditionsEvaluateCarriesTheRunOnEcho) {
  radicle_condition rows[1] = {c_row(kEqual, kOneFloat, 7)};
  rows[0].run_on = 1;
  const radicle_condition_value matching[1] = {radicle_condition_value{1, 1.0, 0, 0.0, 1, 0}};
  const radicle_condition_value mismatched[1] = {radicle_condition_value{1, 1.0, 0, 0.0, 0, 0}};
  radicle_condition_result result{};
  ASSERT_EQ(radicle_conditions_evaluate(rows, matching, 1, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_TRUE);
  ASSERT_EQ(radicle_conditions_evaluate(rows, mismatched, 1, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result.verdict, RADICLE_CONDITION_VERDICT_UNEVALUABLE);
  EXPECT_EQ(result.first_unevaluable_function, 7);
}

TEST(PackageRulesCApi, SelectionRoundTripsThroughTheCAbi) {
  const radicle_condition rows[] = {c_row(kEqual | kOr, kOneFloat, 7), c_row(kEqual, kOneFloat, 9)};
  const radicle_condition_value values[] = {radicle_condition_value{0, 0.0, 0, 0.0, 0, 0},
                                            radicle_condition_value{1, 1.0, 0, 0.0, 0, 0}};
  radicle_condition_result verdict{};
  ASSERT_EQ(radicle_conditions_evaluate(rows, values, 2, &verdict), RADICLE_STATUS_OK);
  EXPECT_EQ(verdict.verdict, RADICLE_CONDITION_VERDICT_TRUE);
  EXPECT_EQ(verdict.rows_evaluated, 2u);

  radicle_package_schedule_fields schedule = c_no_schedule();
  schedule.hour = 8;
  schedule.minute = 0;
  schedule.duration_minutes = 60;
  radicle_schedule_clock clock{};
  clock.minute_of_day = 8 * 60 + 10;
  clock.month = 1;
  clock.previous_month = 1;

  radicle_schedule_window_result window_result{};
  ASSERT_EQ(radicle_schedule_window(&schedule, &clock, &window_result), RADICLE_STATUS_OK);
  EXPECT_EQ(window_result.evaluable, 1);
  EXPECT_EQ(window_result.in_window, 1);
  EXPECT_EQ(window_result.minutes_remaining, 50);

  radicle_package_candidate candidates[2] = {};
  candidates[0].schedule = schedule;
  candidates[0].schedule.hour = 20;
  candidates[1].schedule = c_no_schedule();
  candidates[1].conditions = rows;
  candidates[1].condition_count = 2;
  candidates[1].condition_values = values;
  candidates[1].condition_value_count = 2;

  radicle_package_candidate_reason reasons[2] = {};
  radicle_package_selection selection{};
  ASSERT_EQ(radicle_package_select(candidates, 2, &clock, -1, reasons, &selection), RADICLE_STATUS_OK);
  EXPECT_EQ(selection.status, RADICLE_PACKAGE_SELECTION_SELECTED);
  EXPECT_EQ(selection.selected_index, 1);
  EXPECT_EQ(reasons[0], RADICLE_PACKAGE_REASON_SCHEDULE_OUTSIDE_WINDOW);
  EXPECT_EQ(reasons[1], RADICLE_PACKAGE_REASON_SELECTED);
}

TEST(PackageRulesCApi, SelectRefusesMismatchedValueCount) {
  const radicle_condition rows[] = {c_row(kEqual, kOneFloat, 9)};
  radicle_package_candidate candidate{};
  candidate.conditions = rows;
  candidate.condition_count = 1;
  candidate.condition_values = nullptr;
  candidate.condition_value_count = 0;
  radicle_schedule_clock clock{};
  radicle_package_selection selection{};
  EXPECT_EQ(radicle_package_select(&candidate, 1, &clock, -1, nullptr, &selection), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(PackageRulesCApi, SelectRefusesForcedIndexOutsideTheList) {
  radicle_package_candidate candidate{};
  candidate.schedule = c_no_schedule();
  radicle_schedule_clock clock{};
  radicle_package_selection selection{};
  EXPECT_EQ(radicle_package_select(&candidate, 1, &clock, 1, nullptr, &selection), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_package_select(&candidate, 1, &clock, -2, nullptr, &selection), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(PackageRulesCApi, SelectAcceptsEmptyListAndNullReasons) {
  radicle_schedule_clock clock{};
  radicle_package_selection selection{};
  ASSERT_EQ(radicle_package_select(nullptr, 0, &clock, -1, nullptr, &selection), RADICLE_STATUS_OK);
  EXPECT_EQ(selection.status, RADICLE_PACKAGE_SELECTION_NONE_ELIGIBLE);
  EXPECT_EQ(selection.selected_index, -1);

  radicle_package_candidate candidate{};
  candidate.schedule = c_no_schedule();
  ASSERT_EQ(radicle_package_select(&candidate, 1, &clock, -1, nullptr, &selection), RADICLE_STATUS_OK);
  EXPECT_EQ(selection.status, RADICLE_PACKAGE_SELECTION_SELECTED);
  EXPECT_EQ(selection.selected_index, 0);
}

TEST(PackageRulesCApi, ZeroFilledCandidateIsUndecided) {
  radicle_package_candidate candidate{};
  radicle_schedule_clock clock{};
  radicle_package_candidate_reason reason{};
  radicle_package_selection selection{};
  ASSERT_EQ(radicle_package_select(&candidate, 1, &clock, -1, &reason, &selection), RADICLE_STATUS_OK);
  EXPECT_EQ(selection.status, RADICLE_PACKAGE_SELECTION_UNDECIDED);
  EXPECT_EQ(selection.blocking_index, 0);
  EXPECT_EQ(reason, RADICLE_PACKAGE_REASON_SCHEDULE_UNEVALUABLE);
}

}  // namespace
