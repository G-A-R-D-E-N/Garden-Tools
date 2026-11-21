using System;
using System.Collections.Generic;
using System.Linq;
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

public static unsafe class ConditionFunctions
{
    static ConditionFunctions() => RadicleLibrary.Register();

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
