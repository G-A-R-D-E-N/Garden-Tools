using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WaterAuthoringTests
{
    private const string Plugin = "Water.esm";

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Full = K(0x800), Short = K(0x801), Bare = K(0x802),
        Matt = K(0x810), Sndr = K(0x811), Spel = K(0x812), Imgs = K(0x813), Misc = K(0x814);

    private static byte[] Visual(int size)
    {
        var dnam = new byte[size];
        for (var offset = 0; offset + 4 <= Math.Min(size, 200); offset += 4) BitConverter.GetBytes(offset + 0.5f).CopyTo(dnam, offset);
        foreach (var color in new[] { 4, 8, 36, 96, 192, 196 })
        {
            if (color + 4 > size) continue;
            dnam[color] = 0x10;
            dnam[color + 1] = 0x20;
            dnam[color + 2] = 0x30;
            dnam[color + 3] = 0xFF;
        }
        if (size > 200) dnam[200] = 1;
        return dnam;
    }

    private static byte[] Source(uint flags = 1)
    {
        var edid = ("EDID", Z("Other"));
        var others = new[] { ("MATT", 0x810u), ("SNDR", 0x811u), ("SPEL", 0x812u), ("IMGS", 0x813u), ("MISC", 0x814u) };
        return Build(flags, Array.Empty<string>(),
            new[]
            {
                Top("WATR",
                    new Rec("WATR", 0x800, new[]
                    {
                        ("EDID", Z("Lake")), ("FULL", Z("Lake water")), ("ANAM", new byte[] { 0x50 }), ("FNAM", new byte[] { 1 }),
                        ("TNAM", U32(0x810)), ("SNAM", U32(0x811)), ("XNAM", U32(0x812)), ("YNAM", U32(0x812)), ("INAM", U32(0x813)),
                        ("DATA", Array.Empty<byte>()), ("DNAM", Visual(201)), ("GNAM", Enumerable.Repeat((byte)0xAB, 12).ToArray()),
                        ("NAM0", F32(1, 2, 3)), ("NAM1", F32(4, 5, 6)), ("NAM2", Z("water\\n1.dds")), ("NAM3", Z("water\\n2.dds")),
                        ("NAM4", Z("water\\n3.dds")),
                    }),
                    new Rec("WATR", 0x801, new[]
                    {
                        ("EDID", Z("Old")), ("ANAM", new byte[] { 0x27 }), ("FNAM", new byte[] { 0 }), ("XNAM", U32(0x812)),
                        ("INAM", U32(0x813)), ("DATA", Array.Empty<byte>()), ("DNAM", Visual(188)), ("GNAM", new byte[12]),
                        ("NAM0", F32(0, 0, 0)), ("NAM1", F32(0, 0, 0)), ("NAM2", Z("a.dds")), ("NAM3", Z("b.dds")), ("NAM4", Z("c.dds")),
                    }),
                    new Rec("WATR", 0x802, new[] { ("EDID", Z("Bare")), ("FNAM", new byte[] { 0 }), ("DNAM", Visual(201)) })),
            }.Concat(others.Select(o => Top(o.Item1, new Rec(o.Item1, o.Item2, new[] { edid })))).ToArray());
    }

    private static Status Refusal(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void VisualFieldsColorsAndLinksSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Full, "WATR\\DepthAmount", "2048"));
        Assert.True(plugin.SetScalar(Full, "WATR\\ShallowColorGreen", "200"));
        Assert.True(plugin.SetScalar(Full, "WATR\\LightColorBlue", "9"));
        Assert.True(plugin.SetScalar(Full, "WATR\\ScreenSpaceReflections", "0"));
        Assert.True(plugin.SetScalar(Full, "WATR\\Layer2UVScale", "50"));
        Assert.True(plugin.SetScalar(Full, "WATR\\Flags", "4"));
        Assert.True(plugin.SetScalar(Full, "WATR\\LinearVelocityZ", "-3.5"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Full, "WATR\\ShallowColorRed", "256")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Full, "WATR\\ScreenSpaceReflections", "2")));
        Assert.False(plugin.SetScalar(Short, "WATR\\SiltAmount", "1"));
        Assert.False(plugin.SetScalar(Short, "WATR\\DarkColorBlue", "1"));
        Assert.True(plugin.SetScalar(Short, "WATR\\Layer3NoiseFalloff", "123"));
        Assert.True(plugin.SetString(Full, "WATR\\Layer1NoiseTexture", "water\\new.dds"));
        Assert.True(plugin.SetString(Full, "WATR\\Name", "Pond"));
        Assert.False(plugin.SetString(Bare, "WATR\\Name", "Pond"));
        Assert.Equal(1, plugin.FieldCount(Full, "WATR\\ImageSpace"));
        Assert.Equal(0, plugin.FieldCount(Short, "WATR\\ContactSpell"));
        Assert.True(plugin.SetFormLink(Full, "WATR\\OpenSound", null));
        Assert.True(plugin.SetFormLink(Full, "WATR\\MaterialUnused", Matt));
        Assert.True(plugin.SetFormLink(Full, "WATR\\ContactSpell", Spel));
        Assert.False(plugin.SetFormLink(Short, "WATR\\ContactSpell", Spel));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Full, "WATR\\ImageSpace", null)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Full, "WATR\\ImageSpace", Misc)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Full, "WATR\\OpenSound", Imgs)));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var water = env.Water(env.Winner(Full)!.Value);
        Assert.Equal((byte?)4, water.Flags);
        Assert.Equal(2048f, water.Visual!.FogDepthAmount);
        Assert.Equal(new ByteColor(0x10, 200, 0x30), water.Visual.FogShallowColor);
        Assert.Equal(new ByteColor(0x10, 0x20, 9), water.Visual.SiltLightColor);
        Assert.False(water.Visual.ScreenSpaceReflections);
        Assert.Equal(50f, water.Noise[1].UvScale);
        Assert.Equal("water\\new.dds", water.Noise[0].Texture);
        Assert.Equal((1f, 2f, -3.5f), water.LinearVelocity);
        var old = env.Water(env.Winner(Short)!.Value);
        Assert.Null(old.Visual!.SiltAmount);
        Assert.Equal(123f, old.Noise[2].NoiseFalloff);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
        Assert.Equal(1, reloaded.FieldCount(Full, "WATR\\OpenSound"));
    }

    [Fact]
    public void OptionalSubrecordsAddRemoveAndLocalizedNameIsRefused()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.AddSubrecord(Bare, "SNAM"));
        Assert.True(plugin.SetFormLink(Bare, "WATR\\OpenSound", Sndr));
        Assert.True(plugin.AddSubrecord(Bare, "NAM0"));
        Assert.True(plugin.SetScalar(Bare, "WATR\\LinearVelocityY", "2"));
        Assert.True(plugin.AddSubrecord(Bare, "NAM2"));
        Assert.True(plugin.SetString(Bare, "WATR\\Layer1NoiseTexture", "n.dds"));
        foreach (var refused in new[] { "TNAM", "XNAM", "YNAM", "INAM", "DATA", "GNAM" })
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Bare, refused)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Bare, "DNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Bare, "FNAM")));
        Assert.True(plugin.RemoveSubrecord(Full, "GNAM"));
        Assert.False(plugin.RemoveSubrecord(Full, "GNAM"));
        Assert.True(plugin.RemoveSubrecord(Full, "TNAM"));
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var bare = env.Water(env.Winner(Bare)!.Value);
        Assert.Equal((0f, 2f, 0f), bare.LinearVelocity);
        Assert.Equal("n.dds", bare.Noise[0].Texture);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(1, reloaded.FieldCount(Bare, "WATR\\OpenSound"));
        Assert.Equal(0, reloaded.FieldCount(Full, "WATR\\MaterialUnused"));

        var localizedPath = Path.Combine(temp.Path, "Localized", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(localizedPath)!);
        File.WriteAllBytes(localizedPath, Source(0x81));
        using var localized = PluginFile.Read(localizedPath);
        Assert.Equal(Status.InvalidArgument, Refusal(() => localized.SetString(Full, "WATR\\Name", "Pond")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => localized.AddSubrecord(Bare, "FULL")));
        Assert.True(localized.SetString(Full, "WATR\\Layer3NoiseTexture", "l.dds"));
    }

    [Fact]
    public void CreatesWaterFromTheXEditDefaults()
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, "Created", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using (var plugin = PluginFile.Create(Plugin))
        {
            var water = plugin.CreateRecord("WATR", "NewWater");
            Assert.Equal(0x800u, water.LocalId);
            Assert.Equal(0, plugin.FieldCount(water, "WATR\\OpenSound"));
            Assert.True(plugin.SetScalar(water, "WATR\\Layer1UVScale", "64"));
            Assert.True(plugin.AddSubrecord(water, "NAM2"));
            Assert.True(plugin.SetString(water, "WATR\\Layer1NoiseTexture", "water\\c.dds"));
            plugin.Write(path);
        }

        using var env = RadicleEnvironment.Load(new[] { path });
        var created = env.Water(env.Winner(K(0x800))!.Value);
        Assert.Equal((byte?)0, created.Flags);
        Assert.Equal(8192f, created.Visual!.FogDepthAmount);
        Assert.Equal(0.6f, created.Visual.FogColorShallowRange);
        Assert.Equal(100000f, created.Visual.InteriorSpecularRadius);
        Assert.Equal(64f, created.Noise[0].UvScale);
        Assert.Equal(100f, created.Noise[1].UvScale);
        Assert.Equal(300f, created.Noise[2].NoiseFalloff);
        Assert.Equal(0f, created.Visual.SiltAmount);
        Assert.True(created.Visual.ScreenSpaceReflections);
        Assert.Equal("water\\c.dds", created.Noise[0].Texture);
        Assert.Null(created.LinearVelocity);
    }
}
