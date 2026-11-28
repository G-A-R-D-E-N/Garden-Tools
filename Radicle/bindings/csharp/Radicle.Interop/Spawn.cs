using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: codes are the header's radicle_leveled_pick_result reason list.
public enum LeveledPickReason : byte
{
    Picked = 0,
    ChanceNone = 1,
    NoEntryAtLevel = 2,
    EntriesRolledOut = 3,
    NothingLive = 4,
    BudgetExceeded = 5,
    DepthExceeded = 6,
    ListMissing = 7,
    NotLeveledList = 8,
    Undecodable = 9,
    NotSpawnable = 10,
}

public enum SpawnBaseKind : byte
{
    None = 0,
    Npc = 1,
    LeveledNpc = 2,
    LeveledItem = 3,
    Other = 4,
}

public enum EnableStop : byte
{
    Root = 0,
    Overridden = 1,
    Cycle = 2,
    MissingParent = 3,
    DepthLimit = 4,
}

[Flags]
public enum LeveledFlagsOverride : uint
{
    None = 0,
    CalculateForEach = 0x02,
    UseAll = 0x04,
}

public sealed record LeveledPickOutcome(LeveledPickReason Reason, uint DepthUsed, IReadOnlyList<FormKey> Entries);

// Constraint: EntryCounts[i] is the row count of Entries[i]; it is empty when the native pointer is null.
public sealed record LeveledPickCountOutcome(LeveledPickReason Reason, uint DepthUsed, uint Rolls, uint Multiplier,
    IReadOnlyList<FormKey> Entries, IReadOnlyList<ushort> EntryCounts);

public readonly record struct RefEnableOverride(FormKey Ref, bool Enabled);

public sealed record EnableVerdictOutcome(bool Enabled, EnableStop Stop, uint ChainLength, FormKey? Deciding);

public sealed record PlacedActorSpawn(SpawnBaseKind BaseKind, FormKey? Base, bool ActorReference, bool Persistent,
    int? Count, int? LevelModifier, FormKey? EncounterZone, bool InitiallyDisabled, EnableVerdictOutcome Enable,
    FormKey? ResolvedBase, int? ZoneMinLevel, bool LevelRuleVerified, uint ResolvedLevel);

public sealed record SpawnPlanOutcome(SpawnBaseKind BaseKind, LeveledPickReason Reason, uint DepthUsed, FormKey? Base,
    IReadOnlyList<FormKey> Entries, int? LevelModifier);

public sealed unsafe partial class RadicleEnvironment
{
    public LeveledPickOutcome LeveledPick(FormKey list, uint level, ulong seed, LeveledFlagsOverride flagsOverride = LeveledFlagsOverride.None)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.LeveledPickResult* pick;
        Call.Check(RadicleNative.LeveledPick(use.Pointer, strings.Key(list), level, seed, (uint)flagsOverride, &pick));
        try { return new((LeveledPickReason)pick->Reason, pick->DepthUsed, PickedKeys(pick->Entries, pick->EntryCount)); }
        finally { RadicleNative.LeveledPickResultFree(pick); }
    }

    public LeveledPickCountOutcome LeveledPickCount(FormKey list, uint level, ulong seed, uint count,
        LeveledFlagsOverride flagsOverride = LeveledFlagsOverride.None)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.LeveledPickCountResult* pick;
        Call.Check(RadicleNative.LeveledPickCount(use.Pointer, strings.Key(list), level, seed, (uint)flagsOverride, count, &pick));
        try
        {
            return new((LeveledPickReason)pick->Reason, pick->DepthUsed, pick->Rolls, pick->Multiplier,
                PickedKeys(pick->Entries, pick->EntryCount),
                pick->EntryCounts == IntPtr.Zero ? Array.Empty<ushort>() : Call.Copy<ushort>(pick->EntryCounts, pick->EntryCount));
        }
        finally { RadicleNative.LeveledPickCountResultFree(pick); }
    }

    public EnableVerdictOutcome RefEnableState(Record placed, IReadOnlyList<RefEnableOverride>? overrides = null)
    {
        using var use = Use(placed);
        using var strings = new NativeStrings();
        var native = StateOverrides(strings, overrides);
        var verdict = default(Native.EnableVerdict);
        fixed (Native.RefStateOverride* pointer = native)
            Call.Check(RadicleNative.RefEnableState(use.Pointer, placed.Handle, pointer, (nuint)native.Length, &verdict));
        return Verdict(verdict);
    }

    public PlacedActorSpawn PlacedActorSpawn(Record placed, uint playerLevel, ulong seed, IReadOnlyList<RefEnableOverride>? overrides = null)
    {
        using var use = Use(placed);
        using var strings = new NativeStrings();
        var native = StateOverrides(strings, overrides);
        var spawn = default(Native.ActorSpawn);
        fixed (Native.RefStateOverride* pointer = native)
            Call.Check(RadicleNative.PlacedActorSpawnDecode(use.Pointer, placed.Handle, playerLevel, seed, pointer, (nuint)native.Length, &spawn));
        return new((SpawnBaseKind)spawn.BaseKind, spawn.HasBase == 0 ? null : FormKey.FromNative(spawn.Base),
            spawn.ActorReference != 0, spawn.Persistent != 0,
            spawn.HasCount == 0 ? null : (int?)spawn.Count, spawn.HasLevelModifier == 0 ? null : (int?)spawn.LevelModifier,
            spawn.HasEncounterZone == 0 ? null : FormKey.FromNative(spawn.EncounterZone), spawn.InitiallyDisabled != 0,
            Verdict(spawn.Enable), spawn.HasResolvedBase == 0 ? null : FormKey.FromNative(spawn.ResolvedBase),
            spawn.HasZoneMinLevel == 0 ? null : (int?)spawn.ZoneMinLevel, spawn.LevelRuleVerified != 0, spawn.ResolvedLevel);
    }

    public SpawnPlanOutcome ScriptSpawnPlan(FormKey spawnBase, uint level, ulong seed, int? levelModifier = null)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.SpawnPlan* plan;
        Call.Check(RadicleNative.ScriptSpawnPlan(use.Pointer, strings.Key(spawnBase), level, seed,
            levelModifier.HasValue ? 1 : 0, levelModifier.GetValueOrDefault(), &plan));
        try
        {
            return new((SpawnBaseKind)plan->BaseKind, (LeveledPickReason)plan->Reason, plan->DepthUsed,
                FormKey.FromNative(plan->Base), PickedKeys(plan->Entries, plan->EntryCount),
                plan->HasLevelModifier == 0 ? null : (int?)plan->LevelModifier);
        }
        finally { RadicleNative.SpawnPlanFree(plan); }
    }

    private static EnableVerdictOutcome Verdict(Native.EnableVerdict verdict) =>
        new(verdict.Enabled != 0, (EnableStop)verdict.Stop, verdict.ChainLength, FormKey.FromNative(verdict.Deciding));

    private static Native.RefStateOverride[] StateOverrides(NativeStrings strings, IReadOnlyList<RefEnableOverride>? overrides) =>
        (overrides ?? Array.Empty<RefEnableOverride>())
            .Select(item => new Native.RefStateOverride { Ref = strings.Key(item.Ref), Enabled = (byte)(item.Enabled ? 1 : 0) })
            .ToArray();

    private static FormKey[] PickedKeys(IntPtr items, nuint count) =>
        Call.Copy<Native.FormKey>(items, count).Select(key => FormKey.Required(key, "leveled pick entry")).ToArray();
}
