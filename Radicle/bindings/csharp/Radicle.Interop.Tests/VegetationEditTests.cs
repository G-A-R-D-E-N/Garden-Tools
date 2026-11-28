using System;
using System.Globalization;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class VegetationEditTests
{
    private static readonly FormKey GrassKey = new("Vegetation.esm", 0x800);
    private static readonly FormKey TreeKey = new("Vegetation.esm", 0x801);
    private static readonly FormKey FloraKey = new("Vegetation.esm", 0x802);

    private static byte[] GrassData() => new byte[] { 0xFE, 4, 90, 0xA5, 0xF4, 1, 0xA5, 0xA5 }.Concat(U32(3))
        .Concat(F32(32, 0.2f, 0.5f, 10)).Concat(new byte[] { 0x81, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5 }).ToArray();

    private static byte[] Source(byte[]? cnam = null, byte[]? grass = null) => Build(1, Array.Empty<string>(),
        Top("GRAS", new Rec("GRAS", 0x800, new[] { ("DATA", grass ?? GrassData()) })),
        Top("TREE", new Rec("TREE", 0x801, new[] { ("CNAM", cnam ?? F32(Enumerable.Range(1, 12).Select(i => (float)i).ToArray())), ("PFPC", new byte[] { 1, 2, 3, 4 }) })),
        Top("FLOR", new Rec("FLOR", 0x802, new[] { ("FNAM", new byte[] { 0x34, 0x80 }), ("PFPC", new byte[] { 5, 6, 7, 8 }) })));

    [Fact]
    public void EditsSaveReloadAndTouchOnlyTheRequestedBytes()
    {
        using var temp = new TempDirectory();
        var source = Source();
        var path = temp.Write("Vegetation.esm", source);
        using var plugin = PluginFile.Read(path);
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\Density", "254"));
        Assert.Equal(source, plugin.ToBytes());
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(GrassKey, "GRAS\\UnitsFromWaterType", "4294967296")).Status);
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\Density", "21"));
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\UnitsFromWaterType", "4294967295"));
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\UnitsFromWaterType", "6"));
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\PositionRange", "1e10"));
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\PositionRange", "32"));
        Assert.True(plugin.SetScalar(GrassKey, "GRAS\\WavePeriod", (12.5f).ToString(CultureInfo.InvariantCulture)));
        Assert.True(plugin.SetScalar(TreeKey, "TREE\\LeafFrequency", (-3.5f).ToString(CultureInfo.InvariantCulture)));
        Assert.True(plugin.SetScalar(TreeKey, "TREE\\Winter", "250"));
        Assert.True(plugin.SetScalar(FloraKey, "FLOR\\Flags", "513"));
        Assert.True(plugin.SetScalar(FloraKey, "FLOR\\Spring", "9"));
        var edited = plugin.ToBytes();
        Assert.Equal(source.Length, edited.Length);
        Assert.Equal(1 + 1 + 1 + 2 + 1 + 2 + 1, source.Zip(edited).Count(pair => pair.First != pair.Second));
        plugin.Write(path);
        using var env = RadicleEnvironment.Load(new[] { path });
        var grass = env.Grass(env.Winner(GrassKey)!.Value)!;
        Assert.Equal(new Grass(21, 4, 90, 500, 6, 32, 0.2f, 0.5f, 12.5f, 0x81), grass);
        var tree = env.Tree(env.Winner(TreeKey)!.Value);
        Assert.Equal(-3.5f, tree.Wind![11]);
        Assert.Equal(1f, tree.Wind[0]);
        Assert.Equal(new byte[] { 1, 2, 3, 250 }, tree.Harvest.Seasons);
        var flora = env.Flora(env.Winner(FloraKey)!.Value);
        Assert.Equal((ushort)513, flora.Flags);
        Assert.Equal(new byte[] { 9, 6, 7, 8 }, flora.Harvest.Seasons);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void RefusedEditsLeaveTheBytesUntouched()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Vegetation.esm", Source(cnam: new byte[44], grass: new byte[3]));
        using var plugin = PluginFile.Read(path);
        var before = plugin.ToBytes();
        Assert.False(plugin.SetScalar(new FormKey("Vegetation.esm", 0x999), "GRAS\\Density", "1"));
        Assert.False(plugin.SetScalar(GrassKey, "GRAS\\Flags", "1"));
        Assert.False(plugin.SetScalar(TreeKey, "TREE\\TrunkFlexibility", "1"));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(TreeKey, "GRAS\\Density", "1")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(GrassKey, "GRAS\\Nope", "1")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(FloraKey, "FLOR\\Flags", "65536")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(FloraKey, "FLOR\\Spring", "abc")).Status);
        Assert.False(plugin.SetScalar(GrassKey, "GRAS\\UnitsFromWaterType", "8"));
        Assert.Throws<ArgumentNullException>(() => plugin.SetScalar(GrassKey, null!, "1"));
        Assert.Throws<ArgumentNullException>(() => plugin.SetScalar(GrassKey, "GRAS\\Density", null!));
        Assert.Equal(before, plugin.ToBytes());
    }

    [Theory]
    [InlineData("nan")]
    [InlineData("inf")]
    [InlineData("1e39")]
    public void NonFiniteAndOutOfRangeFloatsAreRefused(string value)
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Vegetation.esm", Source());
        using var plugin = PluginFile.Read(path);
        var before = plugin.ToBytes();
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetScalar(GrassKey, "GRAS\\PositionRange", value)).Status);
        Assert.Equal(before, plugin.ToBytes());
    }
}
