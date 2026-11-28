using System;
using Xunit;

namespace Radicle.Interop.Tests;

public class PackageRulesTests
{
    private const byte Or = 0x01;
    private const byte Equal = 0x00;
    private const uint OneFloat = 0x3F800000u;

    [Fact]
    public void WildcardWindowReportsRemainingMinutesThroughTheNativeKernel()
    {
        var window = PackageRules.ScheduleWindow(new PackageSchedule(-1, -1, -1, 8, 0, 120), Clock(8 * 60 + 30));
        Assert.True(window.Evaluable);
        Assert.True(window.InWindow);
        Assert.Equal(90, window.MinutesRemaining);
        Assert.Equal(480, window.StartMinute);
        Assert.Equal(600, window.EndMinute);
    }

    [Fact]
    public void NoScheduleCandidateIsEligibleAndSelectedAsFirstCandidate()
    {
        var choice = PackageRules.SelectPackage(new[] { Candidate(PackageRules.NoSchedule) }, Clock(480));
        Assert.Equal(SelectionState.Selected, choice.State);
        Assert.Equal(0, choice.SelectedIndex);
        Assert.Equal(new[] { CandidateOutcome.Selected }, choice.Reasons);
    }

    [Fact]
    public void ZeroDurationScheduleIsSelectedOnlyAtItsStartMinute()
    {
        var candidates = new[] { Candidate(new PackageSchedule(-1, -1, -1, 8, 0, 0)), Candidate(PackageRules.NoSchedule) };
        var atStart = PackageRules.SelectPackage(candidates, Clock(480));
        Assert.Equal(SelectionState.Selected, atStart.State);
        Assert.Equal(0, atStart.SelectedIndex);
        var after = PackageRules.SelectPackage(candidates, Clock(481));
        Assert.Equal(1, after.SelectedIndex);
        Assert.Equal(new[] { CandidateOutcome.ScheduleOutsideWindow, CandidateOutcome.Selected }, after.Reasons);
    }

    [Fact]
    public void CalendarOnlyScheduleIsDecidedByTheCalendarWithoutBlocking()
    {
        var choice = PackageRules.SelectPackage(
            new[]
            {
                Candidate(new PackageSchedule(-1, 5, -1, -1, -1, 0)),
                Candidate(new PackageSchedule(-1, 2, -1, -1, -1, 0)),
                Candidate(PackageRules.NoSchedule),
            },
            Clock(480));
        Assert.Equal(SelectionState.Selected, choice.State);
        Assert.Equal(1, choice.SelectedIndex);
        Assert.Equal(
            new[] { CandidateOutcome.ScheduleOutsideWindow, CandidateOutcome.Selected, CandidateOutcome.NotReached },
            choice.Reasons);
    }

    [Fact]
    public void DateZeroAndMonthZeroAreAnyWildcards()
    {
        Assert.True(PackageRules.ScheduleWindow(new PackageSchedule(0, -1, 0, 8, 0, 120), Clock(490)).InWindow);
        var sentinel = PackageRules.ScheduleWindow(new PackageSchedule(0, -1, 0, -1, -1, 0), Clock(490));
        Assert.True(sentinel.Evaluable);
        Assert.True(sentinel.InWindow);
    }

    [Fact]
    public void MonthAndDateWildcardsMatchWhileFixedValuesMustEqualTheClock()
    {
        Assert.True(PackageRules.ScheduleWindow(new PackageSchedule(3, -1, 14, 8, 0, 120), Clock(490)).InWindow);
        Assert.False(PackageRules.ScheduleWindow(new PackageSchedule(4, -1, -1, 8, 0, 120), Clock(490)).InWindow);
        Assert.False(PackageRules.ScheduleWindow(new PackageSchedule(-1, -1, 15, 8, 0, 120), Clock(490)).InWindow);
    }

    [Fact]
    public void ForcedIndexOutsideTheListSurfacesAsInvalidArgument()
    {
        var candidates = new[] { Candidate(PackageRules.NoSchedule) };
        foreach (var forced in new[] { 1, -2 })
        {
            var error = Assert.Throws<RadicleException>(() => PackageRules.SelectPackage(candidates, Clock(0), forced));
            Assert.Equal(Radicle.Interop.Native.Status.InvalidArgument, error.Status);
        }
    }

    [Fact]
    public void EmptyCandidateListReportsNoneEligibleWithoutReasons()
    {
        var choice = PackageRules.SelectPackage(Array.Empty<PackageCandidateInput>(), Clock(0));
        Assert.Equal(SelectionState.NoneEligible, choice.State);
        Assert.Equal(-1, choice.SelectedIndex);
        Assert.Empty(choice.Reasons);
    }

    [Fact]
    public void OrGroupsStayCombinedWithAndAcrossGroups()
    {
        var rules = new[] { Rule(Or | Equal, 1), Rule(Equal, 2), Rule(Equal, 3) };
        Assert.Equal(RuleVerdict.True, PackageRules.EvaluateConditions(rules, new[] { Known(0), Known(1), Known(1) }).Verdict);
        Assert.Equal(RuleVerdict.False, PackageRules.EvaluateConditions(rules, new[] { Known(0), Known(0), Known(1) }).Verdict);
        Assert.Equal(RuleVerdict.False, PackageRules.EvaluateConditions(rules, new[] { Unknown(), Known(1), Known(0) }).Verdict);

        var blocked = PackageRules.EvaluateConditions(rules, new[] { Unknown(), Known(0), Known(1) });
        Assert.Equal(RuleVerdict.Unevaluable, blocked.Verdict);
        Assert.Equal(0UL, blocked.FirstUnevaluableRow);
        Assert.Equal(1, blocked.FirstUnevaluableFunction);
    }

    [Fact]
    public void ThreeRowOrChainIsTrueWhenAnyRowIsTrue()
    {
        var rules = new[] { Rule(Or | Equal, 1), Rule(Or | Equal, 2), Rule(Equal, 3) };
        Assert.Equal(RuleVerdict.True, PackageRules.EvaluateConditions(rules, new[] { Known(0), Known(0), Known(1) }).Verdict);
        Assert.Equal(RuleVerdict.True, PackageRules.EvaluateConditions(rules, new[] { Known(1), Known(0), Known(0) }).Verdict);
        Assert.Equal(RuleVerdict.False, PackageRules.EvaluateConditions(rules, new[] { Known(0), Known(0), Known(0) }).Verdict);
    }

    [Fact]
    public void RunOnEchoMismatchMakesTheRowUnevaluable()
    {
        var rules = new[] { new ConditionRule(Equal, OneFloat, 7, 1, 0) };
        var wrong = new[] { new ConditionInput(true, 1.0, false, 0, 0, 0) };
        var right = new[] { new ConditionInput(true, 1.0, false, 0, 1, 0) };
        Assert.Equal(RuleVerdict.Unevaluable, PackageRules.EvaluateConditions(rules, wrong).Verdict);
        Assert.Equal(RuleVerdict.True, PackageRules.EvaluateConditions(rules, right).Verdict);
    }

    [Fact]
    public void EmptyConditionListPasses()
    {
        var outcome = PackageRules.EvaluateConditions(Array.Empty<ConditionRule>(), Array.Empty<ConditionInput>());
        Assert.Equal(RuleVerdict.True, outcome.Verdict);
        Assert.Equal(0UL, outcome.RowsEvaluated);
    }

    [Fact]
    public void MismatchedRuleAndValueCountsAreRefusedBeforeTheNativeCall()
    {
        Assert.Throws<ArgumentException>(() => PackageRules.EvaluateConditions(
            new[] { Rule(Equal, 9) }, Array.Empty<ConditionInput>()));
    }

    private static ScheduleClockTime Clock(int minuteOfDay) => new(minuteOfDay, 3, 2, 14, 3, 1, 13);

    private static ConditionRule Rule(byte flags, ushort function) => new(flags, OneFloat, function, 0, 0);

    private static ConditionInput Known(double value) => new(true, value, false, 0, 0, 0);

    private static ConditionInput Unknown() => new(false, 0, false, 0, 0, 0);

    private static PackageCandidateInput Candidate(PackageSchedule schedule) =>
        new(schedule, Array.Empty<ConditionRule>(), Array.Empty<ConditionInput>());
}
