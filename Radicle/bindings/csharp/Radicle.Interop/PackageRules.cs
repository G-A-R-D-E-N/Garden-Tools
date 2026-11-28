using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum RuleVerdict { False, True, Unevaluable }

public enum SelectionState { Selected, NoneEligible, Undecided }

public enum CandidateOutcome
{
    Selected,
    Forced,
    Overridden,
    NotReached,
    ScheduleOutsideWindow,
    ScheduleUnevaluable,
    ConditionFalse,
    ConditionUnevaluable,
}

public readonly record struct ScheduleClockTime(int MinuteOfDay, sbyte Month, sbyte Weekday, sbyte Date,
    sbyte PreviousMonth, sbyte PreviousWeekday, sbyte PreviousDate);

public readonly record struct ScheduleWindowInfo(bool Evaluable, bool InWindow, int StartMinute, int EndMinute,
    int MinutesRemaining);

public readonly record struct ConditionRule(byte FlagsByte, uint ComparisonRaw, ushort Function, uint RunOn,
    uint Reference);

public readonly record struct ConditionInput(bool Known, double Value, bool GlobalKnown, double GlobalValue,
    uint ResolvedRunOn, uint ResolvedReference);

public readonly record struct ConditionOutcome(RuleVerdict Verdict, ulong FirstUnevaluableRow,
    ushort FirstUnevaluableFunction, ulong RowsEvaluated);

public sealed record PackageCandidateInput(PackageSchedule Schedule, IReadOnlyList<ConditionRule> Conditions,
    IReadOnlyList<ConditionInput> Values);

public sealed record PackageChoice(SelectionState State, int SelectedIndex, int BlockingIndex,
    IReadOnlyList<CandidateOutcome> Reasons);

public static unsafe class PackageRules
{
    public static PackageSchedule NoSchedule => new(-1, -1, -1, -1, -1, 0);

    public static ScheduleWindowInfo ScheduleWindow(PackageSchedule schedule, ScheduleClockTime clock)
    {
        var nativeSchedule = ToNative(schedule);
        var nativeClock = ToNative(clock);
        var result = default(Native.ScheduleWindowResult);
        Call.Check(RadicleNative.ScheduleWindow(&nativeSchedule, &nativeClock, &result));
        return new ScheduleWindowInfo(result.Evaluable != 0, result.InWindow != 0, result.StartMinute,
            result.EndMinute, result.MinutesRemaining);
    }

    public static ConditionOutcome EvaluateConditions(IReadOnlyList<ConditionRule> rules,
        IReadOnlyList<ConditionInput> values)
    {
        if (rules.Count != values.Count) throw new ArgumentException("Each condition row needs exactly one value.");
        var nativeRules = new Native.Condition[rules.Count];
        for (var i = 0; i < nativeRules.Length; ++i) nativeRules[i] = ToNative(rules[i]);
        var nativeValues = new Native.ConditionValue[values.Count];
        for (var i = 0; i < nativeValues.Length; ++i) nativeValues[i] = ToNative(values[i]);
        var result = default(Native.ConditionResult);
        fixed (Native.Condition* rulePointer = nativeRules)
        fixed (Native.ConditionValue* valuePointer = nativeValues)
            Call.Check(RadicleNative.ConditionsEvaluate(rulePointer, valuePointer, (nuint)nativeRules.Length, &result));
        return new ConditionOutcome((RuleVerdict)(int)result.Verdict, (ulong)result.FirstUnevaluableRow,
            result.FirstUnevaluableFunction, (ulong)result.RowsEvaluated);
    }

    public static PackageChoice SelectPackage(IReadOnlyList<PackageCandidateInput> candidates, ScheduleClockTime clock,
        int forcedIndex = -1)
    {
        var pins = new List<GCHandle>();
        try
        {
            var natives = new Native.PackageCandidate[candidates.Count];
            for (var i = 0; i < natives.Length; ++i)
            {
                var candidate = candidates[i];
                var rules = new Native.Condition[candidate.Conditions.Count];
                for (var j = 0; j < rules.Length; ++j) rules[j] = ToNative(candidate.Conditions[j]);
                var values = new Native.ConditionValue[candidate.Values.Count];
                for (var j = 0; j < values.Length; ++j) values[j] = ToNative(candidate.Values[j]);
                natives[i] = new Native.PackageCandidate
                {
                    Schedule = ToNative(candidate.Schedule),
                    Conditions = Pin(rules, pins),
                    ConditionCount = (nuint)rules.Length,
                    ConditionValues = Pin(values, pins),
                    ConditionValueCount = (nuint)values.Length,
                };
            }
            var reasons = new Native.PackageCandidateReason[natives.Length];
            var nativeClock = ToNative(clock);
            var selection = default(Native.PackageSelection);
            fixed (Native.PackageCandidate* candidatePointer = natives)
            fixed (Native.PackageCandidateReason* reasonPointer = reasons)
                Call.Check(RadicleNative.PackageSelect(candidatePointer, (nuint)natives.Length, &nativeClock,
                    forcedIndex, reasonPointer, &selection));
            var outcomes = new CandidateOutcome[reasons.Length];
            for (var i = 0; i < outcomes.Length; ++i) outcomes[i] = (CandidateOutcome)(int)reasons[i];
            return new PackageChoice((SelectionState)(int)selection.Status, selection.SelectedIndex,
                selection.BlockingIndex, outcomes);
        }
        finally
        {
            foreach (var pin in pins) pin.Free();
        }
    }

    private static IntPtr Pin<T>(T[] items, List<GCHandle> pins) where T : unmanaged
    {
        if (items.Length == 0) return IntPtr.Zero;
        var handle = GCHandle.Alloc(items, GCHandleType.Pinned);
        pins.Add(handle);
        return handle.AddrOfPinnedObject();
    }

    private static Native.PackageScheduleFields ToNative(PackageSchedule schedule)
    {
        if (schedule is null) throw new ArgumentNullException(nameof(schedule));
        return new Native.PackageScheduleFields
        {
            Month = schedule.Month,
            Weekday = schedule.Weekday,
            Date = schedule.Date,
            Hour = schedule.Hour,
            Minute = schedule.Minute,
            DurationMinutes = schedule.DurationMinutes,
        };
    }

    private static Native.ScheduleClock ToNative(ScheduleClockTime clock) => new()
    {
        MinuteOfDay = clock.MinuteOfDay,
        Month = clock.Month,
        Weekday = clock.Weekday,
        Date = clock.Date,
        PreviousMonth = clock.PreviousMonth,
        PreviousWeekday = clock.PreviousWeekday,
        PreviousDate = clock.PreviousDate,
    };

    private static Native.Condition ToNative(ConditionRule rule) => new()
    {
        FlagsByte = rule.FlagsByte,
        ComparisonRaw = rule.ComparisonRaw,
        Function = rule.Function,
        RunOn = rule.RunOn,
        Reference = rule.Reference,
    };

    private static Native.ConditionValue ToNative(ConditionInput input) => new()
    {
        Known = input.Known ? 1 : 0,
        Value = input.Value,
        GlobalKnown = input.GlobalKnown ? 1 : 0,
        GlobalValue = input.GlobalValue,
        ResolvedRunOn = input.ResolvedRunOn,
        ResolvedReference = input.ResolvedReference,
    };
}
