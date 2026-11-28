using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: <summary>Which of the three PERK effect data arms an effect uses.</summary>
public enum PerkEffectKind : byte
{
    QuestStage = 0,
    Ability = 1,
    EntryPoint = 2,
}

// Constraint: <summary>One CTDA condition inside a PERK condition tab.</summary>
public sealed class PerkCondition
{
    public byte Flags { get; init; }
    public byte[] Padding { get; init; } = new byte[3];
    public uint ComparisonRaw { get; init; }
    public ushort Function { get; init; }
    public ushort Unknown2 { get; init; }
    public int Param1 { get; init; }
    public int Param2 { get; init; }
    public uint RunOn { get; init; }
    public uint Reference { get; init; }
    public int Unknown3 { get; init; } = -1;
    public string? Parameter1 { get; init; }
    public string? Parameter2 { get; init; }
}

// Constraint: <summary>One PRKC run: the run-on value and the conditions under it.</summary>
public sealed class PerkConditionTab
{
    public sbyte RunOn { get; init; }
    public IReadOnlyList<PerkCondition> Conditions { get; init; } = Array.Empty<PerkCondition>();
}

// Constraint: <summary>One PERK effect section (one PRKE..PRKF run).</summary>
public sealed class PerkEffect
{
    public PerkEffectKind Type { get; init; }
    public byte Rank { get; init; }
    public byte Priority { get; init; }
    public FormKey? Quest { get; init; }
    public ushort QuestStage { get; init; }
    public FormKey? Ability { get; init; }
    public byte EntryPoint { get; init; }
    public byte Function { get; init; }
    public byte ConditionTabCount { get; init; }
    public IReadOnlyList<PerkConditionTab> Tabs { get; init; } = Array.Empty<PerkConditionTab>();
    public byte? ParameterType { get; init; }
    public ushort? EntryId { get; init; }
    public ushort? ScriptFlags { get; init; }
    public string? ButtonLabel { get; init; }
    public uint? ButtonLabelId { get; init; }
    public byte[]? ButtonLabelRaw { get; init; }
    public byte[]? Data { get; init; }
}

// Constraint: <summary>The PERK effect block and creation over <c>fo4/c_api_perk.h</c>.</summary>
public sealed unsafe partial class PluginFile
{
// Constraint: <summary>Reads every PRKE..PRKF effect of a PERK, in record order.</summary>
    public IReadOnlyList<PerkEffect> PerkEffects(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.PerkEffect* effects;
        nuint count;
        Call.Check(RadicleNative.PluginPerkEffects(use.Pointer, strings.Key(record), &effects, &count));
        try
        {
            var result = new PerkEffect[checked((int)count)];
            for (var i = 0; i < result.Length; ++i) result[i] = PerkEffectValue(effects[i]);
            return result;
        }
        finally
        {
            RadicleNative.PerkEffectsFree(effects);
        }
    }

// Constraint: <summary>Replaces the whole effect block; <c>false</c> when the record does not exist.</summary>
    public bool SetPerkEffects(FormKey record, IEnumerable<PerkEffect> effects)
    {
        ArgumentNullException.ThrowIfNull(effects);
        var values = effects.ToList();
        using var use = Use();
        using var strings = new NativeStrings();
        var pins = new List<GCHandle>();
        try
        {
            var rows = PerkEffectRows(values, strings, pins);
            return Call.Found(RadicleNative.PluginSetPerkEffects(
                use.Pointer, strings.Key(record), Pin(rows, pins), (nuint)rows.Length));
        }
        finally
        {
            foreach (var pin in pins) pin.Free();
        }
    }

// Constraint: <summary>Creates a PERK with its header and effects in one call.</summary>
    public FormKey CreatePerk(string editorId, IEnumerable<PerkEffect>? effects = null)
    {
        ArgumentNullException.ThrowIfNull(editorId);
        var values = effects?.ToList() ?? new List<PerkEffect>();
        using var use = Use();
        using var strings = new NativeStrings();
        var pins = new List<GCHandle>();
        try
        {
            var rows = PerkEffectRows(values, strings, pins);
            Native.FormKey key;
            Call.Check(RadicleNative.PluginCreatePerk(
                use.Pointer, editorId, Pin(rows, pins), (nuint)rows.Length, &key));
            return FormKey.FromNative(key)!.Value;
        }
        finally
        {
            foreach (var pin in pins) pin.Free();
        }
    }

    private static PerkEffect PerkEffectValue(Native.PerkEffect value)
    {
        var tabRows = (Native.PerkConditionTab*)value.Tabs;
        var tabs = new PerkConditionTab[checked((int)value.TabCount)];
        for (var tab = 0; tab < tabs.Length; ++tab)
        {
            var rows = (Native.PerkCondition*)tabRows[tab].Conditions;
            var conditions = new PerkCondition[checked((int)tabRows[tab].ConditionCount)];
            for (var i = 0; i < conditions.Length; ++i)
            {
                var row = rows[i];
                conditions[i] = new PerkCondition
                {
                    Flags = row.Flags,
                    Padding = new[] { row.Padding[0], row.Padding[1], row.Padding[2] },
                    ComparisonRaw = row.ComparisonRaw,
                    Function = row.Function,
                    Unknown2 = row.Unknown2,
                    Param1 = row.Param1,
                    Param2 = row.Param2,
                    RunOn = row.RunOn,
                    Reference = row.Reference,
                    Unknown3 = row.Unknown3,
                    Parameter1 = Utf8.Read(row.Parameter1),
                    Parameter2 = Utf8.Read(row.Parameter2),
                };
            }
            tabs[tab] = new PerkConditionTab { RunOn = tabRows[tab].RunOn, Conditions = conditions };
        }

        return new PerkEffect
        {
            Type = (PerkEffectKind)value.Type,
            Rank = value.Rank,
            Priority = value.Priority,
            Quest = FormKey.FromNative(value.Quest),
            QuestStage = value.QuestStage,
            Ability = FormKey.FromNative(value.Ability),
            EntryPoint = value.EntryPoint,
            Function = value.Function,
            ConditionTabCount = value.ConditionTabCount,
            Tabs = tabs,
            ParameterType = value.HasParameterType != 0 ? value.ParameterType : null,
            EntryId = value.HasEntryId != 0 ? value.EntryId : null,
            ScriptFlags = value.HasScriptFlags != 0 ? value.ScriptFlags : null,
            ButtonLabel = value.HasButtonLabel != 0 && value.ButtonLabel != IntPtr.Zero
                ? Utf8.Read(value.ButtonLabel)
                : null,
            ButtonLabelId = value.HasButtonLabel != 0 && value.ButtonLabel == IntPtr.Zero &&
                            value.ButtonLabelRawSize == 0
                ? value.ButtonLabelId
                : null,
            ButtonLabelRaw = value.HasButtonLabel != 0 && value.ButtonLabelRawSize != 0
                ? Utf8.Raw(value.ButtonLabelRaw, value.ButtonLabelRawSize)
                : null,
            Data = value.DataSize != 0 ? Utf8.Raw(value.Data, value.DataSize) : null,
        };
    }

    private static Native.PerkEffect[] PerkEffectRows(
        IReadOnlyList<PerkEffect> effects, NativeStrings strings, List<GCHandle> pins)
    {
        var rows = new Native.PerkEffect[effects.Count];
        for (var index = 0; index < rows.Length; ++index)
        {
            var effect = effects[index] ?? throw new ArgumentException("A PERK effect cannot be null.", nameof(effects));
            var tabs = effect.Tabs ?? Array.Empty<PerkConditionTab>();
            if (tabs.Count > byte.MaxValue)
                throw new ArgumentException("A PERK effect supports at most 255 condition tabs.", nameof(effects));

            var tabRows = new Native.PerkConditionTab[tabs.Count];
            for (var tab = 0; tab < tabRows.Length; ++tab)
            {
                var source = tabs[tab] ?? throw new ArgumentException("A PERK condition tab cannot be null.", nameof(effects));
                var conditions = source.Conditions ?? Array.Empty<PerkCondition>();
                var conditionRows = new Native.PerkCondition[conditions.Count];
                for (var i = 0; i < conditionRows.Length; ++i)
                    conditionRows[i] = PerkConditionRow(conditions[i], strings);
                tabRows[tab] = new Native.PerkConditionTab
                {
                    RunOn = source.RunOn,
                    Conditions = (IntPtr)Pin(conditionRows, pins),
                    ConditionCount = (nuint)conditionRows.Length,
                };
            }

            var row = new Native.PerkEffect
            {
                Type = (byte)effect.Type,
                Rank = effect.Rank,
                Priority = effect.Priority,
                Quest = strings.Key(effect.Quest),
                QuestStage = effect.QuestStage,
                Ability = strings.Key(effect.Ability),
                EntryPoint = effect.EntryPoint,
                Function = effect.Function,
// Constraint: The native writer derives the stored count from this array's length for entry
                // points, so a caller's stale count must never reach the record.
                ConditionTabCount = (byte)tabs.Count,
                Tabs = (IntPtr)Pin(tabRows, pins),
                TabCount = (nuint)tabRows.Length,
                HasParameterType = effect.ParameterType.HasValue ? 1 : 0,
                ParameterType = effect.ParameterType ?? 0,
                HasEntryId = effect.EntryId.HasValue ? 1 : 0,
                EntryId = effect.EntryId ?? 0,
                HasScriptFlags = effect.ScriptFlags.HasValue ? 1 : 0,
                ScriptFlags = effect.ScriptFlags ?? 0,
            };
            if (effect.ButtonLabelRaw is { Length: > 0 } raw)
            {
                row.HasButtonLabel = 1;
                row.ButtonLabelRaw = (IntPtr)Pin(raw, pins);
                row.ButtonLabelRawSize = (nuint)raw.Length;
            }
            else if (effect.ButtonLabel is not null)
            {
                row.HasButtonLabel = 1;
                row.ButtonLabel = strings.Add(effect.ButtonLabel);
            }
            else if (effect.ButtonLabelId is { } labelId)
            {
                row.HasButtonLabel = 1;
                row.ButtonLabelId = labelId;
            }
            if (effect.Data is { Length: > 0 } data)
            {
                row.Data = (IntPtr)Pin(data, pins);
                row.DataSize = (nuint)data.Length;
            }
            rows[index] = row;
        }
        return rows;
    }

    private static Native.PerkCondition PerkConditionRow(PerkCondition condition, NativeStrings strings)
    {
        ArgumentNullException.ThrowIfNull(condition);
        var padding = condition.Padding ?? new byte[3];
        if (padding.Length != 3)
            throw new ArgumentException("A PERK condition carries exactly three padding bytes.", nameof(condition));
        var row = new Native.PerkCondition
        {
            Flags = condition.Flags,
            ComparisonRaw = condition.ComparisonRaw,
            Function = condition.Function,
            Unknown2 = condition.Unknown2,
            Param1 = condition.Param1,
            Param2 = condition.Param2,
            RunOn = condition.RunOn,
            Reference = condition.Reference,
            Unknown3 = condition.Unknown3,
            Parameter1 = strings.Add(condition.Parameter1),
            Parameter2 = strings.Add(condition.Parameter2),
        };
        row.Padding[0] = padding[0];
        row.Padding[1] = padding[1];
        row.Padding[2] = padding[2];
        return row;
    }

    private static T* Pin<T>(T[] values, List<GCHandle> pins) where T : unmanaged
    {
        if (values.Length == 0) return null;
        var handle = GCHandle.Alloc(values, GCHandleType.Pinned);
        pins.Add(handle);
        return (T*)handle.AddrOfPinnedObject();
    }
}
