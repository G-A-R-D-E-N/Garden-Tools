using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct RadicleCondition(byte FlagsByte, uint ComparisonRaw, ushort Function, ushort Unknown2,
    int Param1, int Param2, uint RunOn, uint Reference, int Unknown3, string? Parameter1String, string? Parameter2String);

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
}
