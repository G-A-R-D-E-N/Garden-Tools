using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record BehaviorCondition(byte FlagsByte, uint ComparisonRaw, ushort Function, ushort Unknown2,
    int Param1, int Param2, uint RunOn, uint Reference, int Unknown3, string? Parameter1String, string? Parameter2String);
public sealed record PackageData(uint Flags, byte Type, byte InterruptOverride, byte PreferredSpeed, byte Unknown, ushort InterruptFlags, ushort TrailingValue);
public sealed record PackageSchedule(sbyte Month, sbyte Weekday, sbyte Date, sbyte Hour, sbyte Minute, uint DurationMinutes);
public sealed record PackageCounter(uint InputCount, FormKey? TemplatePackage, uint Version);
public sealed record PackagePayload(string Signature, byte[] Bytes);
public sealed record PackageLocation(nuint SubrecordIndex, int Type, uint Value, int? Radius, uint? ItemIndex, FormKey? Form);
public sealed record PackageTarget(nuint SubrecordIndex, int Type, uint Value, int CountOrDistance, FormKey? Form);
public sealed record Package(PackageData? Data, PackageSchedule? Schedule, PackageCounter? Counter,
    FormKey? CombatStyle, FormKey? OwnerQuest, FormKey? BeginIdle, FormKey? EndIdle, FormKey? ChangeIdle,
    IReadOnlyList<BehaviorCondition> Conditions, IReadOnlyList<PackagePayload> Opaque,
    IReadOnlyList<PackageLocation> Locations, IReadOnlyList<PackageTarget> Targets, IReadOnlyList<FormKey?> IdleAnimations);
public sealed record CombatCloseRange(float[] Values, uint ThrowMaxTargets, float FlankVariance);
public sealed record CombatStyle(float[]? General, float[] Melee, float[] LongRange, CombatCloseRange? CloseRange,
    float[]? Flight, float? RangedAccuracy, float? CoverSearch, uint? Flags);
public sealed record IdleData(byte LoopMin, byte LoopMax, byte Flags, byte GroupSection, ushort ReplayDelay);
public sealed record Idle(IdleData? Data, FormKey? Parent, FormKey? Previous, string? BehaviorGraph,
    string? AnimationEvent, string? AnimationFile, IReadOnlyList<BehaviorCondition> Conditions);
public sealed record PackageProcedureInput(string TypeName, nuint SubrecordBegin, nuint SubrecordEnd, byte Index,
    string ValueTag, nuint ValueIndex, PackageInputValueKind Kind, bool Boolean, int Integer, uint IntegerRaw, float Real);
public sealed record PackageProcedureNode(bool IsLeaf, string Name, uint ConditionCount, uint ChildCount, uint? Flags,
    nuint OpaqueBegin, nuint OpaqueEnd, IReadOnlyList<nuint> Children);
public sealed record PackageProcedureTree(bool Decoded, nuint RegionBegin, nuint RegionEnd, byte? NextInputId,
    IReadOnlyList<PackageProcedureInput> Inputs, IReadOnlyList<PackageProcedureNode> Nodes);
public sealed record PackageInputEdit(PackageInputValueKind Kind, bool Bool = false, int Integer = 0,
    float Real = 0f, byte[]? Payload = null)
{
    internal Native.PackageInputValue ToNative(IntPtr payload, int size)
    {
        return new Native.PackageInputValue
        {
            Kind = Kind, Boolean = Bool ? 1 : 0, Integer = Integer, Real = Real,
            Payload = payload, PayloadSize = (nuint)size,
        };
    }
}

public sealed unsafe partial class RadicleEnvironment
{
    private static BehaviorCondition[] BehaviorConditions(IntPtr pointer, nuint count) =>
        Call.Copy<Native.BehaviorCondition>(pointer, count).Select(c => new BehaviorCondition(c.FlagsByte,
            c.ComparisonRaw, c.Function, c.Unknown2, c.Param1, c.Param2, c.RunOn, c.Reference, c.Unknown3,
            Utf8.Read(c.Parameter1String), Utf8.Read(c.Parameter2String))).ToArray();

    private static PackagePayload PackagePayloadValue(Native.PackagePayload p) => new(Signature(p.Signature), Utf8.Raw(p.Bytes, p.Size));

    public Package Package(Record record)
    {
        using var use = Use(record);
        Native.PackageInfo* p;
        Call.Check(RadicleNative.PackageDecode(use.Pointer, record.Handle, &p));
        try
        {
            PackageData? data = p->HasData == 0 ? null : new(p->Flags, p->Type, p->InterruptOverride, p->PreferredSpeed, p->Unknown, p->InterruptFlags, p->TrailingValue);
            PackageSchedule? schedule = p->HasSchedule == 0 ? null : new(p->Month, p->Weekday, p->Date, p->Hour, p->Minute, p->DurationMinutes);
            PackageCounter? counter = p->HasCounter == 0 ? null : new(p->InputCount, FormKey.FromNative(p->TemplatePackage), p->Version);
            return new(data, schedule, counter, FormKey.FromNative(p->CombatStyle), FormKey.FromNative(p->OwnerQuest),
                FormKey.FromNative(p->BeginIdle), FormKey.FromNative(p->EndIdle), FormKey.FromNative(p->ChangeIdle),
                BehaviorConditions(p->Conditions, p->ConditionCount),
                Call.Copy<Native.PackagePayload>(p->Opaque, p->OpaqueCount).Select(PackagePayloadValue).ToArray(),
                Call.Copy<Native.PackageLocation>(p->Locations, p->LocationCount).Select(l =>
                    new PackageLocation(l.SubrecordIndex, l.Type, l.Value, l.HasRadius == 0 ? null : l.Radius, l.HasItemIndex == 0 ? null : l.ItemIndex,
                        FormKey.FromNative(l.Form))).ToArray(),
                Call.Copy<Native.PackageTarget>(p->Targets, p->TargetCount).Select(t =>
                    new PackageTarget(t.SubrecordIndex, t.Type, t.Value, t.CountOrDistance, FormKey.FromNative(t.Form))).ToArray(),
                Call.Copy<Native.FormKey>(p->IdleAnimations, p->IdleAnimationCount).Select(FormKey.FromNative).ToArray());
        }
        finally { RadicleNative.PackageFree(p); }
    }

    private static float[] StyleFloats(float* values, int count) => new ReadOnlySpan<float>(values, count).ToArray();

    public CombatStyle CombatStyle(Record record)
    {
        using var use = Use(record);
        Native.CombatStyleInfo c;
        Call.Check(RadicleNative.CombatStyleGetInfo(use.Pointer, record.Handle, &c));
        return new(c.HasGeneral == 0 ? null : StyleFloats(c.General, 12), StyleFloats(c.Melee, checked((int)c.MeleeCount)),
            StyleFloats(c.LongRange, checked((int)c.LongRangeCount)),
            c.HasCloseRange == 0 ? null : new CombatCloseRange(StyleFloats(c.CloseRange, 9), c.ThrowMaxTargets, c.FlankVariance),
            c.HasFlight == 0 ? null : StyleFloats(c.Flight, 8), c.HasRangedAccuracy == 0 ? null : c.RangedAccuracy,
            c.HasCoverSearch == 0 ? null : c.CoverSearch, c.HasFlags == 0 ? null : c.Flags);
    }

    public Idle Idle(Record record)
    {
        using var use = Use(record);
        Native.IdleInfo* i;
        Call.Check(RadicleNative.IdleDecode(use.Pointer, record.Handle, &i));
        try
        {
            return new(i->HasData == 0 ? null : new IdleData(i->LoopMin, i->LoopMax, i->Flags, i->GroupSection, i->ReplayDelay),
                FormKey.FromNative(i->Parent), FormKey.FromNative(i->Previous), Utf8.Read(i->BehaviorGraph),
                Utf8.Read(i->AnimationEvent), Utf8.Read(i->AnimationFile), BehaviorConditions(i->Conditions, i->ConditionCount));
        }
        finally { RadicleNative.IdleFree(i); }
    }

    public PackageProcedureTree PackageProcedure(Record record)
    {
        using var use = Use(record);
        Native.PackageProcedureTree* t;
        Call.Check(RadicleNative.PackageProcedureDecode(use.Pointer, record.Handle, &t));
        try
        {
            return new(t->Decoded != 0, t->RegionBegin, t->RegionEnd, t->HasNextInputId == 0 ? null : t->NextInputId,
                Call.Copy<Native.PackageProcedureInput>(t->Inputs, t->InputCount).Select(i => new PackageProcedureInput(
                    Utf8.Read(i.TypeName) ?? string.Empty, i.SubrecordBegin, i.SubrecordEnd, i.Index,
                    Utf8.Read(i.ValueTag) ?? string.Empty, i.ValueIndex, i.Kind, i.Boolean != 0, i.Integer, i.IntegerRaw,
                    i.Real)).ToArray(),
                Call.Copy<Native.PackageProcedureNode>(t->Nodes, t->NodeCount).Select(n => new PackageProcedureNode(
                    n.IsLeaf != 0, Utf8.Read(n.Name) ?? string.Empty, n.ConditionCount, n.ChildCount, n.HasFlags == 0 ? null : n.Flags,
                    n.OpaqueBegin, n.OpaqueEnd, Call.Copy<nuint>(n.Children, n.ChildrenCount))).ToArray());
        }
        finally { RadicleNative.PackageProcedureFree(t); }
    }

    public static string? PackageLocationTypeName(int type) => Utf8.Read(RadicleNative.PackageLocationTypeName(type));

    public static string? PackageTargetTypeName(int type) => Utf8.Read(RadicleNative.PackageTargetTypeName(type));
}
