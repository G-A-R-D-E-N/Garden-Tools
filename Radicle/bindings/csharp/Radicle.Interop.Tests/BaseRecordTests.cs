using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class BaseRecordTests
{
    [Fact]
    public void DecodesSkyAndBaseRecords()
    {
        using var temp = new TempDirectory();
        var colors = Enumerable.Range(0, 19 * 8).SelectMany(i => new byte[] { (byte)(i / 8), (byte)(i % 8), 0, 0 }).ToArray();
        var light = U32(0, 256, 0x00FF8040, 0x400).Concat(F32(1, 75, 10, 0, 0, 0, 1, 2)).ToArray();
        var plugin = temp.Write("Sky.esm", Build(0x1, Array.Empty<string>(),
            Top("LIGH", new Rec("LIGH", 0x800, new[] { ("DATA", light), ("FNAM", F32(1)) })),
            Top("LAYR", new Rec("LAYR", 0x810, Array.Empty<(string, byte[])>()), new Rec("LAYR", 0x811, new[] { ("PNAM", U32(0x810)) })),
            Top("CLMT", new Rec("CLMT", 0x820, new[] { ("WLST", U32(0x830, 100, 0)) })),
            Top("WTHR", new Rec("WTHR", 0x830, new[] { ("NAM0", colors), ("FNAM", F32(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140)) }, FormVersion: 119)),
            Top("SCOL", new Rec("SCOL", 0x840, new[] { ("ONAM", U32(0x850)), ("DATA", F32(1, 2, 3, 0, 0, 0, 1)) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        FormKey K(uint id) => new("Sky.esm", id);

        var lighting = env.Light(env.Winner(K(0x800))!.Value);
        Assert.Equal(256u, lighting.Data!.Radius);
        Assert.Equal(75f, lighting.Data.Fov);
        Assert.Equal(0x400u, lighting.Data.Flags);
        Assert.Equal(new ByteColor(0x40, 0x80, 0xFF), lighting.Data.Color);
        Assert.Equal(1f, lighting.Fade);
        Assert.Null(env.LayerParent(env.Winner(K(0x810))!.Value));
        Assert.Equal(K(0x810), env.LayerParent(env.Winner(K(0x811))!.Value));
        var climate = env.Climate(env.Winner(K(0x820))!.Value);
        Assert.Equal(K(0x830), climate.Weathers.Single().Weather);
        Assert.Equal(100, climate.Weathers.Single().Chance);
        var weather = env.Weather(env.Winner(K(0x830))!.Value);
        Assert.Equal((19, 8), (weather.ColorGroups, weather.ColorTimes));
        Assert.Equal(new ByteColor(8, 1, 0), weather.Color(8, Weather.Day));
        Assert.Equal(new[] { 10f, 20f, 30f, 40f, 50f, 60f, 70f, 80f, 90f, 100f, 110f, 120f, 130f, 140f }, weather.FogDistances);
        var part = env.StaticCollection(env.Winner(K(0x840))!.Value).Single();
        Assert.Equal(K(0x850), part.Static);
        Assert.Equal((1f, 2f, 3f), part.Placements.Single().Position);
    }
}
