using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct RadicleCondition(byte FlagsByte, uint ComparisonRaw, ushort Function, ushort Unknown2,
    int Param1, int Param2, uint RunOn, uint Reference, int Unknown3, string? Parameter1String, string? Parameter2String);

public sealed record ConditionFunctionInfo(string Name, bool TakesFormParam, bool FirstParamIsQuest);

// Constraint: Number is the signed raw value for Integer, Enum, Other and Index and the float value for Float; Form is null for a null link and FormUnresolved is true when the id lies outside the record plugin's masters.
public readonly record struct RadicleConditionParam(ConditionParamKind Kind, string? TypeName, int Raw, double Number,
    string? Text, FormKey? Form, bool FormUnresolved);

public sealed record RadicleTypedCondition(string? FunctionName, RadicleConditionParam Param1, RadicleConditionParam Param2,
    RadicleConditionParam Comparison, RadicleConditionParam Reference);

// Invariant: the numbers are the CTDA run-on values of the compiled condition schema, and a test compares each member with the native row of the same number.
public enum ConditionRunOn : uint
{
    Subject = 0, Target = 1, Reference = 2, CombatTarget = 3, LinkedReference = 4, QuestAlias = 5,
    PackageData = 6, EventData = 7, CommandTarget = 8, EventCameraRef = 9, MyKiller = 10,
}

public sealed record ConditionRunOnRow(uint Value, string Name);

public static unsafe class ConditionFunctions
{
    static ConditionFunctions() => RadicleLibrary.Register();

    public static IReadOnlyList<ConditionRunOnRow> RunOns()
    {
        Native.ConditionSchemaMeta* schema;
        Call.Check(RadicleNative.ConditionSchemaGet(&schema));
        try
        {
            return Call.Copy<Native.ConditionRunOnMeta>(schema->RunOns, schema->RunOnCount)
                .Select(r => new ConditionRunOnRow(r.Value, Utf8.Read(r.Name) ?? string.Empty)).ToArray();
        }
        finally { RadicleNative.ConditionSchemaFree(schema); }
    }

    // Constraint: the name matches ASCII case-insensitively and ignores spaces, so "Combat Target" and "CombatTarget" both parse; null means the schema has no such run-on.
    public static ConditionRunOn? RunOn(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var wanted = name.Replace(" ", string.Empty);
        foreach (var row in RunOns())
            if (string.Equals(row.Name.Replace(" ", string.Empty), wanted, StringComparison.OrdinalIgnoreCase))
                return (ConditionRunOn)row.Value;
        return null;
    }

    // Constraint: the name matches ASCII case-insensitively; null means no CTDA function has that name.
    public static ushort? Id(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        ushort id;
        return Call.Found(RadicleNative.ConditionFunctionId(name, &id)) ? (ushort?)id : null;
    }

    // Constraint: null means the CTDA function table has no such id; TakesFormParam is true when the first parameter is a form link.
    public static ConditionFunctionInfo? Info(ushort id)
    {
        Native.ConditionFunctionFacts facts;
        return Call.Found(RadicleNative.ConditionFunctionInfo(id, &facts))
            ? new(Utf8.Read(facts.Name) ?? string.Empty, facts.TakesFormParam != 0, facts.FirstParamIsQuest != 0)
            : null;
    }
}

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<RadicleCondition> Conditions(Record record, string? scopePath = null)
    {
        using var use = Use(record);
        Native.ConditionList* list;
        Call.Check(RadicleNative.RecordConditions(use.Pointer, record.Handle, scopePath, &list));
        try
        {
            return Call.Copy<Native.Condition>(list->Rows, list->Count).Select(c => new RadicleCondition(c.FlagsByte,
                c.ComparisonRaw, c.Function, c.Unknown2, c.Param1, c.Param2, c.RunOn, c.Reference, c.Unknown3,
                Utf8.Read(c.Parameter1String), Utf8.Read(c.Parameter2String))).ToArray();
        }
        finally { RadicleNative.ConditionListFree(list); }
    }

    // Constraint: record must be the record the conditions were read from, because its master table resolves the FormIDs; entry i types conditions[i] and a bad FormID only marks that slot unresolved.
    public IReadOnlyList<RadicleTypedCondition> ConditionParameters(Record record, IReadOnlyList<RadicleCondition> conditions)
    {
        ArgumentNullException.ThrowIfNull(conditions);
        using var use = Use(record);
        using var strings = new NativeStrings();
        var rows = conditions.Select(c => new Native.Condition
        {
            FlagsByte = c.FlagsByte, ComparisonRaw = c.ComparisonRaw, Function = c.Function, Unknown2 = c.Unknown2,
            Param1 = c.Param1, Param2 = c.Param2, RunOn = c.RunOn, Reference = c.Reference, Unknown3 = c.Unknown3,
            Parameter1String = strings.Add(c.Parameter1String), Parameter2String = strings.Add(c.Parameter2String),
        }).ToArray();
        var typed = new Native.ConditionTyped[rows.Length];
        fixed (Native.Condition* rowsPtr = rows)
        fixed (Native.ConditionTyped* typedPtr = typed)
            Call.Check(RadicleNative.ConditionParams(use.Pointer, record.Handle, rowsPtr, (nuint)rows.Length, typedPtr));
        return typed.Select(t => new RadicleTypedCondition(Utf8.Read(t.FunctionName), ParamOf(t.Param1), ParamOf(t.Param2),
            ParamOf(t.Comparison), ParamOf(t.Reference))).ToArray();
    }

    private static RadicleConditionParam ParamOf(Native.ConditionParam p) =>
        new(p.Kind, Utf8.Read(p.TypeName), p.Raw, p.Number, Utf8.Read(p.Text), FormKey.FromNative(p.Form), p.FormUnresolved != 0);
}

// Constraint: RunOn is the raw condition run-on code, Reference the resolved run-on reference (only for the reference run-on), ReferenceRaw the row's raw reference field and Param3 its third CTDA parameter; the host decides who Subject, Target or Player is and must answer null for a run-on it cannot resolve.
public readonly record struct LiveSubject(uint RunOn, FormKey? Reference, uint ReferenceRaw, int Param3);

// Invariant: a null answer means unknown and never false; Radicle only echoes the host's numbers, narrowed to float, into the condition kernel.
public interface IConditionLiveState
{
    double? CurrentTime() => null;
    bool? IsDead(LiveSubject subject) => null;
    double? Level(LiveSubject subject) => null;
    bool? IsId(LiveSubject subject, FormKey form) => null;
    bool? IsRace(LiveSubject subject, FormKey race) => null;
    bool? InFaction(LiveSubject subject, FormKey faction) => null;
    // Constraint: the rank is -1 for a non-member, which is what GetFactionRank compares.
    int? FactionRank(LiveSubject subject, FormKey faction) => null;
    bool? HasKeyword(LiveSubject subject, FormKey keyword) => null;
    double? GlobalValue(FormKey global) => null;
    double? ActorValue(LiveSubject subject, FormKey actorValue) => null;
    int? QuestStage(FormKey quest) => null;
    bool? QuestStageDone(FormKey quest, int stage) => null;
    bool? QuestRunning(FormKey quest) => null;
    bool? QuestCompleted(FormKey quest) => null;
    bool? IsVoiceType(LiveSubject subject, FormKey voiceType) => null;
    // Constraint: the answer is the subject's sex ordinal, the number the CTDA sex parameter holds; Radicle compares it with the row.
    int? SexOf(LiveSubject subject) => null;
    int? ItemCount(LiveSubject subject, FormKey item) => null;
    // Constraint: quest is the quest of a QUST row or the parent quest of a SCEN row and null for any other record; alias is the row's raw alias id.
    bool? IsAliasRef(LiveSubject subject, FormKey? quest, int alias) => null;
}

public enum LiveGlobalSource { Host, RecordConstant, NotGlob, NoValue }

// Constraint: Value is null when Source is NotGlob or NoValue; RecordConstant means the host had no value and the GLOB carries the Constant record flag.
public sealed record LiveGlobalValueResult(double? Value, LiveGlobalSource Source);

// Requires: the bridge outlives the native call that holds its function pointer; a host exception is stored, answered as unknown, and rethrown by Rethrow after the call returns.
internal sealed unsafe class ConditionLiveBridge
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int QueryCallback(IntPtr user, IntPtr query, IntPtr outValue);

    private readonly IConditionLiveState _state;
    private readonly QueryCallback _callback;
    private ExceptionDispatchInfo? _fault;

    public ConditionLiveBridge(IConditionLiveState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        _state = state;
        _callback = Query;
    }

    public Native.ConditionLiveState Descriptor() => new()
    {
        StructSize = (nuint)sizeof(Native.ConditionLiveState),
        User = IntPtr.Zero,
        Query = Marshal.GetFunctionPointerForDelegate(_callback),
    };

    public void Rethrow()
    {
        GC.KeepAlive(_callback);
        _fault?.Throw();
    }

    private int Query(IntPtr user, IntPtr query, IntPtr outValue)
    {
        if (_fault is not null) return 0;
        try
        {
            if (Answer((Native.ConditionQuery*)query) is not { } value) return 0;
            *(double*)outValue = value;
            return 1;
        }
        catch (Exception ex)
        {
            _fault ??= ExceptionDispatchInfo.Capture(ex);
            return 0;
        }
    }

    private double? Answer(Native.ConditionQuery* q)
    {
        var subject = new LiveSubject(q->RunOn, FormKey.FromNative(q->Reference), q->ReferenceRaw, q->Param3);
        var form = FormKey.FromNative(q->Form);
        return q->Fact switch
        {
            Native.ConditionFact.CurrentTime => _state.CurrentTime(),
            Native.ConditionFact.IsDead => Flag(_state.IsDead(subject)),
            Native.ConditionFact.Level => _state.Level(subject),
            Native.ConditionFact.IsId when form is { } f => Flag(_state.IsId(subject, f)),
            Native.ConditionFact.IsRace when form is { } f => Flag(_state.IsRace(subject, f)),
            Native.ConditionFact.InFaction when form is { } f => Flag(_state.InFaction(subject, f)),
            Native.ConditionFact.FactionRank when form is { } f => _state.FactionRank(subject, f),
            Native.ConditionFact.HasKeyword when form is { } f => Flag(_state.HasKeyword(subject, f)),
            Native.ConditionFact.GlobalValue when form is { } f => _state.GlobalValue(f),
            Native.ConditionFact.ActorValue when form is { } f => _state.ActorValue(subject, f),
            Native.ConditionFact.QuestStage when form is { } f => _state.QuestStage(f),
            Native.ConditionFact.QuestStageDone when form is { } f => Flag(_state.QuestStageDone(f, q->Integer)),
            Native.ConditionFact.QuestRunning when form is { } f => Flag(_state.QuestRunning(f)),
            Native.ConditionFact.QuestCompleted when form is { } f => Flag(_state.QuestCompleted(f)),
            Native.ConditionFact.IsVoiceType when form is { } f => Flag(_state.IsVoiceType(subject, f)),
            Native.ConditionFact.IsSex => _state.SexOf(subject),
            Native.ConditionFact.ItemCount when form is { } f => _state.ItemCount(subject, f),
            Native.ConditionFact.IsAliasRef => Flag(_state.IsAliasRef(subject, form, q->Integer)),
            _ => null,
        };
    }

    private static double? Flag(bool? value) => value is { } v ? (v ? 1.0 : 0.0) : null;
}

public sealed unsafe partial class RadicleEnvironment
{
    // Constraint: entry i is the live value of conditions[i]; record must be the record the conditions were read from; an undecidable row is Known false and feeds PackageRules.SelectPackage unchanged.
    public IReadOnlyList<ConditionInput> ConditionInputs(Record record, IReadOnlyList<RadicleCondition> conditions,
        IConditionLiveState live)
    {
        ArgumentNullException.ThrowIfNull(conditions);
        var bridge = new ConditionLiveBridge(live);
        using var use = Use(record);
        using var strings = new NativeStrings();
        var rows = NativeConditions(conditions, strings);
        var values = new Native.ConditionValue[rows.Length];
        var descriptor = bridge.Descriptor();
        Status status;
        fixed (Native.Condition* rowsPtr = rows)
        fixed (Native.ConditionValue* valuesPtr = values)
            status = RadicleNative.ConditionLiveValues(use.Pointer, record.Handle, rowsPtr, (nuint)rows.Length, &descriptor, valuesPtr);
        bridge.Rethrow();
        Call.Check(status);
        return values.Select(v => new ConditionInput(v.Known != 0, v.Value, v.GlobalKnown != 0, v.GlobalValue,
            v.ResolvedRunOn, v.ResolvedReference)).ToArray();
    }

    // Constraint: unknown is never false; the outcome is Unevaluable only when no OR group is already false.
    public ConditionOutcome EvaluateConditions(Record record, IReadOnlyList<RadicleCondition> conditions,
        IConditionLiveState live)
    {
        ArgumentNullException.ThrowIfNull(conditions);
        var bridge = new ConditionLiveBridge(live);
        using var use = Use(record);
        using var strings = new NativeStrings();
        var rows = NativeConditions(conditions, strings);
        var descriptor = bridge.Descriptor();
        var result = default(Native.ConditionResult);
        Status status;
        fixed (Native.Condition* rowsPtr = rows)
            status = RadicleNative.ConditionsEvaluateLive(use.Pointer, record.Handle, rowsPtr, (nuint)rows.Length, &descriptor, &result);
        bridge.Rethrow();
        Call.Check(status);
        return new ConditionOutcome((RuleVerdict)(int)result.Verdict, (ulong)result.FirstUnevaluableRow,
            result.FirstUnevaluableFunction, (ulong)result.RowsEvaluated);
    }

    // Constraint: the host is asked first; only a GLOB with the Constant record flag falls back to its stored float, any other GLOB without a host value is NoValue and never its stored default.
    public LiveGlobalValueResult LiveGlobalValue(Record glob, IConditionLiveState live)
    {
        var bridge = new ConditionLiveBridge(live);
        using var use = Use(glob);
        var descriptor = bridge.Descriptor();
        double value;
        GlobLiveReason reason;
        var status = RadicleNative.GlobValueLive(use.Pointer, glob.Handle, &descriptor, &value, &reason);
        bridge.Rethrow();
        Call.Check(status);
        return reason switch
        {
            GlobLiveReason.Host => new(value, LiveGlobalSource.Host),
            GlobLiveReason.RecordConstant => new(value, LiveGlobalSource.RecordConstant),
            GlobLiveReason.NotGlob => new(null, LiveGlobalSource.NotGlob),
            _ => new(null, LiveGlobalSource.NoValue),
        };
    }

    private static Native.Condition[] NativeConditions(IReadOnlyList<RadicleCondition> conditions, NativeStrings strings) =>
        conditions.Select(c => new Native.Condition
        {
            FlagsByte = c.FlagsByte, ComparisonRaw = c.ComparisonRaw, Function = c.Function, Unknown2 = c.Unknown2,
            Param1 = c.Param1, Param2 = c.Param2, RunOn = c.RunOn, Reference = c.Reference, Unknown3 = c.Unknown3,
            Parameter1String = strings.Add(c.Parameter1String), Parameter2String = strings.Add(c.Parameter2String),
        }).ToArray();
}
