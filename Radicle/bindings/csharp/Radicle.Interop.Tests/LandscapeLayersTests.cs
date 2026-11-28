using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class LandscapeLayersTests : IDisposable
{
    private readonly TempDirectory _directory = new();

    public void Dispose() => _directory.Dispose();

    private static byte[] Header(uint texture, byte quadrant, ushort layer) =>
        U32(texture).Concat(new[] { quadrant, (byte)0 }).Concat(BitConverter.GetBytes(layer)).ToArray();

    private static byte[] Vtxt(params (ushort Position, float Opacity)[] entries) =>
        entries.SelectMany(e => BitConverter.GetBytes(e.Position).Concat(new byte[2]).Concat(BitConverter.GetBytes(e.Opacity))).ToArray();

    private RadicleEnvironment Load(Rec land)
    {
        var path = _directory.Write("Layers.esp", Build(0, Array.Empty<string>(), Top("LAND", land)));
        return RadicleEnvironment.Load(new[] { path });
    }

    [Fact]
    public void LayersKeepSubrecordOrderAndDecodeAlphaLikeTheManagedPath()
    {
        using var env = Load(new Rec("LAND", 0x800, new[]
        {
            ("BTXT", Header(0x810, 0, 0xFFFF)),
            ("ATXT", Header(0x811, 1, 3)),
            ("VTXT", Vtxt((20, 0.9f), (20, 0.1f), (21, float.NaN), (22, float.PositiveInfinity), (23, 2f), (24, -1f),
                          (288, 0.5f), (289, 1f), (ushort.MaxValue, 1f))),
            ("ATXT", Header(0x812, 2, 4)),
            ("ATXT", Header(0x813, 3, 5)),
            ("VTXT", Array.Empty<byte>()),
        }));
        var layers = env.LandscapeLayers(env.Winner(new FormKey("Layers.esp", 0x800))!.Value);

        Assert.Equal(4, layers.Count);
        Assert.Equal(new[] { true, false, false, false }, layers.Select(l => l.IsBase));
        Assert.Equal(new byte[] { 0, 1, 2, 3 }, layers.Select(l => l.Quadrant));
        Assert.Equal(new ushort[] { 0xFFFF, 3, 4, 5 }, layers.Select(l => l.Layer));
        Assert.Equal(new[] { 0x810u, 0x811u, 0x812u, 0x813u }, layers.Select(l => l.Texture!.Value.LocalId));
        Assert.Null(layers[0].Alpha);
        Assert.Null(layers[2].Alpha);
        Assert.Null(layers[3].Alpha);

        var alpha = layers[1].Alpha!;
        Assert.Equal(289, alpha.Length);
        Assert.Equal(0.1f, alpha[20]);
        Assert.Equal(0f, alpha[21]);
        Assert.Equal(0f, alpha[22]);
        Assert.Equal(1f, alpha[23]);
        Assert.Equal(0f, alpha[24]);
        Assert.Equal(0.5f, alpha[288]);
        Assert.Equal(1.6f, alpha.Sum(), 4);
    }

    [Fact]
    public void PartialVtxtTailIsRejectedByTheRecordDecoder()
    {
        using var env = Load(new Rec("LAND", 0x800, new[]
        {
            ("ATXT", Header(0x811, 1, 3)),
            ("VTXT", Vtxt((1, 1f)).Concat(new byte[4]).ToArray()),
        }));
        var land = env.Winner(new FormKey("Layers.esp", 0x800))!.Value;
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => env.LandscapeLayers(land)).Status);
    }

    [Fact]
    public void LayerlessLandYieldsNoLayers()
    {
        using var env = Load(new Rec("LAND", 0x800, new[] { ("DATA", U32(1)) }));
        Assert.Empty(env.LandscapeLayers(env.Winner(new FormKey("Layers.esp", 0x800))!.Value));
    }
}
