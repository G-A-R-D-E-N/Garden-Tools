using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record EffectiveWater(FormKey Water, Record? Record, bool UsesDefaultWater, float? Height);

public sealed unsafe partial class RadicleEnvironment
{
    public EffectiveWater EffectiveWater(Record record)
    {
        using var use = Use(record);
        WaterSelection selection;
        Call.Check(RadicleNative.WaterResolve(use.Pointer, record.Handle, &selection));
        return new EffectiveWater(FormKey.FromNative(selection.Water)!.Value,
            selection.Record == IntPtr.Zero ? null : new Record(selection.Record, this),
            selection.UsesDefaultWater != 0, selection.HasHeight != 0 ? selection.Height : null);
    }
}
