using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record Grass(byte Density, byte MinSlope, byte MaxSlope, ushort UnitsFromWater, uint WaterType,
    float PositionRange, float HeightRange, float ColorRange, float WavePeriod, byte Flags);
public sealed record Harvest(FormKey? Ingredient, FormKey? Sound, byte[]? Seasons);
public sealed record Tree(Harvest Harvest, float[]? Wind);
public sealed record Flora(Harvest Harvest, ushort? Flags);

public sealed unsafe partial class RadicleEnvironment
{
    public Grass? Grass(Record record)
    {
        using var use = Use(record);
        Native.GrassInfo g;
        Call.Check(RadicleNative.GrassGetInfo(use.Pointer, record.Handle, &g));
        return g.HasData == 0 ? null : new(g.Density, g.MinSlope, g.MaxSlope, g.UnitsFromWater, g.WaterType,
            g.PositionRange, g.HeightRange, g.ColorRange, g.WavePeriod, g.Flags);
    }

    private static Harvest HarvestInfo(Native.HarvestInfo h) => new(FormKey.FromNative(h.Ingredient),
        FormKey.FromNative(h.Sound), h.HasSeasons == 0 ? null : new[] { h.Seasons[0], h.Seasons[1], h.Seasons[2], h.Seasons[3] });

    public Tree Tree(Record record)
    {
        using var use = Use(record);
        Native.TreeInfo t;
        Call.Check(RadicleNative.TreeGetInfo(use.Pointer, record.Handle, &t));
        float[]? wind = null;
        if (t.HasWind != 0) { wind = new float[12]; for (var i = 0; i < wind.Length; ++i) wind[i] = t.Wind[i]; }
        return new(HarvestInfo(t.Harvest), wind);
    }

    public Flora Flora(Record record)
    {
        using var use = Use(record);
        Native.FloraInfo f;
        Call.Check(RadicleNative.FloraGetInfo(use.Pointer, record.Handle, &f));
        return new(HarvestInfo(f.Harvest), f.HasFlags == 0 ? null : f.Flags);
    }

    public IReadOnlyList<FormKey?> LandscapeGrasses(Record record)
    {
        using var use = Use(record);
        Native.GrassLinks* g;
        Call.Check(RadicleNative.LandscapeGrassesDecode(use.Pointer, record.Handle, &g));
        try { return Call.Copy<Native.FormKey>(g->Grasses, g->GrassCount).Select(FormKey.FromNative).ToArray(); }
        finally { RadicleNative.LandscapeGrassesFree(g); }
    }
}
