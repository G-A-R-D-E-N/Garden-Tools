using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record ItemAmount(FormKey Item, int Count);

public sealed record ActivatorRecord(FormKey Record, UiText FullName, UiText ActivateText, ushort? Flags,
                                     FormKey? InteractionKeyword, FormKey? LoopingSound, FormKey? ActivationSound,
                                     FormKey? WaterType);

public sealed record ContainerRecord(FormKey Record, UiText FullName, byte? Flags, float? Weight,
                                     IReadOnlyList<ItemAmount> Items, FormKey? OpenSound, FormKey? CloseSound,
                                     FormKey? TakeAllSound, FormKey? FilterList);

public sealed record DoorRecord(FormKey Record, UiText FullName, byte? Flags, FormKey? OpenSound, FormKey? CloseSound,
                                FormKey? LoopSound, UiText OpenText, UiText CloseText,
                                IReadOnlyList<FormKey> RandomDestinations);

public sealed record FurnitureMarkerSlot(int Index, ushort? DisabledEntryPoints);

public sealed record FurnitureEntry(ushort AnimType, ushort EntryPoints);

public sealed record FurnitureMarkerParam((float X, float Y, float Z) Offset, float RotationZ, FormKey? Keyword,
                                          byte EntryTypes);

// Constraint: EntryTypes is 0 for marker params below record form version 125; MarkerModel is null when XMRK is absent.
public sealed record FurnitureRecord(FormKey Record, UiText FullName, UiText ActivateText, ushort? Flags,
                                     uint? MarkerFlags, byte? BenchType, FormKey? DrinkingWaterType,
                                     FormKey? AssociatedForm, IReadOnlyList<ItemAmount> Items,
                                     IReadOnlyList<FurnitureMarkerSlot> Markers,
                                     IReadOnlyList<FurnitureEntry> EntryPoints,
                                     IReadOnlyList<FurnitureMarkerParam> MarkerParams, string? MarkerModel);

public sealed unsafe partial class RadicleEnvironment
{
    private static ItemAmount[] ItemAmounts(IntPtr items, nuint count) =>
        Call.Copy<Native.ItemCount>(items, count).Select(i => new ItemAmount(Required(i.Item), i.Count)).ToArray();

    private static FurnitureMarkerParam MarkerParam(Native.FurnitureMarkerParams p) =>
        new((p.Offset[0], p.Offset[1], p.Offset[2]), p.RotationZ, FormKey.FromNative(p.Keyword), p.EntryTypes);

    public ActivatorRecord Activator(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.ActivatorInfo* a;
        Call.Check(RadicleNative.ActivatorDecode(use.Pointer, record.Handle, &a));
        try
        {
            return new ActivatorRecord(Required(a->Record), UiTextOf(a->FullName, index, "ACTI", "FULL", strings),
                UiTextOf(a->ActivateText, index, "ACTI", "ATTX", strings), a->HasFlags == 0 ? null : a->Flags,
                FormKey.FromNative(a->InteractionKeyword), FormKey.FromNative(a->LoopingSound),
                FormKey.FromNative(a->ActivationSound), FormKey.FromNative(a->WaterType));
        }
        finally { RadicleNative.ActivatorFree(a); }
    }

    public ContainerRecord ContainerDefinition(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.ContainerInfo* c;
        Call.Check(RadicleNative.ContainerDecode(use.Pointer, record.Handle, &c));
        try
        {
            return new ContainerRecord(Required(c->Record), UiTextOf(c->FullName, index, "CONT", "FULL", strings),
                c->HasData == 0 ? null : c->Flags, c->HasData == 0 ? null : c->Weight, ItemAmounts(c->Items, c->ItemCount),
                FormKey.FromNative(c->OpenSound), FormKey.FromNative(c->CloseSound),
                FormKey.FromNative(c->TakeAllSound), FormKey.FromNative(c->FilterList));
        }
        finally { RadicleNative.ContainerFree(c); }
    }

    public DoorRecord Door(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.DoorInfo* d;
        Call.Check(RadicleNative.DoorDecode(use.Pointer, record.Handle, &d));
        try
        {
            return new DoorRecord(Required(d->Record), UiTextOf(d->FullName, index, "DOOR", "FULL", strings),
                d->HasFlags == 0 ? null : d->Flags, FormKey.FromNative(d->OpenSound), FormKey.FromNative(d->CloseSound),
                FormKey.FromNative(d->LoopSound), UiTextOf(d->OpenText, index, "DOOR", "ONAM", strings),
                UiTextOf(d->CloseText, index, "DOOR", "CNAM", strings),
                Call.Copy<Native.FormKey>(d->RandomDestinations, d->RandomDestinationCount).Select(Required).ToArray());
        }
        finally { RadicleNative.DoorFree(d); }
    }

    public FurnitureRecord Furniture(Record record, StringTables? strings = null)
    {
        var index = Info(record).PluginIndex;
        using var use = Use(record);
        Native.FurnitureInfo* f;
        Call.Check(RadicleNative.FurnitureDecode(use.Pointer, record.Handle, &f));
        try
        {
            return new FurnitureRecord(Required(f->Record), UiTextOf(f->FullName, index, "FURN", "FULL", strings),
                UiTextOf(f->ActivateText, index, "FURN", "ATTX", strings), f->HasFlags == 0 ? null : f->Flags,
                f->HasMarkerFlags == 0 ? null : f->MarkerFlags, f->HasBenchType == 0 ? null : f->BenchType,
                FormKey.FromNative(f->DrinkingWaterType), FormKey.FromNative(f->AssociatedForm),
                ItemAmounts(f->Items, f->ItemCount),
                Call.Copy<Native.FurnitureMarker>(f->Markers, f->MarkerCount).Select(m =>
                    new FurnitureMarkerSlot(m.Index, m.HasDisabledEntryPoints == 0 ? null : m.DisabledEntryPoints)).ToArray(),
                Call.Copy<Native.FurnitureEntryPoint>(f->EntryPoints, f->EntryPointCount)
                    .Select(e => new FurnitureEntry(e.AnimType, e.EntryPoints)).ToArray(),
                Call.Copy<Native.FurnitureMarkerParams>(f->MarkerParams, f->MarkerParamCount).Select(MarkerParam).ToArray(),
                Utf8.Read(f->MarkerModel));
        }
        finally { RadicleNative.FurnitureFree(f); }
    }
}
