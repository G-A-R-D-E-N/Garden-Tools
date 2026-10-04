using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class HlodFieldTests
{
    private static byte[] Slot(string mesh)
    {
        var slot = new byte[260];
        var text = Z(mesh);
        Array.Copy(text, slot, text.Length);
        return slot;
    }

    private static byte[] WaterVisual()
    {
        var dnam = new byte[201];
        BitConverter.GetBytes(1500f).CopyTo(dnam, 0);
        dnam[4] = 10;
        dnam[5] = 20;
        dnam[6] = 30;
        BitConverter.GetBytes(0.25f).CopyTo(dnam, 128);
        BitConverter.GetBytes(300f).CopyTo(dnam, 176);
        dnam[200] = 1;
        return dnam;
    }

    [Fact]
    public void DecodesDistantLodWaterTypeAndWaterRecords()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Fields.esm", Build(0x1, Array.Empty<string>(),
            Top("STAT",
                new Rec("STAT", 0x800, new[] { ("MNAM", Slot(@"Meshes\LOD\a_lod_0.nif").Concat(Slot(@"Meshes\LOD\a_lod_1.nif")).ToArray()) }),
                new Rec("STAT", 0x801, Array.Empty<(string, byte[])>())),
            Top("ACTI", new Rec("ACTI", 0x810, new[] { ("WNAM", U32(0x840)) })),
            Top("WATR", new Rec("WATR", 0x840, new[]
            {
                ("FNAM", new byte[] { 0x05 }), ("DNAM", WaterVisual()), ("NAM0", F32(1, 2, 3)),
                ("NAM2", Z(@"Textures\water\noise.dds")),
            })),
            Top("CELL", new Grp(0, 2, new Node[]
            {
                new Grp(0, 3, new Node[]
                {
                    new Rec("CELL", 0x820, new[] { ("DATA", new byte[] { 1 }), ("XCWT", U32(0x840)) }),
                }),
            })),
            Top("WRLD", new Rec("WRLD", 0x830, new[]
            {
                ("EDID", Z("World")), ("DNAM", F32(-2048, 0)), ("NAM3", U32(0x840)), ("NAM4", F32(-1024)),
            }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        FormKey K(uint id) => new("Fields.esm", id);

        var lod = env.StaticDistantLod(env.Winner(K(0x800))!.Value);
        Assert.Equal(2, lod.LevelCount);
        Assert.Equal(@"Meshes\LOD\a_lod_0.nif", lod.Meshes[0]);
        Assert.Equal(@"Meshes\LOD\a_lod_1.nif", lod.Meshes[1]);
        Assert.Null(lod.Meshes[2]);
        Assert.Null(lod.Meshes[3]);

        var none = env.StaticDistantLod(env.Winner(K(0x801))!.Value);
        Assert.Equal(0, none.LevelCount);
        Assert.All(none.Meshes, m => Assert.Null(m));

        Assert.Equal(K(0x840), env.ActivatorWaterType(env.Winner(K(0x810))!.Value));

        var water = env.Water(env.Winner(K(0x840))!.Value);
        Assert.Equal((byte?)5, water.Flags);
        Assert.Equal(1500f, water.Visual!.FogDepthAmount);
        Assert.Equal(new ByteColor(10, 20, 30), water.Visual.FogShallowColor);
        Assert.Equal(0.25f, water.Noise[0].WindDirection);
        Assert.Equal(300f, water.Noise[0].NoiseFalloff);
        Assert.Equal(@"Textures\water\noise.dds", water.Noise[0].Texture);
        Assert.Null(water.Noise[1].Texture);
        Assert.Equal((1f, 2f, 3f), water.LinearVelocity);
        Assert.True(water.Visual.ScreenSpaceReflections);

        Assert.Equal(K(0x840), env.Cell(env.Winner(K(0x820))!.Value).Water);

        var world = env.Worldspace(env.Winner(K(0x830))!.Value);
        Assert.Equal(K(0x840), world.LodWater);
        Assert.Equal(-1024f, world.LodWaterHeight);
    }
}
